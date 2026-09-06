#include "CalendarScreen.h"
#include "../config/Config.h"
#include "../config/CalendarFeeds.h"
#include "../ui/Theme.h"
#include "../ui/UiChrome.h"
#include "../network/CaptivePortal.h"
#include <time.h>

CalendarScreen::CalendarScreen(TFT_eSPI* display, CaptivePortal* portal)
    : Screen(display), _portal(portal), _count(0), _sel(0), _scroll(0),
      _detail(false), _fetched(false), _fetchFailed(false), _fetchBadUrl(false),
      _lastFetch(0), _needsRedraw(true) {}

void CalendarScreen::init() {
    _detail = false;
    _needsRedraw = true;
}

// ---- data ----
void CalendarScreen::doFetch() {
    const Theme &t = ThemeManager::current();
    const int W = tft->width();

    // The .ics fetch is blocking (a couple of seconds) - show a hint first.
    tft->fillRect(0, 20, W, tft->height() - 50, t.bg);
    tft->setTextDatum(MC_DATUM);
    tft->setTextSize(1);
    tft->setTextColor(t.fgDim);
    tft->drawString("Loading calendar...", W / 2, 110);
    tft->setTextDatum(TL_DATUM);

    // WINDOW_DAYS is the ceiling this screen is built around; the user can
    // ask for a shorter horizon, which also means fewer events parsed and
    // held.
    int window = ConfigStore::get().calLookaheadDays;
    if (window <= 0 || window > WINDOW_DAYS) window = WINDOW_DAYS;

    // ---- fetch every enabled feed into one list ----
    // Feeds are fetched one at a time into a scratch buffer and appended,
    // rather than all at once, because each fetch already needs a few KB of
    // heap for the HTTP body and running them concurrently is the fastest
    // way to run out of it.
    int  n = 0;
    bool anyOk = false, anyBadUrl = false, anyFail = false;

    for (int f = 0; f < CalendarFeeds::count() && n < MAX_EVENTS; f++) {
        const CalFeed &feed = CalendarFeeds::at(f);
        if (!feed.usable()) continue;

        int got = fetchCalendarEvents(_events + n, MAX_EVENTS - n, feed.url, window);
        if (got >= 0) {
            anyOk = true;
            for (int i = 0; i < got; i++) _events[n + i].feedIndex = (uint8_t)f;
            n += got;
        } else if (got == -2) {
            anyBadUrl = true;
        } else {
            anyFail = true;
        }
    }

    // Drop all-day entries here rather than in the parser, so turning them
    // back on is a redraw away and doesn't need a re-fetch.
    if (!ConfigStore::get().calAllDay) {
        int kept = 0;
        for (int i = 0; i < n; i++) {
            if (!_events[i].allDay) _events[kept++] = _events[i];
        }
        n = kept;
    }

    if (ConfigStore::get().calMerge) n = mergeDuplicates(n);
    sortByStart(n);

    // Drop today's events once they have actually ENDED, not when they
    // start - a meeting running right now should stay on the list. iCal
    // gives an end time for timed events; all-day entries have none, so
    // they are never hidden this way.
    if (ConfigStore::get().calHidePast) {
        time_t now = time(nullptr);
        int kept = 0;
        for (int i = 0; i < n; i++) {
            bool finished = !_events[i].allDay && _events[i].end != 0 && _events[i].end <= now;
            if (!finished) _events[kept++] = _events[i];
        }
        n = kept;
    }

    // Cap on what is held, applied after sorting so it keeps the soonest
    // events rather than whichever feed happened to be fetched first.
    int cap = ConfigStore::get().calMaxEvents;
    if (cap > 0 && n > cap) n = cap;

    if (anyOk)           { _count = n; _fetchFailed = false; _fetchBadUrl = false; }
    else if (anyBadUrl)  { _fetchFailed = true;  _fetchBadUrl = true; }
    else if (anyFail)    { _fetchFailed = true;  _fetchBadUrl = false; }
    else                 { _count = 0; _fetchFailed = false; _fetchBadUrl = false; }

    _fetched = true;
    _lastFetch = millis();
    if (_sel >= _count) _sel = _count > 0 ? _count - 1 : 0;
    _scroll = 0;
    _needsRedraw = true;
}

// The same appointment often lands in two calendars - a work meeting that
// is also on a personal feed, say. Prefer the feed's UID: it survives the
// title being reworded or re-whitespaced by whichever client wrote it. The
// start time is compared too, so a hash collision cannot silently swallow
// an unrelated event. Feeds that publish no UID fall back to title+start,
// which is weaker but better than showing the row twice.
//
// Earlier entries win, so the event keeps the feed listed first.
int CalendarScreen::mergeDuplicates(int count) {
    if (count < 2) return count;

    int kept = 0;
    for (int i = 0; i < count; i++) {
        bool dup = false;
        for (int j = 0; j < kept; j++) {
            const CalEvent &a = _events[i], &b = _events[j];
            if (a.start != b.start) continue;
            if (a.uidHash != 0 && b.uidHash != 0) {
                if (a.uidHash == b.uidHash) { dup = true; break; }
            } else if (strcmp(a.title, b.title) == 0) {
                dup = true; break;
            }
        }
        if (!dup) _events[kept++] = _events[i];
    }
    return kept;
}

// Each feed arrives sorted, but concatenating several does not stay sorted.
// Insertion sort: the list is at most MAX_EVENTS (20) and nearly ordered
// already, which is the case it handles best.
void CalendarScreen::sortByStart(int count) {
    for (int i = 1; i < count; i++) {
        CalEvent key = _events[i];
        int j = i - 1;
        while (j >= 0 && _events[j].start > key.start) {
            _events[j + 1] = _events[j];
            j--;
        }
        _events[j + 1] = key;
    }
}

void CalendarScreen::onConfigChanged() {
    _fetched   = false;   // update() refetches on the next tick
    _lastFetch = 0;
    _sel = 0; _scroll = 0; _detail = false;
    _needsRedraw = true;
}

void CalendarScreen::update() {
    if (CalendarFeeds::usableCount() == 0) return;   // nothing to fetch
    if (WiFi.status() != WL_CONNECTED) return;
    if (time(nullptr) < 1600000000) return;               // clock not synced

    bool stale = !_fetched
               || (_fetchFailed && millis() - _lastFetch > 120000UL) // retry sooner after a failure
               || (millis() - _lastFetch > REFRESH_MS);
    if (stale) doFetch();
}

// ---- date helpers ----
static int diffDays(const struct tm &a, const struct tm &b) { // a - b, in whole days
    if (a.tm_year == b.tm_year) return a.tm_yday - b.tm_yday;
    int yb = b.tm_year + 1900;
    int daysInB = ((yb % 4 == 0 && (yb % 100 != 0 || yb % 400 == 0)) ? 366 : 365);
    return (daysInB - b.tm_yday) + a.tm_yday;   // window only ever runs forward
}

String CalendarScreen::relLabel(const CalEvent &e) const {
    time_t s = e.start;
    struct tm et; gmtime_r(&s, &et);              // event shown "as written" in the feed
    time_t nowT = time(nullptr);
    struct tm ln; localtime_r(&nowT, &ln);

    int d = diffDays(et, ln);
    if (d <= 0) return "TODAY";
    if (d == 1) return "TMRW";

    static const char* wd[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
    char b[12];
    snprintf(b, sizeof(b), "%s %d", wd[et.tm_wday % 7], et.tm_mday);
    return String(b);
}

String CalendarScreen::timeLabel(const CalEvent &e) const {
    if (e.allDay) return "";
    time_t s = e.start;
    struct tm et; gmtime_r(&s, &et);
    char b[8];
    snprintf(b, sizeof(b), "%02d:%02d", et.tm_hour, et.tm_min);
    return String(b);
}

// ---- drawing ----
void CalendarScreen::emptyState(const char* l1, const char* l2) {
    const Theme &t = ThemeManager::current();
    tft->setTextDatum(MC_DATUM);
    tft->setTextSize(2);
    tft->setTextColor(t.fg, t.bg);
    tft->drawString(l1, tft->width() / 2, 100);
    tft->setTextSize(1);
    tft->setTextColor(t.fgDim, t.bg);
    tft->drawString(l2, tft->width() / 2, 124);
    tft->setTextDatum(TL_DATUM);
}

void CalendarScreen::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;

    const Theme &t = ThemeManager::current();
    const int W = tft->width();
    tft->fillRect(0, 20, W, tft->height() - 50, t.bg);   // body only; header + legend are drawn elsewhere

    if (CalendarFeeds::usableCount() == 0) {
        emptyState("No calendar linked", "add an iCal link during setup");
        return;
    }
    if (!_fetched) return;                                // update() will fetch + redraw
    if (_fetchBadUrl && _count == 0) {
        emptyState("Not an iCal (.ics) link", "use the Secret iCal address");
        return;
    }
    if (_fetchFailed && _count == 0) {
        emptyState("Couldn't load calendar", "check the feed URL");
        return;
    }
    if (_count == 0) {
        emptyState("Nothing scheduled", "in the next 60 days");
        return;
    }

    if (_detail) drawDetail();
    else         drawList();
}

void CalendarScreen::drawList() {
    const Theme &t = ThemeManager::current();
    const int W = tft->width();
    const int rowX = 6, rowW = W - 12;

    tft->setTextDatum(TL_DATUM);

    for (int i = 0; i < VISIBLE_ROWS; i++) {
        int idx = _scroll + i;
        if (idx >= _count) break;

        const CalEvent &e = _events[idx];
        int y = LIST_TOP + i * ROW_H;
        bool sel = (idx == _sel);

        tft->fillRect(rowX, y, rowW, ROW_H - 2, sel ? t.selectBg : t.bg);
        if (sel) tft->fillRect(rowX, y, 4, ROW_H - 2, t.accent);

        // left column: relative day + time
        tft->setTextSize(1);
        tft->setTextColor(sel ? t.selectFg : t.accent, sel ? t.selectBg : t.bg);
        tft->setCursor(rowX + 10, y + 3);
        tft->print(relLabel(e));

        String tl = timeLabel(e);
        tft->setTextColor(sel ? t.selectFg : t.fgDim, sel ? t.selectBg : t.bg);
        tft->setCursor(rowX + 10, y + 15);
        tft->print(tl.length() ? tl : String("all day"));

        // title, size-2, fills the rest
        const int titleX = rowX + 78;
        const int maxW = rowW - 78 - 6;
        tft->setTextSize(2);
        tft->setTextColor(sel ? t.selectFg : t.fg, sel ? t.selectBg : t.bg);
        String title = e.title[0] ? e.title : "(no title)";
        while (title.length() > 1 && tft->textWidth(title) > maxW)
            title.remove(title.length() - 1);
        tft->setCursor(titleX, y + (ROW_H - 2 - 16) / 2);
        tft->print(title);

        // With more than one calendar in play, say which one an event came
        // from - otherwise a merged agenda gives no way to tell a work
        // meeting from a personal one. A single feed needs no marker.
        if (CalendarFeeds::usableCount() > 1) {
            const String &fname = CalendarFeeds::at(e.feedIndex).name;
            if (fname.length()) {
                char mark[2] = { (char)toupper(fname[0]), 0 };
                tft->setTextSize(1);
                tft->setTextDatum(MR_DATUM);
                tft->setTextColor(sel ? t.selectFg : t.fgDim, sel ? t.selectBg : t.bg);
                tft->drawString(mark, rowX + rowW - 6, y + (ROW_H - 2) / 2);
                tft->setTextDatum(TL_DATUM);
            }
        }
    }

    // scroll indicator
    if (_count > VISIBLE_ROWS) {
        int trackH = VISIBLE_ROWS * ROW_H - 6;
        int th = max(8, trackH * VISIBLE_ROWS / _count);
        int ty = LIST_TOP + (trackH - th) * _scroll / (_count - VISIBLE_ROWS);
        tft->fillRect(W - 3, LIST_TOP, 2, trackH, t.bg);
        tft->fillRect(W - 3, ty, 2, th, t.rule);
    }
}

void CalendarScreen::drawDetail() {
    const Theme &t = ThemeManager::current();
    const int W = tft->width();
    const CalEvent &e = _events[_sel];
    const int maxW = W - 20;

    tft->setTextDatum(TL_DATUM);

    static const char* wd[] = { "Sunday", "Monday", "Tuesday", "Wednesday",
                                "Thursday", "Friday", "Saturday" };
    static const char* mo[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

    struct tm et; time_t s = e.start; gmtime_r(&s, &et);
    auto hhmm = [](time_t tt, char *b) {
        struct tm x; gmtime_r(&tt, &x);
        snprintf(b, 8, "%02d:%02d", x.tm_hour, x.tm_min);
    };

    char when[52];
    if (e.allDay) {
        snprintf(when, sizeof(when), "%s %d %s  -  All day",
                 wd[et.tm_wday % 7], et.tm_mday, mo[et.tm_mon % 12]);
    } else if (e.end > e.start) {
        char a[8], b[8]; hhmm(e.start, a); hhmm(e.end, b);
        snprintf(when, sizeof(when), "%s %d %s  %s-%s",
                 wd[et.tm_wday % 7], et.tm_mday, mo[et.tm_mon % 12], a, b);
    } else {
        char a[8]; hhmm(e.start, a);
        snprintf(when, sizeof(when), "%s %d %s  %s",
                 wd[et.tm_wday % 7], et.tm_mday, mo[et.tm_mon % 12], a);
    }

    // small helper to draw one clipped size-1 line and advance y
    int y = 26;
    auto line1 = [&](const String &str, uint16_t col) {
        String v = str;
        while (v.length() > 1 && tft->textWidth(v) > maxW) v.remove(v.length() - 1);
        tft->setTextSize(1);
        tft->setTextColor(col, t.bg);
        tft->setCursor(10, y);
        tft->print(v);
        y += 13;
    };

    line1(relLabel(e), t.accent);
    line1(when, t.fgDim);
    y += 5;

    // title - size 2, up to 2 wrapped lines
    tft->setTextSize(2);
    tft->setTextColor(t.fg, t.bg);
    {
        String rem = e.title[0] ? e.title : "(no title)";
        for (int ln = 0; ln < 2 && rem.length(); ln++) {
            int fit = rem.length();
            while (fit > 0 && tft->textWidth(rem.substring(0, fit)) > maxW) fit--;
            if (fit < (int)rem.length()) {
                int sp = rem.substring(0, fit).lastIndexOf(' ');
                if (sp > 0) fit = sp;
            }
            if (fit <= 0) fit = rem.length();
            tft->setCursor(10, y);
            tft->print(rem.substring(0, fit));
            rem = rem.substring(fit); rem.trim();
            y += 20;
        }
    }
    y += 4;

    if (e.location[0]) line1(String("@ ") + e.location, t.fgDim);

    if (e.guestCount > 0) {
        String g = e.guests;
        int shown = 0;
        if (g.length()) { shown = 1; for (int i = 0; i < (int)g.length(); i++) if (g[i] == ',') shown++; }
        int extra = e.guestCount - shown;
        char gl[120];
        if (shown && extra > 0)  snprintf(gl, sizeof(gl), "with %s  +%d", g.c_str(), extra);
        else if (shown)          snprintf(gl, sizeof(gl), "with %s", g.c_str());
        else                     snprintf(gl, sizeof(gl), "%d guest%s", e.guestCount, e.guestCount == 1 ? "" : "s");
        line1(gl, t.fgDim);
    }

    if (e.notes[0] && y < 180) {
        String rem = e.notes;
        for (int ln = 0; ln < 2 && rem.length() && y < 190; ln++) {
            int fit = rem.length();
            while (fit > 0 && tft->textWidth(rem.substring(0, fit)) > maxW) fit--;
            if (fit < (int)rem.length()) {
                int sp = rem.substring(0, fit).lastIndexOf(' ');
                if (sp > 8) fit = sp;
            }
            if (fit <= 0) fit = rem.length();
            tft->setTextSize(1);
            tft->setTextColor(t.fgDim, t.bg);
            tft->setCursor(10, y);
            tft->print(rem.substring(0, fit));
            rem = rem.substring(fit); rem.trim();
            y += 12;
        }
    }
}

// ---- input ----
void CalendarScreen::onEncoderUp() {
    if (_detail || _count == 0) return;
    if (_sel > 0) _sel--;
    if (_sel < _scroll) _scroll = _sel;
    _needsRedraw = true;
}

void CalendarScreen::onEncoderDown() {
    if (_detail || _count == 0) return;
    if (_sel < _count - 1) _sel++;
    if (_sel >= _scroll + VISIBLE_ROWS) _scroll = _sel - VISIBLE_ROWS + 1;
    _needsRedraw = true;
}

void CalendarScreen::onButtonPress() {
    if (_count == 0) return;
    _detail = !_detail;
    _needsRedraw = true;
}

void CalendarScreen::getActionLegend(String &line1, String &line2) const {
    if (_detail) {
        line1 = "";
        line2 = "o BACK";
    } else if (_count > 0) {
        line1 = "^v SCROLL EVENTS";
        line2 = "o DETAILS";
    } else {
        line1 = "";
        line2 = "";
    }
}
