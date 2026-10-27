// Standalone transport test of the same USB engine used by the Core Audio plug-in.
// Copyright (c) 2026 Fast Track C600 contributors. MIT License.
#include "../driver/UsbEngine.hpp"
#include <chrono>
#include <cstdio>
#include <thread>

int main() {
    if (!UsbEngine::Present()) { std::fprintf(stderr, "C600 not found\n"); return 1; }
    UsbEngine engine;
    if (!engine.Start()) { std::fprintf(stderr, "USB engine failed to start\n"); return 1; }
    float silence[480 * UsbEngine::OutputChannels] = {};
    float input[480 * UsbEngine::InputChannels];
    for (unsigned n = 0; n < 200; ++n) {
        engine.WriteOutput(silence, 480);
        engine.ReadInput(input, 480);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    engine.Stop();
    auto stats = engine.GetStats();
    std::printf("input: %llu packets, %llu frames; output: %llu packets, %llu frames; errors: %llu\n",
                (unsigned long long)stats.inputPackets,
                (unsigned long long)stats.inputFrames,
                (unsigned long long)stats.outputPackets,
                (unsigned long long)stats.outputFrames,
                (unsigned long long)stats.errors);
    return stats.inputPackets > 10000 && stats.outputPackets > 10000 &&
           stats.errors == 0 ? 0 : 1;
}
