#pragma once
#include "ScreenId.h"
#include <TFT_eSPI.h>

class Screen;
class CaptivePortal;

// One table describing every top-level screen this build knows about: its
// stable id, the name shown in the header and in menus, and how to build
// one. Adding a screen is a new ScreenId plus one row in kScreens
// (ScreenRegistry.cpp) - nothing in main.cpp, SettingsScreen or
// ScreenManager needs a matching edit, which is exactly the duplication
// this replaces.
//
// The factory takes both a display and the portal even though most screens
// only want the display: a uniform signature is what lets the table be
// walked in a loop, and a factory simply ignores the argument it does not
// need. Screens are still constructed eagerly today; the indirection is
// here so construction can later be skipped entirely for a screen the user
// has hidden.
struct ScreenDef {
    ScreenId    id;
    const char* name;
    Screen*   (*make)(TFT_eSPI* tft, CaptivePortal* portal);
};

namespace ScreenRegistry {

    // The whole table, in the order screens are added at boot. Settings is
    // last, and ScreenManager relies on that only through isPinned().
    const ScreenDef* all();
    int count();

    // Lookup by stable id. Returns nullptr for an id this build does not
    // know - which happens on a firmware downgrade, where saved settings
    // may still name a screen that no longer exists.
    const ScreenDef* byId(ScreenId id);

    // Screens the user may choose as their home screen, i.e. everything
    // except the pinned Settings entry. selectable(i) walks that subset in
    // table order; it returns nullptr if i is out of range.
    const ScreenDef* selectable(int index);
    int selectableCount();

    // True for a screen that must always be present and always sits last
    // in the cycle. Settings is the only one: it is the sole route back
    // out to configuration, so hiding it would strand the user.
    bool isPinned(ScreenId id);

} // namespace ScreenRegistry
