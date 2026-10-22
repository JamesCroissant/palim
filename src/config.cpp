#include "config.hpp"

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

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];

        if (arg == "--device") {
            config.devicePath = nextValue(arg, args, i);
        } else if (arg == "--width") {
            config.width = parseInt(arg, nextValue(arg, args, i));
        } else if (arg == "--height") {
            config.height = parseInt(arg, nextValue(arg, args, i));
        } else if (arg == "--threshold") {
            config.changeThresholdPercent = parseDouble(arg, nextValue(arg, args, i));
        } else if (arg == "--stable-ms") {
            config.stableDuration = std::chrono::milliseconds(parseInt(arg, nextValue(arg, args, i)));
        } else if (arg == "--output-dir") {
            config.outputDir = nextValue(arg, args, i);
        } else {
            throw std::invalid_argument("unrecognized argument: " + arg);
        }
    }

    return config;
}

}  // namespace palim
