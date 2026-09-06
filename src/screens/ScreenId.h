#pragma once
#include <stdint.h>

// Stable, persistable identity for every top-level screen.
//
// A screen used to be identified purely by its position in ScreenManager's
// screens[] vector. That meant three separate places had to agree on one
// ordering - main.cpp's addScreen() call order, HOME_SCREEN_NAMES[] in
// SettingsScreen.cpp, and the hardcoded count handed to
// PlaneTrackerScreen::setPagerInfo() - and they had already drifted:
// CaptivePortal.h documented the saved home-screen index as
// "0=Plane, 1=Weather, 2=Games, 3=Focus", a list written before the
// Calendar screen existed.
//
// Positions have to be free to move (the user can reorder and hide
// screens), so anything persisted refers to a screen by the id below
// instead of by index.
//
// RULES, because these values end up in the user's saved settings:
//   * never renumber or reuse an existing id, even if the screen is
//     deleted - a device upgrading from older firmware still has it stored
//   * give a new screen the next free id in the low range
//   * Settings is 255: it is pinned as the last stop in the cycle and can
//     never be hidden, and parking it at the top of the range keeps the
//     low numbers contiguous for real, user-orderable screens
enum class ScreenId : uint8_t {
    Plane    = 0,
    Weather  = 1,
    Games    = 2,
    Focus    = 3,
    Calendar = 4,

    Settings = 255,
};
