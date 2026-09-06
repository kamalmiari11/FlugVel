#include "Config.h"
#include "../screens/ScreenRegistry.h"
#include "../ui/Theme.h"

#include <Arduino.h>
#include <Preferences.h>
#include <EEPROM.h>

// NVS namespace + key. "flugvel" is shared with the calendar URL that
// CaptivePortal already stores, which is fine - NVS keys are independent.
static const char* NVS_NAMESPACE = "flugvel";
static const char* NVS_KEY       = "cfg";

// The old EEPROM addresses this migrates from. They are duplicated here
// rather than shared with CaptivePortal on purpose: this is a snapshot of a
// layout that is now frozen history, and it must not drift if that file's
// remaining offsets are ever rearranged.
namespace LegacyEeprom {
    static const int SIZE          = 512;
    static const int INTERVAL      = 170;  // int32
    static const int HOME_SCREEN   = 174;  // uint8
    static const int FLAPPY_BEST   = 175;  // int32
    static const int THEME_ID      = 179;  // uint8
    static const int PADDLE_BEST   = 180;  // int32
    static const int DATETIME_FMT  = 184;  // uint8
    static const int IMPERIAL      = 185;  // uint8
}

static Config s_cfg;
static bool   s_loaded    = false;
static bool   s_migrated  = false;

// ---------------------------------------------------------------------------
// Defaults: every screen the registry knows about, in table order, enabled.
// Settings is skipped - it is pinned and appended by whoever builds the
// cycle, so storing it would let a future edit accidentally hide or move it.
// ---------------------------------------------------------------------------
static void applyV2Defaults(Config &c);
static void applyV3Defaults(Config &c);
static void applyV4Defaults(Config &c);

static void applyDefaults(Config &c) {
    memset(&c, 0, sizeof(c));
    c.version = ConfigStore::VERSION;

    c.screenCount = 0;
    for (int i = 0; i < ScreenRegistry::count() && c.screenCount < CONFIG_MAX_SCREENS; i++) {
        const ScreenDef &def = ScreenRegistry::all()[i];
        if (ScreenRegistry::isPinned(def.id)) continue;
        c.screens[c.screenCount].id      = (uint8_t)def.id;
        c.screens[c.screenCount].enabled = 1;
        c.screenCount++;
    }

    c.brightness        = 100;
    c.dateTimeFormat    = 0;
    c.themeId           = 0;
    c.imperial          = 0;
    c.showDate          = 1;
    c.legend            = 1;
    c.leftHanded        = 0;
    c.flightIntervalSec = 30;
    c.flappyBest        = 0;
    c.paddleBest        = 0;

    applyV2Defaults(c);
    applyV3Defaults(c);
    applyV4Defaults(c);
}

// Split out so the v1 -> v2 migration can reuse it: a v1 blob has none of
// these bytes, so they have to be filled in from scratch after the prefix
// is copied across.
static void applyV2Defaults(Config &c) {
    c.planeFlyover     = 1;
    c.planeQuote       = 1;
    c.weatherDays      = 7;
    c.focusMinutes     = 25;
    c.breakMinutes     = 5;
    c.calLookaheadDays = 60;
    c.calAllDay        = 1;
}

static void applyV3Defaults(Config &c) {
    c.pagerStyle    = 0;   // dots, as the header has always drawn them
    c.knobReversed  = 0;
    c.startupResume = 0;   // boot to home
    c.lastScreenId  = 0;
}

static void applyV4Defaults(Config &c) {
    c.planeMinAlt100m  = 0;    // no floor
    c.weatherRefresh10 = 3;    // 30 min, matching the old hardcoded interval
    c.weatherPrefetch  = 1;
    c.weatherIcons     = 0;
    c.gamesMask        = 0xFF; // every game visible
    c.focusLock        = 1;    // the existing behaviour
    c.focusAutoBreak   = 1;    // also the existing behaviour
    c.calMerge         = 1;
    c.calMaxEvents     = 20;
    c.calHidePast      = 0;
}

// ---------------------------------------------------------------------------
// One-time migration out of the old EEPROM layout.
//
// Every read is range-checked exactly the way CaptivePortal::loadLocation-
// EEPROM() checks it, because a never-written EEPROM reads back as 0xFF and
// would otherwise hand us an interval of -1 or a theme id of 255. Anything
// that fails its check keeps the default already in `c`.
// ---------------------------------------------------------------------------
static void migrateFromEeprom(Config &c) {
    EEPROM.begin(LegacyEeprom::SIZE);

    int interval = 0;
    EEPROM.get(LegacyEeprom::INTERVAL, interval);
    if (interval >= 5 && interval <= 3600) c.flightIntervalSec = (uint16_t)interval;

    int flappy = 0;
    EEPROM.get(LegacyEeprom::FLAPPY_BEST, flappy);
    if (flappy >= 0 && flappy <= 9999) c.flappyBest = flappy;

    int paddle = 0;
    EEPROM.get(LegacyEeprom::PADDLE_BEST, paddle);
    if (paddle >= 0 && paddle <= 9999) c.paddleBest = paddle;

    uint8_t theme = EEPROM.read(LegacyEeprom::THEME_ID);
    if (theme < ThemeRegistry::count()) c.themeId = theme;

    uint8_t fmt = EEPROM.read(LegacyEeprom::DATETIME_FMT);
    if (fmt < 4) c.dateTimeFormat = fmt;

    uint8_t imp = EEPROM.read(LegacyEeprom::IMPERIAL);
    if (imp <= 1) c.imperial = imp;

    // The old "home screen" was an index into the addScreen() order. There
    // is no index any more - screens[0] IS home - so honour the user's old
    // choice by rotating the default order until that screen leads. Doing
    // it as a rotation rather than a swap keeps the rest of the sequence in
    // the order they were used to cycling through.
    uint8_t home = EEPROM.read(LegacyEeprom::HOME_SCREEN);
    const ScreenDef* homeDef = ScreenRegistry::selectable(home);
    if (homeDef) {
        int at = -1;
        for (int i = 0; i < c.screenCount; i++) {
            if (c.screens[i].id == (uint8_t)homeDef->id) { at = i; break; }
        }
        if (at > 0) {
            ScreenSlot rotated[CONFIG_MAX_SCREENS];
            for (int i = 0; i < c.screenCount; i++) {
                rotated[i] = c.screens[(at + i) % c.screenCount];
            }
            memcpy(c.screens, rotated, sizeof(ScreenSlot) * c.screenCount);
        }
    }

    Serial.printf("[Config] Migrated from EEPROM: home=%s interval=%us theme=%u fmt=%u %s\n",
                  homeDef ? homeDef->name : "(default)",
                  c.flightIntervalSec, c.themeId, c.dateTimeFormat,
                  c.imperial ? "imperial" : "metric");
}

namespace ConfigStore {

void begin() {
    if (s_loaded) return;

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/true)) {
        // Namespace does not exist yet - first boot on this firmware.
        applyDefaults(s_cfg);
        migrateFromEeprom(s_cfg);
        s_migrated = true;
        s_loaded   = true;
        save();
        return;
    }

    Config stored;
    size_t got = prefs.getBytes(NVS_KEY, &stored, sizeof(stored));
    prefs.end();

    // Reject anything that is not exactly the shape this build expects.
    // A short read means a different (older) layout; a version mismatch
    // means the fields moved. Either way the bytes cannot be trusted
    // field-by-field, so rebuild rather than reinterpret.
    // Every version is an append-only extension of the one before (see the
    // END OF VERSION markers in Config.h), so an older blob is a byte-exact
    // prefix of the current struct: copy what is there, fill in whatever
    // came later, and write it back in the current shape. Throwing it away
    // instead would silently reset a screen order the user had set.
    if (got >= CONFIG_V1_SIZE && got < sizeof(stored) &&
        stored.version >= 1 && stored.version < VERSION) {
        memset(&s_cfg, 0, sizeof(s_cfg));
        memcpy(&s_cfg, &stored, got);
        if (got < CONFIG_V2_SIZE) applyV2Defaults(s_cfg);
        if (got < CONFIG_V3_SIZE) applyV3Defaults(s_cfg);
        if (got < sizeof(stored)) applyV4Defaults(s_cfg);
        uint8_t was = s_cfg.version;
        s_cfg.version = VERSION;
        s_loaded = true;
        Serial.printf("[Config] Migrated stored config v%u -> v%u\n", was, VERSION);
        save();
        return;
    }

    if (got == sizeof(stored) && stored.version == VERSION) {
        s_cfg    = stored;
        s_loaded = true;

        // Guard the one field that indexes an array at runtime, in case the
        // blob was written by a build with more screens compiled in.
        if (s_cfg.screenCount > CONFIG_MAX_SCREENS) s_cfg.screenCount = CONFIG_MAX_SCREENS;
        Serial.printf("[Config] Loaded from NVS (%u screens)\n", s_cfg.screenCount);
        return;
    }

    Serial.printf("[Config] No usable config in NVS (read %u bytes, version %u) - rebuilding\n",
                  (unsigned)got, got ? stored.version : 0);
    applyDefaults(s_cfg);
    migrateFromEeprom(s_cfg);
    s_migrated = true;
    s_loaded   = true;
    save();
}

Config& get() {
    if (!s_loaded) begin();
    return s_cfg;
}

void save() {
    s_cfg.version = VERSION;

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/false)) {
        Serial.println("[Config] NVS open failed - settings not saved");
        return;
    }
    size_t written = prefs.putBytes(NVS_KEY, &s_cfg, sizeof(s_cfg));
    prefs.end();

    if (written != sizeof(s_cfg)) {
        Serial.printf("[Config] Short write to NVS (%u of %u bytes)\n",
                      (unsigned)written, (unsigned)sizeof(s_cfg));
    }
}

int homeSelectable() {
    Config &c = get();
    if (c.screenCount == 0) return 0;
    for (int i = 0; i < ScreenRegistry::selectableCount(); i++) {
        const ScreenDef* d = ScreenRegistry::selectable(i);
        if (d && (uint8_t)d->id == c.screens[0].id) return i;
    }
    return 0;  // screens[0] is unknown to this build (downgrade)
}

void setHomeSelectable(int index) {
    const ScreenDef* d = ScreenRegistry::selectable(index);
    if (!d) return;

    Config &c = get();
    int at = -1;
    for (int i = 0; i < c.screenCount; i++) {
        if (c.screens[i].id == (uint8_t)d->id) { at = i; break; }
    }
    if (at <= 0) return;  // not found, or already leading

    ScreenSlot rotated[CONFIG_MAX_SCREENS];
    for (int i = 0; i < c.screenCount; i++) {
        rotated[i] = c.screens[(at + i) % c.screenCount];
    }
    memcpy(c.screens, rotated, sizeof(ScreenSlot) * c.screenCount);
}

bool migratedFromEeprom() { return s_migrated; }

} // namespace ConfigStore
