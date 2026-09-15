#include "Theme.h"
#include "../MyColors.h"

namespace ThemeRegistry {

// The FlugVel brand set: sage + olive + orange (shared with the config page
// and emails), and black + light blue from the logo. Colours were chosen by
// measured contrast, one job per colour:
//   fg/bg >= 10:1, fgDim/bg >= 5:1 (a small panel read at an angle),
//   orange = "now / needs attention", selectMark = where you are,
//   accent2/danger = up/down only (always paired with +/- so it never
//   relies on red vs green alone).
// Ratios below are sRGB; re-check on the physical panel, which shifts some.

// Meadow and Forest below are PANEL-COMPENSATED: this clone panel drains
// mid-tone sage to gray and crushes dark olive to charcoal, so the values
// sent are greener/lighter than the look they produce. Picked by eye from
// the calibration swatches (Meadow = sage #8, Forest = olive #6), then the
// other roles re-derived around them. The portal preview deliberately keeps
// the intended look (#c8d0b8 / #1a1c17), since a browser doesn't drain.

// Meadow - brand default. Matches the portal/email look ON THE PANEL.
const Theme Meadow = {
    "Meadow",
    PANEL_HEX(0x9cbf6a),   // bg      sage (calibration #8)
    PANEL_HEX(0x1a2010),   // fg      dark olive       8.0:1
    PANEL_HEX(0x3a4a22),   // fgDim                    4.6:1
    PANEL_HEX(0x6f8c45),   // rule
    PANEL_HEX(0x23271d),   // selectBg
    PANEL_HEX(0xe6f0d0),   // selectFg                12.9:1
    PANEL_HEX(0xd2692a),   // selectMark - brighter orange on the dark row  4.2:1
    PANEL_HEX(0x9a3f0a),   // accent  - deep orange on sage               3.3:1
    PANEL_HEX(0x1c4410),   // accent2 up                5.3:1
    PANEL_HEX(0x7a1a08),   // danger  down              5.1:1
    12,
};

// Linen - light, highest daylight readability.
const Theme Linen = {
    "Linen",
    PANEL_HEX(0xf4f3ec),   // bg      warm white
    PANEL_HEX(0x1c1c1a),   // fg                      15.3:1
    PANEL_HEX(0x5e5e56),   // fgDim                    5.9:1
    PANEL_HEX(0xc4c3b6),   // rule
    PANEL_HEX(0x1c1c1a),   // selectBg
    PANEL_HEX(0xf4f3ec),   // selectFg
    PANEL_HEX(0xd8641a),   // selectMark               4.7:1 on selectBg
    PANEL_HEX(0xd8641a),   // accent                   3.3:1
    PANEL_HEX(0x2f6f3a),   // accent2                  5.5:1
    PANEL_HEX(0xb3261e),   // danger                   5.9:1
    12,
};

// Forest - dark olive for dim rooms. The selected row is a raised olive
// rather than an inverted light slab, which glares in the dark.
const Theme Forest = {
    "Forest",
    PANEL_HEX(0x44552c),   // bg      olive (calibration #6)
    PANEL_HEX(0xeef2e2),   // fg                       7.1:1
    PANEL_HEX(0xc2cca8),   // fgDim                    4.8:1
    PANEL_HEX(0x6a7c48),   // rule
    PANEL_HEX(0x566b38),   // selectBg  raised olive (calibration #8)
    PANEL_HEX(0xffffff),   // selectFg                 5.9:1
    PANEL_HEX(0xffa050),   // selectMark               2.9:1 - light orange; the row itself carries the cue
    PANEL_HEX(0xffa050),   // accent                   4.0:1
    PANEL_HEX(0xc8f08a),   // accent2                  6.3:1
    PANEL_HEX(0xffa89a),   // danger                   4.4:1
    12,
};

// Onyx - black + FlugVel blue. Blue owns selection; an orange bar on the
// blue row would be 1.6:1 and vanish, so the mark is black and orange is
// kept for "now / attention" only.
const Theme Onyx = {
    "Onyx",
    PANEL_HEX(0x0d0f10),   // bg      black
    PANEL_HEX(0xe9f7f9),   // fg                      17.5:1
    PANEL_HEX(0x8fb4ba),   // fgDim                    8.6:1
    PANEL_HEX(0x2b3a3d),   // rule
    PANEL_HEX(0x9edbe6),   // selectBg  logo blue
    PANEL_HEX(0x0d0f10),   // selectFg                12.6:1
    PANEL_HEX(0x0d0f10),   // selectMark
    PANEL_HEX(0xf08a3a),   // accent                   7.7:1
    PANEL_HEX(0x7ee0a1),   // accent2                 12.0:1
    PANEL_HEX(0xff7a70),   // danger                   7.6:1
    12,
};

// Table of every theme this build knows about, indexed by the same numeric
// id CaptivePortal persists to EEPROM. Index 0 must always be a safe
// default. Ids are persisted - reuse a slot only for a like-for-like
// replacement: 0 was Mono Dot-Matrix (already sage), which Meadow refines.
static const Theme* const kThemes[] = {
    &Meadow,
    &Linen,
    &Forest,
    &Onyx,
};
static const int kThemeCount = sizeof(kThemes) / sizeof(kThemes[0]);

const Theme& byId(uint8_t id) {
    if (id < kThemeCount) return *kThemes[id];
    return Meadow;
}

int count() { return kThemeCount; }

} // namespace ThemeRegistry

const Theme* ThemeManager::_current = &ThemeRegistry::Meadow;

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
