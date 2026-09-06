#include "Theme.h"
#include "../MyColors.h"

namespace ThemeRegistry {

const Theme MonoDotMatrix = {
    "Mono Dot-Matrix",
    MY_MONO_BG,
    MY_MONO_FG,
    MY_MONO_FG_DIM,
    MY_MONO_RULE,
    MY_MONO_SELECT_BG,
    MY_MONO_SELECT_FG,
    MY_ORANGE,   // accent - unaffected by the panel's color quirk (see MyColors.h)
    MY_GREEN,    // accent2
    MY_RED,      // danger
    12,          // smallLineH
};

const Theme AmberTerminal = {
    "Amber Terminal",
    MY_AMBER_BG,
    MY_AMBER_FG,
    MY_AMBER_FG_DIM,
    MY_AMBER_RULE,
    MY_AMBER_FG,      // selectBg - fg fill...
    MY_AMBER_BG,      // selectFg - ...with bg-coloured (inverted) text
    MY_AMBER_ACCENT,
    MY_AMBER_ACCENT2,
    MY_AMBER_DANGER,
    12,
};

const Theme GreenPhosphor = {
    "Green Phosphor",
    MY_PHOS_BG,
    MY_PHOS_FG,
    MY_PHOS_FG_DIM,
    MY_PHOS_RULE,
    MY_PHOS_FG,
    MY_PHOS_BG,
    MY_PHOS_ACCENT,
    MY_PHOS_ACCENT2,
    MY_PHOS_DANGER,
    12,
};

const Theme IceBlue = {
    "Ice Blue",
    MY_ICE_BG,
    MY_ICE_FG,
    MY_ICE_FG_DIM,
    MY_ICE_RULE,
    MY_ICE_FG,
    MY_ICE_BG,
    MY_ICE_ACCENT,
    MY_ICE_ACCENT2,
    MY_ICE_DANGER,
    12,
};

// Table of every theme this build knows about, indexed by the same numeric
// id CaptivePortal persists to EEPROM. Index 0 must always be a safe
// default. Adding a theme later is just another row here plus one more
// Theme instance above - no drawing code changes.
static const Theme* const kThemes[] = {
    &MonoDotMatrix,
    &AmberTerminal,
    &GreenPhosphor,
    &IceBlue,
};
static const int kThemeCount = sizeof(kThemes) / sizeof(kThemes[0]);

const Theme& byId(uint8_t id) {
    if (id < kThemeCount) return *kThemes[id];
    return MonoDotMatrix;
}

int count() { return kThemeCount; }

} // namespace ThemeRegistry

const Theme* ThemeManager::_current = &ThemeRegistry::MonoDotMatrix;

void ThemeManager::begin(uint8_t persistedId) {
    _current = &ThemeRegistry::byId(persistedId);
}

const Theme& ThemeManager::current() {
    return *_current;
}

uint8_t ThemeManager::setThemeById(uint8_t id) {
    // Clamp to a real, persistable id rather than persisting whatever
    // out-of-range value was passed in - byId() would have silently fallen
    // back to the default theme anyway, so make the id match what's
    // actually now active.
    uint8_t applied = (id < ThemeRegistry::count()) ? id : 0;
    _current = &ThemeRegistry::byId(applied);
    return applied;
}
