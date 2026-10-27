#pragma once

namespace palim {

// Tracks consecutive failures of some operation and reports when a
// caller should give up rather than keep retrying forever. One failed
// attempt is normal and expected to recover on its own (main.cpp's
// capture thread uses this around Camera::grab(), where a single
// dropped frame is unremarkable); recordFailure() returning true means
// enough of them have happened in a row, with no success in between,
// that the condition is almost certainly permanent (a disconnected
// camera, a stuck driver) rather than a transient blip.
class ConsecutiveFailureGuard {
public:
    explicit ConsecutiveFailureGuard(int maxConsecutiveFailures) : max_(maxConsecutiveFailures) {}

    void recordSuccess() { count_ = 0; }

    // Returns true once this many consecutive failures have been
    // recorded without an intervening recordSuccess() call.
    bool recordFailure() { return ++count_ >= max_; }

private:
    int max_;
    int count_ = 0;
};

}  // namespace palim
