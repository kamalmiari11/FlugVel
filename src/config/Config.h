#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../screens/ScreenId.h"

// ============================================================================
// Persistent user configuration, stored in NVS rather than EEPROM.
//
// WHY NOT EEPROM: the EEPROM block is 512 bytes with every field at a
// hand-picked offset (city at 0, country at 50, lat/lon at 100/104, the
// interval at 170, theme at 179 ... Wi-Fi credentials at 200-299). Adding a
// setting means choosing another free address by hand and editing
// saveLocationEEPROM() and loadLocationEEPROM() in step - and a screen
// order plus per-screen options plus several calendar feeds simply does not
// fit in the ~212 bytes left. NVS is a key/value store with no offsets to
// allocate and no fixed ceiling, and this project already uses it for the
// calendar URL, so it costs no new dependency.
//
// WHAT STAYED BEHIND: Wi-Fi credentials and the location block (city,
// country, lat/lon, timezone, bounce text, facing direction) are still in
// EEPROM, owned by CaptivePortal. They are set once during setup and never
// grow, so moving them would be churn without benefit - and leaving the
// credentials alone means an existing device still boots and connects
// exactly as before, even if this config fails to load.
//
// Everything here is a plain POD written with putBytes(), so the layout is
// the format. Any change to the fields below - added, removed, reordered,
// resized - MUST bump VERSION, and load() must gain a migration for it.
// ============================================================================

// Generous headroom over the six screens that exist today; the array is
// fixed-size so the whole struct stays a flat blob.
static const int CONFIG_MAX_SCREENS = 16;

// One entry in the user's cycle order. Settings is never stored here: it is
// pinned by ScreenRegistry::isPinned() and always appended last, so it can
// neither be hidden nor dragged out of place.
struct ScreenSlot {
    uint8_t id;       // a ScreenId value - NOT an index into anything
    uint8_t enabled;  // uint8_t rather than bool to keep the layout explicit
};

struct Config {
    uint8_t version;

    // ---- cycle ----
    // Order is the cycle order, so screens[0] is the home screen. Ids this
    // build does not recognise are kept (so a downgrade-then-upgrade puts
    // the screen back where it was) but skipped when building the cycle.
    uint8_t    screenCount;
    ScreenSlot screens[CONFIG_MAX_SCREENS];

    // ---- device ----
    uint8_t brightness;      // 0-100, drives Backlight::set()
    uint8_t dateTimeFormat;  // index into Units' format table, 0-3
    uint8_t themeId;         // index into ThemeRegistry
    uint8_t imperial;        // 0 = metric, 1 = imperial
    uint8_t showDate;        // date beside the clock in the header
    uint8_t legend;          // the bottom action-legend strip
    uint8_t leftHanded;      // board turned around: rotate 180 + flip encoder

    // ---- per-screen ----
    uint16_t flightIntervalSec;  // how often the nearest flight is re-fetched
    int32_t  flappyBest;
    int32_t  paddleBest;

    // ======================= END OF VERSION 1 =======================
    // Everything above this line is the v1 layout and MUST NOT be
    // reordered, resized or removed. New fields go below, appended only,
    // so that v1 stays a byte-exact prefix of the current struct and a
    // stored v1 blob can be migrated by copying that prefix and defaulting
    // the rest (see ConfigStore::begin). Reordering anything above would
    // silently reinterpret a real user's saved settings as garbage.
    // ================================================================

    // ---- added in v2: per-screen options ----
    uint8_t planeFlyover;    // play the fly-over animation on a new aircraft
    uint8_t planeQuote;      // quote strip under the plane box
    uint8_t weatherDays;     // rows in the week table, 3-7
    uint8_t focusMinutes;    // default focus length on the set screen
    uint8_t breakMinutes;    // default break length
    uint8_t calLookaheadDays;// how far forward the agenda looks
    uint8_t calAllDay;       // include events with no start time

    // ======================= END OF VERSION 2 =======================

    // ---- added in v3 ----
    uint8_t pagerStyle;      // 0 = dots, 1 = "n/N" counter, 2 = hidden
    uint8_t knobReversed;    // flip encoder direction (wiring, not handedness)
    uint8_t startupResume;   // 0 = boot to home, 1 = boot to the last screen
    uint8_t lastScreenId;    // which screen was showing, for startupResume

    // ======================= END OF VERSION 3 =======================

    // ---- added in v4: the rest of the per-screen options ----
    // Scaled to fit a byte where the natural unit would not: an altitude
    // floor in hundreds of metres, a refresh interval in ten-minute steps.
    uint8_t planeMinAlt100m;  // ignore aircraft below this; 0 = no limit
    uint8_t weatherRefresh10; // forecast refresh, in units of 10 minutes
    uint8_t weatherPrefetch;  // background-load each day's hourly data
    uint8_t weatherIcons;     // condition glyph beside each day
    uint8_t gamesMask;        // bit per entry in the game table; 0 = show all
    uint8_t focusLock;        // block screen switching while a session runs
    uint8_t focusAutoBreak;   // roll straight into the break when focus ends
    uint8_t calMerge;         // collapse the same event from several feeds
    uint8_t calMaxEvents;     // cap on events held in RAM
    uint8_t calHidePast;      // drop today's events once they have ended

    // ======================= END OF VERSION 4 =======================

    // ---- added in v5: best scores for the two newest games ----
    int32_t simonBest;
    int32_t airTrafficBest;

    // ======================= END OF VERSION 5 =======================
};

// Size of the v1 layout, i.e. the offset of the first field added in v2.
// Used to migrate a stored v1 blob rather than discarding it - a user who
// has already set their screen order should not lose it to a field being
// added elsewhere.
#define CONFIG_V1_SIZE offsetof(Config, planeFlyover)
#define CONFIG_V2_SIZE offsetof(Config, pagerStyle)
#define CONFIG_V3_SIZE offsetof(Config, planeMinAlt100m)
#define CONFIG_V4_SIZE offsetof(Config, simonBest)

namespace ConfigStore {

    // Bump on ANY change to the Config layout above, and handle the old
    // shape in load(). A stored blob whose version or size does not match
    // is discarded and rebuilt from defaults + the EEPROM migration.
    static const uint8_t VERSION = 5;

    // Loads the config, or builds it. Call once at boot, BEFORE anything
    // reads a setting - ThemeManager::begin(), Units::begin() and
    // CaptivePortal::loadLocationEEPROM() all take their values from here.
    // The one-time EEPROM migration happens inside, so nothing needs to have
    // read the old block first. Idempotent; safe to call twice.
    void begin();

    // The live config. Mutate it freely, then call save() - nothing here
    // writes to flash on its own.
    Config& get();

    // Persist to NVS. Cheap enough for a settings screen exit, but not for
    // something called every loop() - NVS wear is real.
    void save();

    // The cycle's home screen, expressed the way the Settings menu still
    // asks about it: an index into ScreenRegistry::selectable(). Home is
    // really "whatever screens[0] is", so these translate between the two.
    // setHomeSelectable() rotates the order rather than swapping, so the
    // sequence the user is used to cycling through is preserved - it just
    // starts somewhere else. Neither writes to flash; call save().
    int  homeSelectable();
    void setHomeSelectable(int index);

    // True if begin() found nothing stored and seeded this config from the
    // old EEPROM layout. Only useful for logging, but it makes a confusing
    // first boot after an update obvious in the serial monitor.
    bool migratedFromEeprom();

} // namespace ConfigStore
