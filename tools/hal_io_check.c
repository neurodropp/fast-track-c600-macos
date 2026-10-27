// Exercise the installed C600 device through Core Audio with silent output.
// Copyright (c) 2026 Fast Track C600 contributors. MIT License.
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static _Atomic unsigned long callbacks;
static _Atomic unsigned long input_bytes;
static _Atomic unsigned long output_bytes;

static OSStatus process(AudioDeviceID device, const AudioTimeStamp* now,
                        const AudioBufferList* input, const AudioTimeStamp* input_time,
                        AudioBufferList* output, const AudioTimeStamp* output_time,
                        void* context) {
    (void)device; (void)now; (void)input_time; (void)output_time; (void)context;
    atomic_fetch_add(&callbacks, 1);
    if (input) for (UInt32 n = 0; n < input->mNumberBuffers; ++n)
        atomic_fetch_add(&input_bytes, input->mBuffers[n].mDataByteSize);
    if (output) for (UInt32 n = 0; n < output->mNumberBuffers; ++n) {
        if (output->mBuffers[n].mData)
            memset(output->mBuffers[n].mData, 0, output->mBuffers[n].mDataByteSize);
        atomic_fetch_add(&output_bytes, output->mBuffers[n].mDataByteSize);
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

int main(void) {
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
    printf("Core Audio I/O: callbacks=%lu input_bytes=%lu output_bytes=%lu\n",
           atomic_load(&callbacks), atomic_load(&input_bytes), atomic_load(&output_bytes));
    return atomic_load(&callbacks) > 0 ? 0 : 1;
}
