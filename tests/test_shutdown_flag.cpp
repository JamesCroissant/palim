#include <gtest/gtest.h>

#include <csignal>

#include "shutdown_flag.hpp"

namespace {

TEST(ShutdownFlag, StartsUnsetAfterReset) {
    palim::ShutdownFlag::instance().resetForTesting();
    EXPECT_FALSE(palim::ShutdownFlag::instance().requested());
}

TEST(ShutdownFlag, RequestSetsIt) {
    palim::ShutdownFlag::instance().resetForTesting();
    palim::ShutdownFlag::instance().request();
    EXPECT_TRUE(palim::ShutdownFlag::instance().requested());
}

// The real thing main() relies on: that installing the handler and then
// actually receiving SIGINT/SIGTERM (not just calling request() directly)
// sets the flag. raise() delivers the signal synchronously on this
// thread, so by the time it returns, the handler has already run.
TEST(ShutdownFlag, SigintSetsItThroughTheRealHandler) {
    palim::ShutdownFlag::instance().resetForTesting();
    palim::installShutdownSignalHandlers();

    raise(SIGINT);

    EXPECT_TRUE(palim::ShutdownFlag::instance().requested());
}

TEST(ShutdownFlag, SigtermSetsItThroughTheRealHandler) {
    palim::ShutdownFlag::instance().resetForTesting();
    palim::installShutdownSignalHandlers();

    raise(SIGTERM);

    EXPECT_TRUE(palim::ShutdownFlag::instance().requested());
}

}  // namespace
