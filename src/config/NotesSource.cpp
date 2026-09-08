#include "NotesSource.h"
#include <Preferences.h>

static const char* NVS_NAMESPACE = "flugvel";
static const char* KEY_TOKEN     = "notesTok";
static const char* KEY_PAGE      = "notesPg";
static const char* KEY_MAX       = "notesMax";
static const char* KEY_CHECKED   = "notesChk";
static const char* KEY_GROUP     = "notesGrp";

static String s_token;
static String s_pageId;
static int    s_maxItems    = 8;
static bool   s_showChecked = false;
static bool   s_groupByDay  = false;
static bool   s_loaded      = false;

namespace NotesSource {

void begin() {
    if (s_loaded) return;
    s_loaded = true;

    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, /*readOnly=*/true)) {
        s_token       = prefs.getString(KEY_TOKEN, "");
        s_pageId      = prefs.getString(KEY_PAGE,  "");
        s_maxItems    = prefs.getInt(KEY_MAX, 8);
        s_showChecked = prefs.getBool(KEY_CHECKED, false);
        s_groupByDay  = prefs.getBool(KEY_GROUP, false);
        prefs.end();
    }
    if (s_maxItems < 5 || s_maxItems > 15) s_maxItems = 8;
}

const String& token()  { return s_token; }
const String& pageId() { return s_pageId; }
int  maxItems()        { return s_maxItems; }
bool showChecked()     { return s_showChecked; }
bool groupByDay()      { return s_groupByDay; }

bool usable() { return s_token.length() > 0 && s_pageId.length() > 0; }

void set(const String &tok, const String &page, int max, bool showChk, bool groupDay) {
    s_token       = tok;
    s_pageId      = page;
    s_maxItems    = (max < 5) ? 5 : (max > 15 ? 15 : max);
    s_showChecked = showChk;
    s_groupByDay  = groupDay;
}

void save() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/false)) {
        Serial.println("[NotesSource] NVS open failed - not saved");
        return;
    }
    prefs.putString(KEY_TOKEN, s_token);
    prefs.putString(KEY_PAGE,  s_pageId);
    prefs.putInt(KEY_MAX, s_maxItems);
    prefs.putBool(KEY_CHECKED, s_showChecked);
    prefs.putBool(KEY_GROUP, s_groupByDay);
    prefs.end();
}

} // namespace NotesSource
