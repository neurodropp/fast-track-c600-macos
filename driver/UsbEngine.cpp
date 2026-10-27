// Copyright (c) 2026 Fast Track C600 contributors. MIT License.
#include "UsbEngine.hpp"

#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
constexpr int VendorID = 0x0763;
constexpr int ProductID = 0x2031;
constexpr int AudioControlInterface = 1;
constexpr int OutputInterface = 2;
constexpr int InputInterface = 3;
constexpr UInt16 MixerGain0dB = 0xfcfe;

bool propertyEquals(io_service_t service, CFStringRef key, int expected) {
    CFTypeRef property = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    int actual = -1;
    bool ok = property && CFGetTypeID(property) == CFNumberGetTypeID() &&
              CFNumberGetValue((CFNumberRef)property, kCFNumberIntType, &actual) &&
              actual == expected;
    if (property) CFRelease(property);
    return ok;
}

io_service_t findInterface(int number) {
    io_iterator_t iterator = IO_OBJECT_NULL;
    if (IOServiceGetMatchingServices(kIOMainPortDefault,
            IOServiceMatching(kIOUSBInterfaceClassName), &iterator) != KERN_SUCCESS) return IO_OBJECT_NULL;
    io_service_t service = IO_OBJECT_NULL;
    while ((service = IOIteratorNext(iterator))) {
        if (propertyEquals(service, CFSTR(kUSBVendorID), VendorID) &&
            propertyEquals(service, CFSTR(kUSBProductID), ProductID) &&
            propertyEquals(service, CFSTR(kUSBInterfaceNumber), number)) break;
        IOObjectRelease(service);
    }
    IOObjectRelease(iterator);
    return service;
}

IOUSBInterfaceInterface300 **interfaceFor(int number) {
    io_service_t service = findInterface(number);
    if (!service) return nullptr;
    IOCFPlugInInterface **plugin = nullptr;
    SInt32 score = 0;
    kern_return_t result = IOCreatePlugInInterfaceForService(
        service, kIOUSBInterfaceUserClientTypeID, kIOCFPlugInInterfaceID,
        &plugin, &score);
    IOObjectRelease(service);
    if (result != KERN_SUCCESS || !plugin) return nullptr;
    IOUSBInterfaceInterface300 **iface = nullptr;
    HRESULT query = (*plugin)->QueryInterface(
        plugin, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID300), (LPVOID *)&iface);
    (*plugin)->Release(plugin);
    return query == S_OK ? iface : nullptr;
}

IOReturn controlRequest(IOUSBInterfaceInterface300 **iface,
                        UInt8 direction, UInt16 value, UInt16 index,
                        void* data, UInt16 length) {
    IOUSBDevRequestTO request = {};
    request.bmRequestType = USBmakebmRequestType(direction, kUSBClass, kUSBInterface);
    request.bRequest = 0x01; // UAC2 CUR
    request.wValue = value;
    request.wIndex = index;
    request.wLength = length;
    request.pData = data;
    request.noDataTimeout = 100;
    request.completionTimeout = 500;
    return (*iface)->ControlRequestTO(iface, 0, &request);
}

bool scheduleError(IOReturn result) {
    return result == (IOReturn)0xe00002ed || result == (IOReturn)0xe00002ee;
}

int32_t unpack24(const UInt8* p) {
    int32_t value = int32_t(p[0]) | (int32_t(p[1]) << 8) | (int32_t(p[2]) << 16);
    if (value & 0x800000) value |= ~0xffffff;
    return value;
}

void pack24(UInt8* p, float sample) {
    float clipped = std::max(-1.0f, std::min(0.99999988f, sample));
    int32_t value = (int32_t)std::lrintf(clipped * 8388608.0f);
    p[0] = (UInt8)value;
    p[1] = (UInt8)(value >> 8);
    p[2] = (UInt8)(value >> 16);
}
} // namespace

template <unsigned Channels>
void UsbEngine::Ring<Channels>::Push(const float* data, size_t frames) noexcept {
    uint64_t head = write.load(std::memory_order_relaxed);
    uint64_t tail = read.load(std::memory_order_acquire);
    for (size_t n = 0; n < frames; ++n) {
        if (head - tail >= Capacity) break;
        std::memcpy(&samples[(head % Capacity) * Channels],
                    data + n * Channels, Channels * sizeof(float));
        ++head;
    }
    write.store(head, std::memory_order_release);
}

template <unsigned Channels>
void UsbEngine::Ring<Channels>::Pop(float* data, size_t frames) noexcept {
    uint64_t tail = read.load(std::memory_order_relaxed);
    uint64_t head = write.load(std::memory_order_acquire);
    for (size_t n = 0; n < frames; ++n) {
        float* dst = data + n * Channels;
        if (tail < head) {
            std::memcpy(dst, &samples[(tail % Capacity) * Channels],
                        Channels * sizeof(float));
            ++tail;
        } else std::memset(dst, 0, Channels * sizeof(float));
    }
    read.store(tail, std::memory_order_release);
}

template <unsigned Channels>
void UsbEngine::Ring<Channels>::Clear() noexcept {
    write.store(0, std::memory_order_relaxed);
    read.store(0, std::memory_order_relaxed);
}

UsbEngine::UsbEngine() = default;
UsbEngine::~UsbEngine() { Stop(); }

bool UsbEngine::Present() {
    io_service_t service = findInterface(AudioControlInterface);
    if (!service) return false;
    IOObjectRelease(service);
    return true;
}

UsbEngine::Stats UsbEngine::GetStats() const noexcept {
    return { input_packets_.load(), output_packets_.load(),
             input_frames_.load(), output_frames_.load(), errors_.load() };
}

bool UsbEngine::Start() {
    Stop();
    input_.Clear();
    output_.Clear();
    input_packets_.store(0);
    output_packets_.store(0);
    input_frames_.store(0);
    output_frames_.store(0);
    errors_.store(0);
    stop_.store(false);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ready_ = false;
        start_ok_ = false;
    }
    worker_ = std::thread(&UsbEngine::Worker, this);
    std::unique_lock<std::mutex> lock(mutex_);
    bool notified = ready_cv_.wait_for(lock, std::chrono::seconds(5),
                                       [this] { return ready_; });
    bool success = notified && start_ok_;
    lock.unlock();
    if (!success) Stop();
    return success;
}

void UsbEngine::Stop() {
    stop_.store(true);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (loop_) { CFRunLoopStop(loop_); CFRunLoopWakeUp(loop_); }
    }
    if (worker_.joinable()) worker_.join();
}

void UsbEngine::ReadInput(float* destination, size_t frames) noexcept {
    input_.Pop(destination, frames);
}

void UsbEngine::WriteOutput(const float* source, size_t frames) noexcept {
    output_.Push(source, frames);
}

bool UsbEngine::SetClock() {
    UInt8 internalPin = 1;
    IOReturn result = controlRequest(control_, kUSBOut, 0x0100,
                                     (0x80 << 8) | AudioControlInterface,
                                     &internalPin, sizeof(internalPin));
    if (result != kIOReturnSuccess) return false;
    UInt32 rate = SampleRate;
    result = controlRequest(control_, kUSBOut, 0x0100,
                            (0x81 << 8) | AudioControlInterface,
                            &rate, sizeof(rate));
    return result == kIOReturnSuccess;
}

bool UsbEngine::ConfigureMixer() {
    // PCM channel n -> output n, at 0 dB. Other routes are left untouched.
    for (unsigned channel = 0; channel < OutputChannels; ++channel) {
        UInt16 gain = MixerGain0dB;
        UInt16 selector = (UInt16)(0x0100 + channel * OutputChannels + channel + 1);
        IOReturn result = controlRequest(control_, kUSBOut, selector,
                                         (0x40 << 8) | AudioControlInterface,
                                         &gain, sizeof(gain));
        if (result != kIOReturnSuccess) return false;
    }
    return true;
}

bool UsbEngine::SetupStream(IOUSBInterfaceInterface300 **iface, int alt,
                            UInt8 direction, UInt8* pipe, CFRunLoopSourceRef* source) {
    if (!iface || (*iface)->USBInterfaceOpen(iface) != kIOReturnSuccess) return false;
    if ((*iface)->SetAlternateInterface(iface, alt) != kIOReturnSuccess) return false;
    UInt8 count = 0;
    if ((*iface)->GetNumEndpoints(iface, &count) != kIOReturnSuccess) return false;
    for (UInt8 candidate = 1; candidate <= count; ++candidate) {
        UInt8 gotDirection, number, type, interval;
        UInt16 maxPacket;
        if ((*iface)->GetPipeProperties(iface, candidate, &gotDirection,
                &number, &type, &maxPacket, &interval) == kIOReturnSuccess &&
            gotDirection == direction && type == kUSBIsoc && number == 1)
            *pipe = candidate;
    }
    if (!*pipe) return false;
    if ((*iface)->CreateInterfaceAsyncEventSource(iface, source) != kIOReturnSuccess || !*source)
        return false;
    CFRunLoopAddSource(loop_, *source, kCFRunLoopDefaultMode);
    return true;
}

bool UsbEngine::OpenDevice() {
    control_ = interfaceFor(AudioControlInterface);
    input_iface_ = interfaceFor(InputInterface);
    output_iface_ = interfaceFor(OutputInterface);
    if (!control_ || !input_iface_ || !output_iface_) return false;
    if ((*control_)->USBInterfaceOpen(control_) != kIOReturnSuccess) return false;
    if (!SetClock() || !ConfigureMixer()) return false;
    if (!SetupStream(input_iface_, 1, kUSBIn, &input_pipe_, &input_source_)) return false;
    if (!SetupStream(output_iface_, 1, kUSBOut, &output_pipe_, &output_source_)) return false;
    UInt64 frame = 0;
    AbsoluteTime timestamp;
    if ((*input_iface_)->GetBusFrameNumber(input_iface_, &frame, &timestamp) != kIOReturnSuccess)
        return false;
    next_input_frame_ = frame + 6;
    next_output_frame_ = frame + 8;
    for (unsigned n = 0; n < Requests; ++n) {
        input_transfers_[n].owner = this;
        input_transfers_[n].index = n;
        output_transfers_[n].owner = this;
        output_transfers_[n].index = n;
    }
    for (unsigned n = 0; n < Requests; ++n) if (!SubmitInput(n)) return false;
    for (unsigned n = 0; n < Requests; ++n) if (!SubmitOutput(n)) return false;
    return true;
}

void UsbEngine::CloseDevice() {
    if (loop_ && input_source_) {
        CFRunLoopRemoveSource(loop_, input_source_, kCFRunLoopDefaultMode);
        CFRelease(input_source_);
        input_source_ = nullptr;
    }
    if (loop_ && output_source_) {
        CFRunLoopRemoveSource(loop_, output_source_, kCFRunLoopDefaultMode);
        CFRelease(output_source_);
        output_source_ = nullptr;
    }
    auto close = [](IOUSBInterfaceInterface300 ***slot) {
        if (!*slot) return;
        (**slot)->USBInterfaceClose(*slot);
        (**slot)->Release(*slot);
        *slot = nullptr;
    };
    close(&output_iface_);
    close(&input_iface_);
    close(&control_);
    input_pipe_ = output_pipe_ = 0;
}

bool UsbEngine::SubmitInput(unsigned index) {
    Transfer& transfer = input_transfers_[index];
    for (auto& frame : transfer.frames) {
        frame.frStatus = 0;
        frame.frReqCount = 234;
        frame.frActCount = 0;
    }
    IOReturn result = (*input_iface_)->ReadIsochPipeAsync(input_iface_, input_pipe_,
        transfer.data, next_input_frame_, Microframes, transfer.frames,
        InputComplete, &transfer);
    if (scheduleError(result)) {
        UInt64 now = 0;
        AbsoluteTime timestamp;
        if ((*input_iface_)->GetBusFrameNumber(input_iface_, &now, &timestamp) == kIOReturnSuccess) {
            next_input_frame_ = now + 4;
            result = (*input_iface_)->ReadIsochPipeAsync(input_iface_, input_pipe_,
                transfer.data, next_input_frame_, Microframes, transfer.frames,
                InputComplete, &transfer);
        }
    }
    if (result != kIOReturnSuccess) {
        errors_.fetch_add(1);
        stop_.store(true); return false;
    }
    ++next_input_frame_;
    return true;
}

bool UsbEngine::SubmitOutput(unsigned index) {
    Transfer& transfer = output_transfers_[index];
    size_t offset = 0;
    for (auto& frame : transfer.frames) {
        unsigned samples = 6;
        if (feedback_head_ != feedback_tail_) {
            samples = feedback_[feedback_tail_];
            feedback_tail_ = (feedback_tail_ + 1) % 4096;
        }
        samples = std::min(samples, 13u);
        float pcm[13 * OutputChannels];
        output_.Pop(pcm, samples);
        for (unsigned n = 0; n < samples * OutputChannels; ++n)
            pack24(transfer.data + offset + n * 3, pcm[n]);
        frame.frStatus = 0;
        frame.frReqCount = (UInt16)(samples * OutputChannels * 3);
        frame.frActCount = 0;
        offset += frame.frReqCount;
    }
    IOReturn result = (*output_iface_)->WriteIsochPipeAsync(output_iface_, output_pipe_,
        transfer.data, next_output_frame_, Microframes, transfer.frames,
        OutputComplete, &transfer);
    if (scheduleError(result)) {
        UInt64 now = 0;
        AbsoluteTime timestamp;
        if ((*output_iface_)->GetBusFrameNumber(output_iface_, &now, &timestamp) == kIOReturnSuccess) {
            next_output_frame_ = now + 4;
            result = (*output_iface_)->WriteIsochPipeAsync(output_iface_, output_pipe_,
                transfer.data, next_output_frame_, Microframes, transfer.frames,
                OutputComplete, &transfer);
        }
    }
    if (result != kIOReturnSuccess) {
        errors_.fetch_add(1);
        stop_.store(true); return false;
    }
    ++next_output_frame_;
    return true;
}

void UsbEngine::InputComplete(void* context, IOReturn result, void* argument) {
    (void)argument;
    Transfer* transfer = static_cast<Transfer*>(context);
    UsbEngine* engine = transfer->owner;
    if (result != kIOReturnSuccess) {
        engine->errors_.fetch_add(1);
        engine->stop_.store(true);
    }
    for (unsigned microframe = 0; microframe < Microframes; ++microframe) {
        const IOUSBIsocFrame& frame = transfer->frames[microframe];
        if (frame.frStatus != kIOReturnSuccess && frame.frStatus != kIOReturnUnderrun) {
            engine->errors_.fetch_add(1);
            continue;
        }
        unsigned count = std::min((unsigned)(frame.frActCount / (InputChannels * 3)), 13u);
        engine->input_packets_.fetch_add(1);
        engine->input_frames_.fetch_add(count);
        unsigned next = (engine->feedback_head_ + 1) % 4096;
        if (next != engine->feedback_tail_) {
            engine->feedback_[engine->feedback_head_] = (UInt8)count;
            engine->feedback_head_ = next;
        }
        float pcm[13 * InputChannels];
        const UInt8* bytes = transfer->data + microframe * 234;
        for (unsigned n = 0; n < count * InputChannels; ++n)
            pcm[n] = (float)unpack24(bytes + n * 3) / 8388608.0f;
        engine->input_.Push(pcm, count);
    }
    if (!engine->stop_.load()) engine->SubmitInput(transfer->index);
}

void UsbEngine::OutputComplete(void* context, IOReturn result, void* argument) {
    (void)argument;
    Transfer* transfer = static_cast<Transfer*>(context);
    UsbEngine* engine = transfer->owner;
    if (result != kIOReturnSuccess) {
        engine->errors_.fetch_add(1);
        engine->stop_.store(true);
    }
    for (auto& frame : transfer->frames) {
        if (frame.frStatus == kIOReturnSuccess || frame.frStatus == kIOReturnUnderrun) {
            engine->output_packets_.fetch_add(1);
            engine->output_frames_.fetch_add(frame.frActCount / (OutputChannels * 3));
        } else engine->errors_.fetch_add(1);
    }
    if (!engine->stop_.load()) engine->SubmitOutput(transfer->index);
}

void UsbEngine::Worker() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = CFRunLoopGetCurrent();
    }
    bool opened = OpenDevice();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        start_ok_ = opened;
        ready_ = true;
    }
    ready_cv_.notify_all();
    while (opened && !stop_.load())
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);
    stop_.store(true);
    if (opened) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
    CloseDevice();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = nullptr;
    }
}
