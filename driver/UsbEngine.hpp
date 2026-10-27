#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/usb/IOUSBLib.h>

// USB device clock is fixed to 48 kHz in this initial driver.
class UsbEngine {
public:
    static constexpr unsigned InputChannels = 6;
    static constexpr unsigned OutputChannels = 8;
    static constexpr unsigned SampleRate = 48000;

    UsbEngine();
    ~UsbEngine();
    UsbEngine(const UsbEngine&) = delete;
    UsbEngine& operator=(const UsbEngine&) = delete;

    static bool Present();
    struct Stats {
        uint64_t inputPackets, outputPackets, inputFrames, outputFrames, errors;
    };
    Stats GetStats() const noexcept;
    bool Start();
    void Stop();
    void ReadInput(float* destination, size_t frameCount) noexcept;
    void WriteOutput(const float* source, size_t frameCount) noexcept;

private:
    template <unsigned Channels> struct Ring {
        static constexpr uint64_t Capacity = 32768;
        float samples[Capacity * Channels]{};
        std::atomic<uint64_t> write{0};
        std::atomic<uint64_t> read{0};
        void Push(const float* data, size_t frames) noexcept;
        void Pop(float* data, size_t frames) noexcept;
        void Clear() noexcept;
    };

    static constexpr unsigned Requests = 4;
    static constexpr unsigned Microframes = 8;
    struct Transfer {
        UInt8 data[Microframes * 312]{};
        IOUSBIsocFrame frames[Microframes]{};
        UsbEngine* owner = nullptr;
        unsigned index = 0;
    };

    void Worker();
    bool OpenDevice();
    void CloseDevice();
    bool SetClock();
    bool ConfigureMixer();
    bool SetupStream(IOUSBInterfaceInterface300 **interface, int alt,
                     UInt8 direction, UInt8* pipe, CFRunLoopSourceRef* source);
    bool SubmitInput(unsigned index);
    bool SubmitOutput(unsigned index);
    static void InputComplete(void* context, IOReturn result, void* argument);
    static void OutputComplete(void* context, IOReturn result, void* argument);

    Ring<InputChannels> input_;
    Ring<OutputChannels> output_;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable ready_cv_;
    bool ready_ = false;
    bool start_ok_ = false;
    std::atomic<bool> stop_{false};
    std::atomic<uint64_t> input_packets_{0}, output_packets_{0};
    std::atomic<uint64_t> input_frames_{0}, output_frames_{0}, errors_{0};
    CFRunLoopRef loop_ = nullptr;
    IOUSBInterfaceInterface300 **control_ = nullptr;
    IOUSBInterfaceInterface300 **input_iface_ = nullptr;
    IOUSBInterfaceInterface300 **output_iface_ = nullptr;
    UInt8 input_pipe_ = 0, output_pipe_ = 0;
    CFRunLoopSourceRef input_source_ = nullptr, output_source_ = nullptr;
    UInt64 next_input_frame_ = 0, next_output_frame_ = 0;
    Transfer input_transfers_[Requests], output_transfers_[Requests];
    UInt8 feedback_[4096]{};
    unsigned feedback_head_ = 0, feedback_tail_ = 0;
};
