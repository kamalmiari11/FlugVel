#include "StockSource.h"
#include <Preferences.h>

static const char* NVS_NAMESPACE = "flugvel";
static const char* KEY_APIKEY    = "stockKey";
static const char* KEY_SYMBOLS   = "stockSym";
static const char* KEY_REFRESH   = "stockRef";

static String s_apiKey;
static String s_symbolsCsv;
static int    s_refreshMinutes = 15;
static bool   s_loaded = false;

namespace StockSource {

void begin() {
    if (s_loaded) return;
    s_loaded = true;

    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, /*readOnly=*/true)) {
        s_apiKey         = prefs.getString(KEY_APIKEY, "");
        s_symbolsCsv     = prefs.getString(KEY_SYMBOLS, "");
        s_refreshMinutes = prefs.getInt(KEY_REFRESH, 15);
        prefs.end();
    }
    if (s_refreshMinutes < 5 || s_refreshMinutes > 60) s_refreshMinutes = 15;
}

const String& apiKey()      { return s_apiKey; }
const String& symbolsCsv()  { return s_symbolsCsv; }
int  refreshMinutes()       { return s_refreshMinutes; }

int symbolCount() {
    int n = 0;
    int start = 0;
    while (start < (int)s_symbolsCsv.length() && n < MAX_SYMBOLS) {
        int comma = s_symbolsCsv.indexOf(',', start);
        String piece = (comma < 0) ? s_symbolsCsv.substring(start) : s_symbolsCsv.substring(start, comma);
        piece.trim();
        if (piece.length() > 0) n++;
        if (comma < 0) break;
        start = comma + 1;
    }
    return n;
}

String symbolAt(int index) {
    if (index < 0 || index >= MAX_SYMBOLS) return "";
    int seen = 0;
    int start = 0;
    while (start <= (int)s_symbolsCsv.length()) {
        int comma = s_symbolsCsv.indexOf(',', start);
        String piece = (comma < 0) ? s_symbolsCsv.substring(start) : s_symbolsCsv.substring(start, comma);
        piece.trim();
        piece.toUpperCase();
        if (piece.length() > 0) {
            if (seen == index) return piece;
            seen++;
        }
        if (comma < 0) break;
        start = comma + 1;
    }
    return "";
}

bool usable() { return s_apiKey.length() > 0 && symbolCount() > 0; }

void set(const String &key, const String &symbolsCsv, int refreshMinutes) {
    s_apiKey         = key;
    s_symbolsCsv     = symbolsCsv;
    s_refreshMinutes = (refreshMinutes < 5) ? 5 : (refreshMinutes > 60 ? 60 : refreshMinutes);
}

void save() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/false)) {
        Serial.println("[StockSource] NVS open failed - not saved");
        return;
    }
    prefs.putString(KEY_APIKEY, s_apiKey);
    prefs.putString(KEY_SYMBOLS, s_symbolsCsv);
    prefs.putInt(KEY_REFRESH, s_refreshMinutes);
    prefs.end();
}

} // namespace StockSource
