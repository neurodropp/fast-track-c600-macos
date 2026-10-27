// Short USB capture transport check for M-Audio Fast Track C600.
// No audio is stored. Copyright (c) 2026 Fast Track C600 contributors. MIT.
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { VENDOR_ID = 0x0763, PRODUCT_ID = 0x2031, FRAMES_PER_REQUEST = 8,
       REQUEST_COUNT = 4, CAPTURE_PACKET_BYTES = 234 };

typedef struct {
    UInt8 bytes[FRAMES_PER_REQUEST * CAPTURE_PACKET_BYTES];
    IOUSBIsocFrame frames[FRAMES_PER_REQUEST];
} Transfer;

static IOUSBInterfaceInterface300 **capture_iface;
static UInt8 capture_pipe;
static UInt64 next_bus_frame;
static Transfer transfers[REQUEST_COUNT];
static unsigned long packet_count, byte_count, packet_errors, submit_errors;
static volatile int stopping;

static int property_equals(io_service_t service, CFStringRef key, int expected) {
    CFTypeRef p = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    int found = -1;
    int ok = p && CFGetTypeID(p) == CFNumberGetTypeID() &&
             CFNumberGetValue(p, kCFNumberIntType, &found) && found == expected;
    if (p) CFRelease(p);
    return ok;
}

static IOUSBInterfaceInterface300 **get_interface(int number) {
    io_iterator_t iter = IO_OBJECT_NULL;
    if (IOServiceGetMatchingServices(kIOMainPortDefault,
            IOServiceMatching(kIOUSBInterfaceClassName), &iter) != KERN_SUCCESS) return NULL;
    io_service_t service = IO_OBJECT_NULL;
    while ((service = IOIteratorNext(iter))) {
        if (property_equals(service, CFSTR(kUSBVendorID), VENDOR_ID) &&
            property_equals(service, CFSTR(kUSBProductID), PRODUCT_ID) &&
            property_equals(service, CFSTR(kUSBInterfaceNumber), number)) break;
        IOObjectRelease(service);
    }
    IOObjectRelease(iter);
    if (!service) return NULL;
    IOCFPlugInInterface **plugin = NULL;
    SInt32 score = 0;
    kern_return_t status = IOCreatePlugInInterfaceForService(
        service, kIOUSBInterfaceUserClientTypeID, kIOCFPlugInInterfaceID, &plugin, &score);
    IOObjectRelease(service);
    if (status != KERN_SUCCESS || !plugin) return NULL;
    IOUSBInterfaceInterface300 **iface = NULL;
    HRESULT q = (*plugin)->QueryInterface(
        plugin, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID300), (LPVOID *)&iface);
    (*plugin)->Release(plugin);
    return q == S_OK ? iface : NULL;
}

static IOReturn set_clock(IOUSBInterfaceInterface300 **control) {
    // Select internal clock source 0x81, then request 48 kHz. This is volatile
    // device state and can be restored by reconnecting the USB cable.
    UInt8 pin = 1;
    IOUSBDevRequestTO req = {0};
    req.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBClass, kUSBInterface);
    req.bRequest = 0x01; // UAC2 SET_CUR
    req.wValue = 0x0100;
    req.wIndex = (0x80 << 8) | 1;
    req.wLength = 1;
    req.pData = &pin;
    req.noDataTimeout = 100;
    req.completionTimeout = 500;
    IOReturn result = (*control)->ControlRequestTO(control, 0, &req);
    if (result != kIOReturnSuccess) return result;
    UInt32 rate = 48000;
    req.wIndex = (0x81 << 8) | 1;
    req.wLength = sizeof(rate);
    req.pData = &rate;
    return (*control)->ControlRequestTO(control, 0, &req);
}

static void submit_transfer(unsigned index);

static void capture_done(void *context, IOReturn result, void *argument) {
    (void)argument;
    unsigned index = (unsigned)(uintptr_t)context;
    if (result != kIOReturnSuccess) packet_errors += FRAMES_PER_REQUEST;
    for (unsigned i = 0; i < FRAMES_PER_REQUEST; ++i) {
        IOUSBIsocFrame *f = &transfers[index].frames[i];
        if (f->frStatus == kIOReturnSuccess || f->frStatus == kIOReturnUnderrun) {
            packet_count++;
            byte_count += f->frActCount;
        } else packet_errors++;
    }
    if (!stopping) submit_transfer(index);
}

static void submit_transfer(unsigned index) {
    for (unsigned i = 0; i < FRAMES_PER_REQUEST; ++i) {
        transfers[index].frames[i].frStatus = 0;
        transfers[index].frames[i].frReqCount = CAPTURE_PACKET_BYTES;
        transfers[index].frames[i].frActCount = 0;
    }
    IOReturn result = (*capture_iface)->ReadIsochPipeAsync(
        capture_iface, capture_pipe, transfers[index].bytes, next_bus_frame,
        FRAMES_PER_REQUEST, transfers[index].frames,
        capture_done, (void *)(uintptr_t)index);
    if (result != kIOReturnSuccess) {
        fprintf(stderr, "Isochronous capture submit: 0x%08x\n", result);
        submit_errors++;
        stopping = 1;
    }
    next_bus_frame++;
}

int main(void) {
    IOUSBInterfaceInterface300 **control = get_interface(1);
    capture_iface = get_interface(3);
    if (!control || !capture_iface) {
        fprintf(stderr, "C600 audio interfaces not found\n"); return 1;
    }
    IOReturn result = (*control)->USBInterfaceOpen(control);
    if (result != kIOReturnSuccess) {
        fprintf(stderr, "Cannot open control interface: 0x%08x\n", result); return 1;
    }
    result = set_clock(control);
    if (result != kIOReturnSuccess) {
        fprintf(stderr, "Cannot set internal 48 kHz clock: 0x%08x\n", result); return 1;
    }
    result = (*capture_iface)->USBInterfaceOpen(capture_iface);
    if (result != kIOReturnSuccess) {
        fprintf(stderr, "Cannot open capture interface: 0x%08x\n", result); return 1;
    }
    result = (*capture_iface)->SetAlternateInterface(capture_iface, 1);
    if (result != kIOReturnSuccess) {
        fprintf(stderr, "Cannot select capture stream: 0x%08x\n", result); return 1;
    }
    UInt8 endpoint_count = 0;
    result = (*capture_iface)->GetNumEndpoints(capture_iface, &endpoint_count);
    if (result != kIOReturnSuccess) return 1;
    for (UInt8 pipe = 1; pipe <= endpoint_count; ++pipe) {
        UInt8 direction, number, type, interval;
        UInt16 max_packet;
        if ((*capture_iface)->GetPipeProperties(capture_iface, pipe,
                &direction, &number, &type, &max_packet, &interval) == kIOReturnSuccess &&
            direction == kUSBIn && type == kUSBIsoc && number == 1) capture_pipe = pipe;
    }
    if (!capture_pipe) { fprintf(stderr, "Capture endpoint 0x81 unavailable\n"); return 1; }
    CFRunLoopSourceRef source = NULL;
    result = (*capture_iface)->CreateInterfaceAsyncEventSource(capture_iface, &source);
    if (result != kIOReturnSuccess || !source) {
        fprintf(stderr, "Cannot create USB event source: 0x%08x\n", result); return 1;
    }
    CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopDefaultMode);
    UInt64 frame = 0;
    AbsoluteTime timestamp;
    result = (*capture_iface)->GetBusFrameNumber(capture_iface, &frame, &timestamp);
    if (result != kIOReturnSuccess) return 1;
    next_bus_frame = frame + 6;
    for (unsigned i = 0; i < REQUEST_COUNT && !stopping; ++i) submit_transfer(i);
    if (!stopping) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 2.0, false);
    stopping = 1;
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.2, false);
    printf("capture: packets=%lu bytes=%lu errors=%lu submit_errors=%lu\n",
           packet_count, byte_count, packet_errors, submit_errors);
    printf("approximate audio duration: %.3f s (6 channels, 24 bit, 48 kHz)\n",
           (double)byte_count / (6.0 * 3.0 * 48000.0));
    CFRunLoopRemoveSource(CFRunLoopGetCurrent(), source, kCFRunLoopDefaultMode);
    CFRelease(source);
    (*capture_iface)->USBInterfaceClose(capture_iface);
    (*capture_iface)->Release(capture_iface);
    (*control)->USBInterfaceClose(control);
    (*control)->Release(control);
    return packet_count > 0 && submit_errors == 0 ? 0 : 1;
}
