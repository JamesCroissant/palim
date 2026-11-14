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
    // Set when the command line contained --help/-h. parseArgs() checks
    // for this before anything else, so it wins over an otherwise-
    // invalid flag elsewhere in argv rather than first throwing on that
    // -- the same priority --help gets in every other command-line
    // tool. Every other field is left at its default when this is set;
    // main.cpp checks this before acting on any of them.
    bool helpRequested = false;
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
    // Caps how many commits/NNN directories SnapshotWriter ever keeps at
    // once -- see its own comment for why. 0 (the default) means
    // unlimited, matching this project's behavior before this existed.
    int maxCommits = 0;
};

// Parses palim's CLI flags into a Config, starting from the defaults
// above. Throws std::invalid_argument (with a human-readable message)
// on an unrecognized flag, a missing value, a value that fails to parse
// as the expected type, or one that parses but is out of range
// (--width/--height/--frame-history-capacity/--max-commits <= 0,
// --stable-ms < 0, --threshold non-finite or negative -- see
// config.cpp's parseIntAtLeast/parseNonNegativeFiniteDouble).
//
// Supported flags: --device <path>, --width <px>, --height <px>,
// --threshold <percent>, --stable-ms <ms>, --output-dir <path>,
// --frame-history-capacity <frames>, --max-commits <count>, --help/-h.
//
// --help/-h anywhere in `args` short-circuits all of the above: returns
// immediately with Config::helpRequested == true, without validating
// (or even looking at) anything else in `args` and without ever
// throwing, regardless of what else is present.
Config parseArgs(const std::vector<std::string>& args);

}  // namespace palim
