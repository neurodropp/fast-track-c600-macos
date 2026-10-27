// Read-only USB and UAC2 probe for the M-Audio Fast Track C600.
// Copyright (c) 2026 Fast Track C600 contributors. MIT License.
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { VID = 0x0763, PID = 0x2031, AC_INTERFACE = 1 };

static void add_number(CFMutableDictionaryRef match, CFStringRef key, SInt32 value) {
    CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
    if (!n) { fprintf(stderr, "CFNumberCreate failed\n"); exit(1); }
    CFDictionarySetValue(match, key, n);
    CFRelease(n);
}

static io_service_t find_service(const char *class_name, int interface_number) {
    CFMutableDictionaryRef match = IOServiceMatching(class_name);
    if (!match) return IO_OBJECT_NULL;
    // USBHostInterface does not honor all matching keys on every macOS release.
    // Enumerate the small interface set and check the registry properties here.
    if (interface_number < 0) {
        add_number(match, CFSTR(kUSBVendorID), VID);
        add_number(match, CFSTR(kUSBProductID), PID);
    }
    io_iterator_t iter = IO_OBJECT_NULL;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, match, &iter) != KERN_SUCCESS)
        return IO_OBJECT_NULL;
    io_service_t service = IO_OBJECT_NULL;
    while ((service = IOIteratorNext(iter))) {
        if (interface_number < 0) break;
        const struct { CFStringRef key; int expected; } checks[] = {
            { CFSTR(kUSBVendorID), VID },
            { CFSTR(kUSBProductID), PID },
            { CFSTR(kUSBInterfaceNumber), interface_number },
        };
        int matches = 1;
        for (unsigned i = 0; i < sizeof(checks) / sizeof(checks[0]); ++i) {
            CFTypeRef property = IORegistryEntryCreateCFProperty(
                service, checks[i].key, kCFAllocatorDefault, 0);
            int value = -1;
            if (!property || CFGetTypeID(property) != CFNumberGetTypeID() ||
                !CFNumberGetValue(property, kCFNumberIntType, &value) ||
                value != checks[i].expected) matches = 0;
            if (property) CFRelease(property);
        }
        if (matches) break;
        IOObjectRelease(service);
    }
    IOObjectRelease(iter);
    return service;
}

static void dump_configuration(IOUSBDeviceInterface **device) {
    UInt8 count = 0;
    if ((*device)->GetNumberOfConfigurations(device, &count) != kIOReturnSuccess) {
        fprintf(stderr, "Cannot read USB configuration count\n"); return;
    }
    printf("USB configurations: %u\n", count);
    for (UInt8 c = 0; c < count; ++c) {
        IOUSBConfigurationDescriptorPtr config = NULL;
        if ((*device)->GetConfigurationDescriptorPtr(device, c, &config) != kIOReturnSuccess || !config)
            continue;
        const UInt8 *bytes = (const UInt8 *)config;
        UInt16 total = USBToHostWord(config->wTotalLength);
        printf("Configuration %u: %u bytes\n", c, total);
        UInt8 current_class = 0;
        for (UInt16 offset = 0; offset + 2 <= total;) {
            UInt8 length = bytes[offset], type = bytes[offset + 1];
            if (length < 2 || offset + length > total) {
                fprintf(stderr, "Malformed descriptor at offset %u\n", offset); break;
            }
            const UInt8 *d = bytes + offset;
            if (type == 4 && length >= 9) {
                current_class = d[5];
                printf("  Interface %u alt %u: class %02x subclass %02x protocol %02x, endpoints %u\n",
                       d[2], d[3], d[5], d[6], d[7], d[4]);
            }
            else if (type == 5 && length >= 7)
                printf("    Endpoint %02x: attributes %02x, max packet %u, interval %u\n",
                       d[2], d[3], (unsigned)(d[4] | (d[5] << 8)), d[6]);
            else if (type == 0x24 && current_class == 0xff &&
                     length >= 6 && d[2] == 2 && d[3] == 1)
                printf("    PCM format: %u bytes/sample, %u valid bits\n", d[4], d[5]);
            offset += length;
        }
    }
}

static void read_clock(void) {
    io_service_t service = find_service(kIOUSBInterfaceClassName, AC_INTERFACE);
    if (!service) { puts("Audio control interface not found"); return; }
    IOCFPlugInInterface **plugin = NULL;
    SInt32 score = 0;
    kern_return_t status = IOCreatePlugInInterfaceForService(
        service, kIOUSBInterfaceUserClientTypeID, kIOCFPlugInInterfaceID, &plugin, &score);
    IOObjectRelease(service);
    if (status != KERN_SUCCESS || !plugin) {
        printf("Audio control interface unavailable: 0x%08x\n", status); return;
    }
    IOUSBInterfaceInterface300 **iface = NULL;
    HRESULT query = (*plugin)->QueryInterface(
        plugin, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID300), (LPVOID *)&iface);
    (*plugin)->Release(plugin);
    if (query != S_OK || !iface) {
        printf("Audio control interface query failed: 0x%08x\n", (unsigned)query); return;
    }
    status = (*iface)->USBInterfaceOpen(iface);
    if (status != kIOReturnSuccess) {
        printf("Audio control interface open failed: 0x%08x\n", status);
        (*iface)->Release(iface); return;
    }
    // UAC2 GET_CUR: clock selector 0x80, control 1, interface 1.
    UInt8 source = 0;
    IOUSBDevRequestTO req = {0};
    req.bmRequestType = USBmakebmRequestType(kUSBIn, kUSBClass, kUSBInterface);
    req.bRequest = 0x01;
    req.wValue = 0x0100;
    req.wIndex = (0x80 << 8) | AC_INTERFACE;
    req.wLength = sizeof(source);
    req.pData = &source;
    req.noDataTimeout = 100;
    req.completionTimeout = 500;
    status = (*iface)->ControlRequestTO(iface, 0, &req);
    if (status == kIOReturnSuccess) {
        printf("Current clock selector pin: %u (%s)\n", source,
               source == 1 ? "internal" : source == 2 ? "S/PDIF" : "unknown");
        UInt32 hz = 0;
        req.wIndex = ((UInt16)(0x80 + source) << 8) | AC_INTERFACE;
        req.wLength = sizeof(hz);
        req.pData = &hz;
        status = (*iface)->ControlRequestTO(iface, 0, &req);
        if (status == kIOReturnSuccess) printf("Current sample rate: %u Hz\n", hz);
    }
    if (status != kIOReturnSuccess)
        printf("Clock read unavailable: 0x%08x\n", status);
    (*iface)->USBInterfaceClose(iface);
    (*iface)->Release(iface);
}

int main(void) {
    io_service_t service = find_service(kIOUSBDeviceClassName, -1);
    if (!service) { fprintf(stderr, "Fast Track C600 (0763:2031) not found\n"); return 1; }
    IOCFPlugInInterface **plugin = NULL;
    SInt32 score = 0;
    kern_return_t status = IOCreatePlugInInterfaceForService(
        service, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plugin, &score);
    IOObjectRelease(service);
    if (status != KERN_SUCCESS || !plugin) {
        fprintf(stderr, "Cannot access USB device: 0x%08x\n", status); return 1;
    }
    IOUSBDeviceInterface **device = NULL;
    HRESULT query = (*plugin)->QueryInterface(
        plugin, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID), (LPVOID *)&device);
    (*plugin)->Release(plugin);
    if (query != S_OK || !device) {
        fprintf(stderr, "Cannot query USB device: 0x%08x\n", (unsigned)query); return 1;
    }
    puts("M-Audio Fast Track C600 (0763:2031)");
    dump_configuration(device);
    (*device)->Release(device);
    read_clock();
    return 0;
}
