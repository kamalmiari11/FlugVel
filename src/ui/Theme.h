#pragma once
#include <TFT_eSPI.h>

// A pure-data palette + a few layout constants that every screen's drawing
// code reads through instead of referencing raw color constants directly.
// Add more Theme instances to ThemeRegistry (see Theme.cpp) to support
// switchable themes later (e.g. a CRT-green or amber variant) without
// touching any screen's drawing logic - just data.
struct Theme {
    const char* name;

    uint16_t bg;         // screen background
    uint16_t fg;         // primary text/lines
    uint16_t fgDim;       // secondary/dim text
    uint16_t rule;        // hairline rule color
    uint16_t selectBg;    // selected-row fill
    uint16_t selectFg;    // selected-row (inverted) text
    uint16_t accent;      // warnings / "today" / needs-attention highlight
    uint16_t accent2;     // positive/good (e.g. best score, up-to-date)
    uint16_t danger;      // errors / cancel / game-over

    // Small layout constants, kept here so future themes can tweak spacing
    // too, not just color.
    int smallLineH;       // line height for size-1 text rows (legend, hints)
};

namespace ThemeRegistry {
    extern const Theme MonoDotMatrix;
    extern const Theme AmberTerminal;
    extern const Theme GreenPhosphor;
    extern const Theme IceBlue;

    // Looks up a theme by its persisted numeric id (see CaptivePortal's
    // theme storage). Falls back to MonoDotMatrix for any id it doesn't
    // recognize - keeps this forward-compatible with EEPROM values written
    // by a firmware that knows about more themes than this build does.
    const Theme& byId(uint8_t id);

    // How many themes this build knows about (table size in Theme.cpp).
    int count();
}

// Holds "the current theme" and hands it out to drawing code. No screen
// should reference a raw color constant directly - always go through
// Theme::current()'s semantic roles instead.
class ThemeManager {
public:
    // Call once at boot, with the theme id loaded from persistent storage
    // (see CaptivePortal::getThemeId()). Safe to call with any value -
    // unrecognized ids fall back to the default theme.
    static void begin(uint8_t persistedId);

    static const Theme& current();

    // Changes the active theme immediately (drawing code reads this live)
    // and returns its numeric id, for the caller to persist. Not currently
    // wired to any Settings UI - see Theme.cpp/CaptivePortal for the
    // storage path this is ready to plug into.
    static uint8_t setThemeById(uint8_t id);

private:
    static const Theme* _current;
};
