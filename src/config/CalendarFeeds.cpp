#include "CalendarFeeds.h"
#include <Preferences.h>

static const char* NVS_NAMESPACE = "flugvel";
static const char* KEY_COUNT     = "calN";

static CalFeed s_feeds[CAL_MAX_FEEDS];
static int     s_count  = 0;
static bool    s_loaded = false;

// Keys are "calU0"/"calL0"/"calE0" .. one set per slot. Short because NVS
// key names are capped at 15 characters.
static String keyUrl (int i) { return String("calU") + i; }
static String keyName(int i) { return String("calL") + i; }
static String keyOn  (int i) { return String("calE") + i; }

namespace CalendarFeeds {

void begin(const String &legacySingleUrl) {
    if (s_loaded) return;
    s_loaded = true;

    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, /*readOnly=*/true)) {
        s_count = prefs.getInt(KEY_COUNT, 0);
        if (s_count < 0) s_count = 0;
        if (s_count > CAL_MAX_FEEDS) s_count = CAL_MAX_FEEDS;

        for (int i = 0; i < s_count; i++) {
            s_feeds[i].url     = prefs.getString(keyUrl(i).c_str(),  "");
            s_feeds[i].name    = prefs.getString(keyName(i).c_str(), "");
            s_feeds[i].enabled = prefs.getBool  (keyOn(i).c_str(),   true);
            if (s_feeds[i].name.length() == 0) s_feeds[i].name = String("Calendar ") + (i + 1);
        }
        prefs.end();
    }

    // Nothing stored yet. If this device already had a calendar configured
    // through the old single-URL setting, carry it into slot 0 rather than
    // making the user paste it again.
    if (s_count == 0 && legacySingleUrl.length() >= 8) {
        s_feeds[0].name    = "Calendar";
        s_feeds[0].url     = legacySingleUrl;
        s_feeds[0].enabled = true;
        s_count = 1;
        save();
        Serial.println("[CalendarFeeds] Migrated the single calendar URL into slot 0");
    }
}

int count() { return s_count; }

const CalFeed& at(int index) {
    static CalFeed empty;
    if (index < 0 || index >= s_count) return empty;
    return s_feeds[index];
}

int usableCount() {
    int n = 0;
    for (int i = 0; i < s_count; i++) if (s_feeds[i].usable()) n++;
    return n;
}

void set(int index, const String &name, const String &url, bool enabled) {
    if (index < 0 || index >= CAL_MAX_FEEDS) return;
    if (index >= s_count) s_count = index + 1;
    s_feeds[index].name    = name.length() ? name : String("Calendar ") + (index + 1);
    s_feeds[index].url     = url;
    s_feeds[index].enabled = enabled;
}

bool add(const String &name, const String &url) {
    if (s_count >= CAL_MAX_FEEDS) return false;
    set(s_count, name, url, true);
    return true;
}

void remove(int index) {
    if (index < 0 || index >= s_count) return;
    for (int i = index; i < s_count - 1; i++) s_feeds[i] = s_feeds[i + 1];
    s_feeds[s_count - 1] = CalFeed();
    s_count--;
}

void clear() {
    for (int i = 0; i < CAL_MAX_FEEDS; i++) s_feeds[i] = CalFeed();
    s_count = 0;
}

void save() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/false)) {
        Serial.println("[CalendarFeeds] NVS open failed - feeds not saved");
        return;
    }
    prefs.putInt(KEY_COUNT, s_count);
    for (int i = 0; i < s_count; i++) {
        prefs.putString(keyUrl(i).c_str(),  s_feeds[i].url);
        prefs.putString(keyName(i).c_str(), s_feeds[i].name);
        prefs.putBool  (keyOn(i).c_str(),   s_feeds[i].enabled);
    }
    // Clear the tail so a removed feed can't come back if the count is ever
    // written larger again.
    for (int i = s_count; i < CAL_MAX_FEEDS; i++) {
        prefs.remove(keyUrl(i).c_str());
        prefs.remove(keyName(i).c_str());
        prefs.remove(keyOn(i).c_str());
    }
    prefs.end();
}

} // namespace CalendarFeeds
