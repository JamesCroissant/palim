#include "config.hpp"

#include <cmath>
#include <stdexcept>

namespace palim {

namespace {

int parseInt(const std::string& flag, const std::string& value) {
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(value, &consumed);
        if (consumed != value.size()) {
            throw std::invalid_argument("");
        }
        return parsed;
    } catch (const std::exception&) {
        throw std::invalid_argument(flag + " expects an integer, got '" + value + "'");
    }
}

// Like parseInt, but rejects anything below `min` -- a negative or zero
// value is nonsense for a pixel dimension or a millisecond duration, and
// a negative width/height specifically would otherwise get cast to a
// huge V4L2_FMT __u32 (Camera::setFormat) rather than fail loudly here.
int parseIntAtLeast(const std::string& flag, const std::string& value, int min) {
    const int parsed = parseInt(flag, value);
    if (parsed < min) {
        throw std::invalid_argument(flag + " must be >= " + std::to_string(min) + ", got '" + value + "'");
    }
    return parsed;
}

// Like parseInt, but rejects anything outside [min, max] -- used for
// --pixel-diff-threshold, where both ends matter: negative is nonsense
// for an absolute pixel-intensity difference, and 255 or above could
// never be exceeded by an 8-bit diff (cv::threshold's src > thresh would
// never hold), silently disabling change detection instead of failing
// loudly.
int parseIntInRange(const std::string& flag, const std::string& value, int min, int max) {
    const int parsed = parseInt(flag, value);
    if (parsed < min || parsed > max) {
        throw std::invalid_argument(flag + " must be between " + std::to_string(min) + " and " +
                                     std::to_string(max) + ", got '" + value + "'");
    }
    return parsed;
}

// cv::GaussianBlur requires its kernel size to be a positive odd integer
// (or exactly 0, meaning "derive it from sigma" -- not an option here,
// since ChangeDetector always passes sigma 0 too). Rejected here rather
// than left to surface as an uncaught cv::Exception assertion failure
// out of ChangeDetector::preprocess the first time a frame is processed.
int parsePositiveOddInt(const std::string& flag, const std::string& value) {
    const int parsed = parseIntAtLeast(flag, value, 1);
    if (parsed % 2 == 0) {
        throw std::invalid_argument(flag + " must be odd, got '" + value + "'");
    }
    return parsed;
}

double parseDouble(const std::string& flag, const std::string& value) {
    try {
        std::size_t consumed = 0;
        const double parsed = std::stod(value, &consumed);
        if (consumed != value.size()) {
            throw std::invalid_argument("");
        }
        return parsed;
    } catch (const std::exception&) {
        throw std::invalid_argument(flag + " expects a number, got '" + value + "'");
    }
}

// std::stod parses "nan"/"inf"/"-inf" as a clean, no-exception success --
// none of them mean anything as a change-percentage threshold.
double parseNonNegativeFiniteDouble(const std::string& flag, const std::string& value) {
    const double parsed = parseDouble(flag, value);
    if (!std::isfinite(parsed) || parsed < 0.0) {
        throw std::invalid_argument(flag + " must be a finite number >= 0, got '" + value + "'");
    }
    return parsed;
}

// Returns the next argument and advances i past it, or throws if this
// flag was the last argument.
const std::string& nextValue(const std::string& flag, const std::vector<std::string>& args, std::size_t& i) {
    if (i + 1 >= args.size()) {
        throw std::invalid_argument(flag + " requires a value");
    }
    return args[++i];
}

}  // namespace

Config parseArgs(const std::vector<std::string>& args) {
    Config config;

    for (const std::string& arg : args) {
        if (arg == "--help" || arg == "-h") {
            config.helpRequested = true;
            return config;
        }
    }

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];

        if (arg == "--device") {
            config.devicePath = nextValue(arg, args, i);
        } else if (arg == "--width") {
            config.width = parseIntAtLeast(arg, nextValue(arg, args, i), 1);
        } else if (arg == "--height") {
            config.height = parseIntAtLeast(arg, nextValue(arg, args, i), 1);
        } else if (arg == "--threshold") {
            config.changeThresholdPercent = parseNonNegativeFiniteDouble(arg, nextValue(arg, args, i));
        } else if (arg == "--stable-ms") {
            config.stableDuration =
                std::chrono::milliseconds(parseIntAtLeast(arg, nextValue(arg, args, i), 0));
        } else if (arg == "--output-dir") {
            config.outputDir = nextValue(arg, args, i);
        } else if (arg == "--frame-history-capacity") {
            config.frameHistoryCapacity = parseIntAtLeast(arg, nextValue(arg, args, i), 1);
        } else if (arg == "--max-commits") {
            config.maxCommits = parseIntAtLeast(arg, nextValue(arg, args, i), 1);
        } else if (arg == "--pixel-diff-threshold") {
            config.pixelDiffThreshold = parseIntInRange(arg, nextValue(arg, args, i), 0, 254);
        } else if (arg == "--blur-kernel-size") {
            config.blurKernelSize = parsePositiveOddInt(arg, nextValue(arg, args, i));
        } else {
            throw std::invalid_argument("unrecognized argument: " + arg);
        }
    }

    return config;
}

}  // namespace palim
