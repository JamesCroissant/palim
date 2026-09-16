#pragma once

#include <optional>

namespace palim {

// Snapshot of camera state at the moment a frame was captured. Every
// field besides width/height is optional because not every UVC device
// supports every control -- a missing value means "this camera doesn't
// expose this control", not an error.
struct CameraSettings {
    int width = 0;
    int height = 0;
    std::optional<int> exposureAuto;             // V4L2_CID_EXPOSURE_AUTO
    std::optional<int> exposureAbsolute;          // V4L2_CID_EXPOSURE_ABSOLUTE
    std::optional<int> gain;                      // V4L2_CID_GAIN
    std::optional<int> whiteBalanceAuto;          // V4L2_CID_AUTO_WHITE_BALANCE
    std::optional<int> whiteBalanceTemperature;   // V4L2_CID_WHITE_BALANCE_TEMPERATURE
};

}  // namespace palim
