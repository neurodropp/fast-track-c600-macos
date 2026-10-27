// Copyright (c) 2026 Fast Track C600 contributors. MIT License.
#include "UsbEngine.hpp"

#include <aspl/Driver.hpp>
#include <CoreAudio/AudioServerPlugIn.h>
#include <memory>

namespace {

class C600Device final : public aspl::Device {
public:
    using aspl::Device::Device;
    UInt32 GetTransportType() const override { return kAudioDeviceTransportTypeUSB; }
    bool GetIsAlive() const override { return UsbEngine::Present(); }
};

class C600Handler final : public aspl::ControlRequestHandler,
                          public aspl::IORequestHandler {
public:
    OSStatus OnStartIO() override {
        return engine_.Start() ? kAudioHardwareNoError : kAudioHardwareUnspecifiedError;
    }

    void OnStopIO() override { engine_.Stop(); }

    void OnReadClientInput(const std::shared_ptr<aspl::Client>&,
                           const std::shared_ptr<aspl::Stream>&,
                           Float64, Float64, void* data, UInt32 bytes) override {
        engine_.ReadInput(static_cast<float*>(data),
                          bytes / (UsbEngine::InputChannels * sizeof(float)));
    }

    void OnWriteMixedOutput(const std::shared_ptr<aspl::Stream>&,
                            Float64, Float64, const void* data, UInt32 bytes) override {
        engine_.WriteOutput(static_cast<const float*>(data),
                            bytes / (UsbEngine::OutputChannels * sizeof(float)));
    }

private:
    UsbEngine engine_;
};

AudioStreamBasicDescription floatFormat(UInt32 channels) {
    AudioStreamBasicDescription format = {};
    format.mSampleRate = UsbEngine::SampleRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked |
                          kAudioFormatFlagsNativeEndian;
    format.mBytesPerPacket = channels * sizeof(float);
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = channels * sizeof(float);
    format.mChannelsPerFrame = channels;
    format.mBitsPerChannel = 32;
    return format;
}

std::shared_ptr<aspl::Driver> createDriver() {
    auto context = std::make_shared<aspl::Context>();
    aspl::DeviceParameters parameters;
    parameters.Name = "Fast Track C600";
    parameters.Manufacturer = "M-Audio";
    parameters.DeviceUID = "FastTrackC600:0763:2031";
    parameters.ModelUID = "FastTrackC600";
    parameters.SampleRate = UsbEngine::SampleRate;
    parameters.ChannelCount = UsbEngine::OutputChannels;
    parameters.Latency = 512;
    parameters.SafetyOffset = 256;
    parameters.ZeroTimeStampPeriod = 480;
    parameters.EnableMixing = true;

    auto device = std::make_shared<C600Device>(context, parameters);
    aspl::StreamParameters input;
    input.Direction = aspl::Direction::Input;
    input.Format = floatFormat(UsbEngine::InputChannels);
    device->AddStreamAsync(input);
    aspl::StreamParameters output;
    output.Direction = aspl::Direction::Output;
    output.Format = floatFormat(UsbEngine::OutputChannels);
    device->AddStreamAsync(output);

    auto handler = std::make_shared<C600Handler>();
    device->SetControlHandler(handler);
    device->SetIOHandler(handler);
    auto plugin = std::make_shared<aspl::Plugin>(context);
    plugin->AddDevice(device);
    return std::make_shared<aspl::Driver>(context, plugin);
}

} // namespace

extern "C" void* C600DriverEntryPoint(CFAllocatorRef, CFUUIDRef typeUUID) {
    if (!CFEqual(typeUUID, kAudioServerPlugInTypeUUID)) return nullptr;
    static std::shared_ptr<aspl::Driver> driver = createDriver();
    return driver->GetReference();
}
