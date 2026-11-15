#pragma once

namespace palim {

// Whether a GUI display session is available for cv::namedWindow()/
// cv::imshow() (main.cpp's preview window) to actually open a window
// on. Takes the two relevant environment variables' values as plain
// arguments (rather than calling std::getenv() itself) specifically so
// this decision -- which is pure string logic -- can be unit-tested
// without actually manipulating the process environment.
//
// Checking this *before* ever calling cv::namedWindow() matters because
// OpenCV's Qt/GTK highgui backend doesn't fail that call with a
// catchable C++ exception when no display is reachable: it aborts the
// whole process via Qt's qFatal, which calls abort() -- a signal, not a
// throw, so no amount of try/catch around main()'s own logic could ever
// have caught it. That's worse here than it would be for a program with
// no cleanup to lose: an abort() this abrupt bypasses the
// capture/processing/storage threads' own clean-shutdown path entirely
// (ShutdownFlag, joining all three threads) -- the exact "half-written
// commit" failure mode Phase 10 exists to prevent, just reached through
// the one path that was never guarded against it. Running palim from a
// systemd unit with no desktop session attached, or over a plain SSH
// session without `-X`, with a real camera plugged in (so the existing
// Camera::isOpened() check doesn't stop it first), hits exactly this.
inline bool displayAvailable(const char* displayEnv, const char* waylandDisplayEnv) {
    const bool hasDisplay = displayEnv != nullptr && displayEnv[0] != '\0';
    const bool hasWaylandDisplay = waylandDisplayEnv != nullptr && waylandDisplayEnv[0] != '\0';
    return hasDisplay || hasWaylandDisplay;
}

}  // namespace palim
