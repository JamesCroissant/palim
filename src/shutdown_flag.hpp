#pragma once

#include <atomic>
#include <csignal>

namespace palim {

// Lets main's capture/processing/storage pipeline shut down cleanly on
// SIGINT/SIGTERM (Ctrl-C, or a plain `kill`) instead of the default
// handler terminating the process immediately. Without this, a signal
// arriving while the storage thread is mid-SnapshotWriter::write()
// could leave a commits/NNN/ directory with some of
// before.jpg/after.jpg/metadata.json written and the rest missing --
// a corrupted commit, which is exactly what this tool exists to avoid
// ever producing.
//
// A single global flag, not a per-instance object: a process only ever
// has one "has shutdown been requested" answer, and the one place that
// sets it -- a signal handler -- can only call async-signal-safe
// functions. A relaxed atomic store/load is one of the few things
// guaranteed safe to do from inside one.
class ShutdownFlag {
public:
    static ShutdownFlag& instance() {
        static ShutdownFlag flag;
        return flag;
    }

    void request() { requested_.store(true, std::memory_order_relaxed); }
    bool requested() const { return requested_.load(std::memory_order_relaxed); }

    // For tests only, so one test raising a signal doesn't leave the
    // (process-global) flag set for whichever test runs next.
    void resetForTesting() { requested_.store(false, std::memory_order_relaxed); }

    ShutdownFlag(const ShutdownFlag&) = delete;
    ShutdownFlag& operator=(const ShutdownFlag&) = delete;

private:
    ShutdownFlag() = default;
    std::atomic<bool> requested_{false};
};

inline void handleShutdownSignal(int /*signum*/) { ShutdownFlag::instance().request(); }

// Installs handleShutdownSignal for SIGINT and SIGTERM. Call once from
// main(), before starting the capture/processing/storage threads.
inline void installShutdownSignalHandlers() {
    std::signal(SIGINT, handleShutdownSignal);
    std::signal(SIGTERM, handleShutdownSignal);
}

}  // namespace palim
