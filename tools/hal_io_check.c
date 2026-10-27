// Exercise the installed C600 device through Core Audio with silence or a test tone.
// Copyright (c) 2026 Fast Track C600 contributors. MIT License.
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdatomic.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static _Atomic unsigned long callbacks;
static _Atomic unsigned long input_bytes;
static _Atomic unsigned long output_bytes;
static _Atomic unsigned long tone_samples;
static int tone_enabled;
static double tone_phase;

static OSStatus process(AudioDeviceID device, const AudioTimeStamp* now,
                        const AudioBufferList* input, const AudioTimeStamp* input_time,
                        AudioBufferList* output, const AudioTimeStamp* output_time,
                        void* context) {
    (void)device; (void)now; (void)input_time; (void)output_time; (void)context;
    atomic_fetch_add(&callbacks, 1);
    if (input) for (UInt32 n = 0; n < input->mNumberBuffers; ++n)
        atomic_fetch_add(&input_bytes, input->mBuffers[n].mDataByteSize);
    UInt32 channel_offset = 0;
    UInt32 tone_frames = 0;
    if (output) for (UInt32 n = 0; n < output->mNumberBuffers; ++n) {
        AudioBuffer* buffer = &output->mBuffers[n];
        if (buffer->mData) {
            memset(buffer->mData, 0, buffer->mDataByteSize);
            if (tone_enabled && buffer->mNumberChannels > 0 && channel_offset < 2) {
                float* samples = buffer->mData;
                UInt32 frames = buffer->mDataByteSize /
                                (buffer->mNumberChannels * sizeof(float));
                if (n == 0) tone_frames = frames;
                for (UInt32 frame = 0; frame < frames; ++frame) {
                    double phase = tone_phase + frame * (2.0 * 3.141592653589793 * 440.0 / 48000.0);
                    float tone = 0.1f * sinf((float)phase);
                    for (UInt32 channel = 0; channel < buffer->mNumberChannels; ++channel)
                        if (channel_offset + channel < 2) {
                            samples[frame * buffer->mNumberChannels + channel] = tone;
                            atomic_fetch_add(&tone_samples, 1);
                        }
                }
            }
        }
        channel_offset += buffer->mNumberChannels;
        atomic_fetch_add(&output_bytes, output->mBuffers[n].mDataByteSize);
    }
    if (tone_enabled) {
        tone_phase += tone_frames * (2.0 * 3.141592653589793 * 440.0 / 48000.0);
        while (tone_phase >= 2.0 * 3.141592653589793)
            tone_phase -= 2.0 * 3.141592653589793;
    }
    return noErr;
}

static AudioDeviceID find_device(void) {
    AudioObjectPropertyAddress address = {
        kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    UInt32 bytes = 0;
    OSStatus result = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,
                                                     &address, 0, NULL, &bytes);
    if (result != noErr) return kAudioObjectUnknown;
    AudioDeviceID* devices = malloc(bytes);
    if (!devices) return kAudioObjectUnknown;
    result = AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0,
                                        NULL, &bytes, devices);
    if (result != noErr) { free(devices); return kAudioObjectUnknown; }
    AudioDeviceID found = kAudioObjectUnknown;
    UInt32 count = bytes / sizeof(AudioDeviceID);
    address.mSelector = kAudioDevicePropertyDeviceUID;
    for (UInt32 n = 0; n < count; ++n) {
        CFStringRef uid = NULL;
        UInt32 size = sizeof(uid);
        if (AudioObjectGetPropertyData(devices[n], &address, 0, NULL, &size, &uid) == noErr && uid) {
            if (CFStringCompare(uid, CFSTR("FastTrackC600:0763:2031"), 0) == kCFCompareEqualTo) {
                found = devices[n];
                CFRelease(uid);
                break;
            }
            CFRelease(uid);
        }
    }
    free(devices);
    return found;
}

int main(int argc, char** argv) {
    if (argc == 2 && strcmp(argv[1], "--tone") == 0) tone_enabled = 1;
    else if (argc != 1) {
        fprintf(stderr, "Usage: %s [--tone]\n", argv[0]); return 2;
    }
    AudioDeviceID device = find_device();
    if (device == kAudioObjectUnknown) {
        fprintf(stderr, "Installed C600 Core Audio device not found\n"); return 1;
    }
    AudioDeviceIOProcID io = NULL;
    OSStatus result = AudioDeviceCreateIOProcID(device, process, NULL, &io);
    if (result != noErr) { fprintf(stderr, "CreateIOProcID: %d\n", (int)result); return 1; }
    result = AudioDeviceStart(device, io);
    if (result != noErr) {
        fprintf(stderr, "AudioDeviceStart: %d\n", (int)result);
        AudioDeviceDestroyIOProcID(device, io);
        return 1;
    }
    sleep(2);
    AudioDeviceStop(device, io);
    AudioDeviceDestroyIOProcID(device, io);
    printf("Core Audio I/O (%s): callbacks=%lu input_bytes=%lu output_bytes=%lu tone_samples=%lu\n",
           tone_enabled ? "440 Hz tone on outputs 1-2" : "silence",
           atomic_load(&callbacks), atomic_load(&input_bytes), atomic_load(&output_bytes),
           atomic_load(&tone_samples));
    return atomic_load(&callbacks) > 0 && (!tone_enabled || atomic_load(&tone_samples) > 0) ? 0 : 1;
}
