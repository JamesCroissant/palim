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
    // How many recent frames FrameHistory keeps (~30fps * 3s of headroom
    // at the default), so a commit can pick the sharpest one from a time
    // window instead of whatever frame happened to be current the instant
    // the state machine fired. Also the one place a zero-or-negative
    // value would be a real bug, not just a bad setting: RingBuffer's
    // wraparound indexing divides by its capacity, so a capacity of 0
    // would be a modulo-by-zero on the very first frame -- caught here,
    // at the CLI boundary, rather than inside that general-purpose class.
    int frameHistoryCapacity = 90;
};

// Parses palim's CLI flags into a Config, starting from the defaults
// above. Throws std::invalid_argument (with a human-readable message)
// on an unrecognized flag, a missing value, a value that fails to parse
// as the expected type, or one that parses but is out of range
// (--width/--height/--frame-history-capacity <= 0, --stable-ms < 0,
// --threshold non-finite or negative -- see config.cpp's
// parseIntAtLeast/parseNonNegativeFiniteDouble).
//
// Supported flags: --device <path>, --width <px>, --height <px>,
// --threshold <percent>, --stable-ms <ms>, --output-dir <path>,
// --frame-history-capacity <frames>.
Config parseArgs(const std::vector<std::string>& args);

}  // namespace palim
