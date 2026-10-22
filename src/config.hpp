#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace palim {

// Runtime-configurable knobs for `palim`'s main loop. Every field here
// was previously a hardcoded constant or literal in main.cpp, even
// though the classes that consume them (Camera, StateMachine,
// SnapshotWriter) already accepted these as constructor arguments --
// main.cpp just never exposed a way to set them without recompiling.
struct Config {
    std::string devicePath = "/dev/video0";
    int width = 1280;
    int height = 720;
    double changeThresholdPercent = 5.0;
    std::chrono::milliseconds stableDuration{2000};
    std::string outputDir = "commits";
};

// Parses palim's CLI flags into a Config, starting from the defaults
// above. Throws std::invalid_argument (with a human-readable message)
// on an unrecognized flag, a missing value, or a value that fails to
// parse as the expected type.
//
// Supported flags: --device <path>, --width <px>, --height <px>,
// --threshold <percent>, --stable-ms <ms>, --output-dir <path>.
Config parseArgs(const std::vector<std::string>& args);

}  // namespace palim
