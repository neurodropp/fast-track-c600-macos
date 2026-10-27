// Load the driver bundle without installing it into Core Audio.
// Copyright (c) 2026 Fast Track C600 contributors. MIT License.
#include <CoreAudio/AudioServerPlugIn.h>
#include <dlfcn.h>
#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "Usage: %s <driver binary>\n", argv[0]); return 2; }
    void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { std::fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    using Entry = void* (*)(CFAllocatorRef, CFUUIDRef);
    auto entry = reinterpret_cast<Entry>(dlsym(library, "C600DriverEntryPoint"));
    if (!entry) { std::fprintf(stderr, "entry point unavailable\n"); return 1; }
    void* driver = entry(kCFAllocatorDefault, kAudioServerPlugInTypeUUID);
    if (!driver) { std::fprintf(stderr, "entry point returned null\n"); return 1; }
    std::puts("Core Audio driver entry point loaded successfully");
    return 0;
}
