#include "Header.h"
#include "../ui/Theme.h"
#include "../ui/UiChrome.h"
#include "../ui/Units.h"
#include "../config/Config.h"

Header::Header(TFT_eSPI* display) {
    tft = display;
}

// Maps the IANA zone names this project actually deals with (the
// CaptivePortal dropdown list, plus common ones ip-api.com/Open-Meteo
// return) to POSIX TZ rule strings that configTzTime()/newlib's tzset()
// can actually parse, DST included. Unknown zones fall back to UTC0 so we
// never silently drift - at worst you get UTC displayed instead of local.
String Header::posixTzFromIana(const String &iana) {
    struct TzEntry { const char* iana; const char* posix; };
    static const TzEntry table[] = {
        // Dropdown list in CaptivePortal.cpp
        { "UTC",                  "UTC0" },
        { "Europe/London",        "GMT0BST,M3.5.0/1,M10.5.0/2" },
        { "Europe/Amsterdam",     "CET-1CEST,M3.5.0,M10.5.0/3" },
        { "Europe/Berlin",        "CET-1CEST,M3.5.0,M10.5.0/3" },
        { "Europe/Paris",         "CET-1CEST,M3.5.0,M10.5.0/3" },
        { "America/New_York",     "EST5EDT,M3.2.0,M11.1.0" },
        { "America/Chicago",      "CST6CDT,M3.2.0,M11.1.0" },
        { "America/Denver",       "MST7MDT,M3.2.0,M11.1.0" },
        { "America/Los_Angeles",  "PST8PDT,M3.2.0,M11.1.0" },
        { "Asia/Tokyo",           "JST-9" },
        { "Asia/Shanghai",        "CST-8" },
        { "Asia/Kolkata",         "IST-5:30" },
        { "Australia/Sydney",     "AEST-10AEDT,M10.1.0,M4.1.0/3" },
        // A few extra common ones auto-detect (ip-api.com/Open-Meteo) may
        // return that aren't in the manual dropdown
        { "Europe/Madrid",        "CET-1CEST,M3.5.0,M10.5.0/3" },
        { "Europe/Rome",          "CET-1CEST,M3.5.0,M10.5.0/3" },
        { "Europe/Brussels",      "CET-1CEST,M3.5.0,M10.5.0/3" },
        { "Europe/Dublin",        "IST-1GMT0,M10.5.0,M3.5.0/1" },
        { "Europe/Moscow",        "MSK-3" },
        { "America/Sao_Paulo",    "<-03>3" },
        { "America/Toronto",      "EST5EDT,M3.2.0,M11.1.0" },
        { "Asia/Dubai",           "<+04>-4" },
        { "Asia/Singapore",       "<+08>-8" },
        { "Asia/Hong_Kong",       "HKT-8" },
        { "Pacific/Auckland",     "NZST-12NZDT,M9.5.0,M4.1.0/3" },
    };

    for (const auto &e : table) {
        if (iana == e.iana) return String(e.posix);
    }

    Serial.printf("[Header] Unrecognized timezone \"%s\" - falling back to UTC\n", iana.c_str());
    return String("UTC0");
}

void Header::setTimezone(const String &timezone) {
    if (timezone == _timezone) return;
    _timezone = timezone;
    String posixTz = posixTzFromIana(_timezone);
    configTzTime(posixTz.c_str(), "pool.ntp.org", "time.nist.gov");
    invalidate();   // the strip must repaint - the hour may have jumped
    Serial.printf("[Header] Timezone now %s\n", _timezone.c_str());
}

void Header::begin(const String &timezone) {
    // Save the user-selected IANA timezone
    _timezone = timezone;

    // configTzTime()'s first argument must be a POSIX TZ rule string (with
    // DST transition rules), NOT an IANA zone name - it silently fails to
    // apply anything meaningful if given "Europe/Amsterdam" directly,
    // which is why the clock was showing a fixed number of hours behind
    // (effectively plain UTC, no DST). Convert first.
    String posixTz = posixTzFromIana(_timezone);
    configTzTime(posixTz.c_str(), "pool.ntp.org", "time.nist.gov");

    // Wait for NTP to sync (optional, prevents first incorrect time display)
    Serial.print("Waiting for NTP time sync");
    struct tm timeinfo;
    int retries = 10;
    while (!getLocalTime(&timeinfo) && retries > 0) {
        Serial.print(".");
        delay(1000);
        retries--;
    }
    Serial.println();
}

void Header::draw(const String &title, int currentScreenIndex, int screenCount) {
    const Theme &theme = ThemeManager::current();

    struct tm timeinfo;
    bool haveTime = getLocalTime(&timeinfo, 5);

    char combined[32] = "";
    if (haveTime) {
        char timeString[12]; // "HH:MM" or "HH:MM AM"
        strftime(timeString, sizeof(timeString), Units::strftimeTime(), &timeinfo);

        // Dropping the date frees roughly 60px in this strip, which is what
        // keeps the centred pager clear of the title once there are a lot of
        // screens in the cycle.
        if (ConfigStore::get().showDate) {
            char dateString[12];
            strftime(dateString, sizeof(dateString), Units::strftimeDate(), &timeinfo);
            snprintf(combined, sizeof(combined), "%s  %s", dateString, timeString);
        } else {
            snprintf(combined, sizeof(combined), "%s", timeString);
        }
    }

    // Split "did anything change" into two cases so the common one - only
    // the clock ticked over - never repaints the whole strip. Clearing the
    // full-width header rect once a minute was a visible black flash across
    // the top of every screen; now that only happens when the title or the
    // pager actually change (i.e. on a screen switch).
    bool layoutChanged = !_drawnOnce || title != _lastTitle ||
                          currentScreenIndex != _lastScreenIndex ||
                          screenCount != _lastScreenCount;
    bool timeChanged = String(combined) != _lastTimeString;
    if (!layoutChanged && !timeChanged) return;

    _drawnOnce = true;
    _lastTitle = title;
    _lastScreenIndex = currentScreenIndex;
    _lastScreenCount = screenCount;
    _lastTimeString = combined;

    const int pagerDotCount = (currentScreenIndex >= 0) ? screenCount : 0;
    const int W = tft->width();
    const int clockRightEdge = W - 4;

    if (layoutChanged) {
        tft->fillRect(0, 0, W, headerHeight, theme.bg);
        tft->drawFastHLine(0, headerHeight - 1, W, theme.rule);

        // Bracketed title, top-left, e.g. "[ DASHBOARD ]"
        if (title.length() > 0) {
            String bracketed = title;
            bracketed.toUpperCase();
            UiChrome::drawBracketTitle(tft, bracketed, 4, 6, theme.fg);
        }

        // Pager, CENTERED. Dots read well up to about ten screens; past
        // that they crowd the title, so a plain "n/N" counter stays legible
        // at any count and "off" gives the strip back entirely.
        const uint8_t pagerStyle = ConfigStore::get().pagerStyle;
        if (pagerDotCount > 0 && pagerStyle == 0) {
            const int pagerSpan  = (pagerDotCount - 1) * 9 + 4;
            const int pagerRight = (W + pagerSpan) / 2;
            UiChrome::drawDotPager(tft, currentScreenIndex, pagerDotCount, pagerRight,
                                    headerHeight / 2 - 1, theme.fg, theme.rule);
        } else if (pagerDotCount > 0 && pagerStyle == 1) {
            char counter[12];
            snprintf(counter, sizeof(counter), "%d/%d", currentScreenIndex + 1, pagerDotCount);
            tft->setTextDatum(MC_DATUM);
            tft->setTextFont(1);
            tft->setTextSize(1);
            tft->setTextColor(theme.fgDim);
            tft->drawString(counter, W / 2, headerHeight / 2 - 1);
            tft->setTextDatum(TL_DATUM);
        }
    }

    // Date + time, RIGHT-aligned at the far edge (always shown). The title
    // is far left and the pager sits in the middle, so on a plain clock
    // tick we only clear/repaint this right-hand band.
    tft->setTextDatum(TL_DATUM);
    tft->setTextFont(1);
    tft->setTextSize(1);
    tft->setTextColor(theme.fgDim, theme.bg);

    if (haveTime) {
        int16_t textW = tft->textWidth(combined);
        int16_t x = clockRightEdge - textW;
        if (!layoutChanged) {
            tft->fillRect(x - 4, 1, (W - (x - 4)), headerHeight - 2, theme.bg);
        }
        tft->setCursor(x, 6);
        tft->print(combined);
    }
}