#include "calendar_api.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <string.h>
#include <ctype.h>

// ============================================================================
// iCal (.ics) feed reader.
//
// The feeds Google/iCloud/Outlook hand out contain the WHOLE calendar
// (years of history), typically 100 KB+ and served chunked, so nothing is
// buffered: the body is read through a chunked-aware byte reader that
// yields one unfolded iCal line at a time, and only the handful of
// properties actually shown (SUMMARY / LOCATION / DTSTART / ...) are kept,
// for VEVENTs that start inside the requested window.
//
// A repeating event appears in the feed once, as a rule rather than as one
// entry per occurrence, so RRULE is expanded here into the individual
// occurrences that land in the window - see the RRule section below.
// ============================================================================

namespace {

// ---- chunked / content-length aware byte source ----
struct IcsReader {
    WiFiClient *client = nullptr;
    bool        chunked = false;
    long        remaining = 0;    // bytes left in the current chunk (or whole body in CL mode)
    bool        firstChunk = true;
    bool        expectTrailer = false; // a data chunk's closing CRLF still to consume
    bool        done = false;
    unsigned long deadline = 0;
    uint32_t     totalRead = 0;
    static const uint32_t MAX_TOTAL = 400000; // hard safety cap

    // Block until at least n bytes are buffered, or the stream ends / times out.
    bool waitFor(int n) {
        while (client->available() < n) {
            if (millis() > deadline) return false;
            if (!client->connected() && client->available() < n) return false;
            delay(2);
        }
        return true;
    }

    // Read one CRLF-terminated line straight off the socket (chunk-size line).
    // Returns the parsed hex size, or -1 on error / final (0) chunk.
    long readChunkSize() {
        String s;
        for (;;) {
            if (!waitFor(1)) return -1;
            char c = client->read();
            if (c == '\n') break;
            if (c != '\r') s += c;
            if (s.length() > 12) return -1;   // not a hex size line - stream desync
        }
        int semi = s.indexOf(';');
        if (semi >= 0) s = s.substring(0, semi);
        s.trim();
        if (s.length() == 0) return -1;
        return strtol(s.c_str(), nullptr, 16);
    }

    int rawByte() {
        if (done) return -1;
        for (;;) {
            if (millis() > deadline || totalRead >= MAX_TOTAL) { done = true; return -1; }

            // Serve a data byte if the current chunk still has one.
            if (remaining > 0) {
                if (!waitFor(1)) { done = true; return -1; }
                remaining--;
                totalRead++;
                return client->read();
            }

            if (!chunked) { done = true; return -1; }   // content-length body exhausted

            // Chunk boundary: consume the trailing CRLF of the previous chunk,
            // then read the next size line. expectTrailer makes this safe to
            // re-enter after a partial-buffer wait (no double-consume).
            if (expectTrailer) {
                if (!waitFor(2)) { done = true; return -1; }
                client->read(); client->read();     // \r \n
                expectTrailer = false;
            }
            firstChunk = false;
            long sz = readChunkSize();
            if (sz <= 0) { done = true; return -1; } // final chunk or desync
            remaining = sz;
            expectTrailer = true;
            // loop -> next pass serves the first byte of this chunk
        }
    }

    // one logical raw line (CR/LF stripped); false at end of stream
    bool rawLine(String &out) {
        out = "";
        int c;
        bool any = false;
        while ((c = rawByte()) >= 0) {
            any = true;
            if (c == '\n') return true;
            if (c == '\r') continue;
            if (out.length() < 400) out += (char)c;  // long DESCRIPTION lines clip - we don't keep them
        }
        return any;
    }
};

// ---- date helpers (no timezone / mktime dependency) ----
// Howard Hinnant's days-from-civil.
long daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097L + (long)doe - 719468L;
}

time_t civilToEpoch(int y, int mo, int d, int h, int mi, int s) {
    return (time_t)(daysFromCivil(y, mo, d) * 86400LL + h * 3600 + mi * 60 + s);
}

// The inverse - Hinnant's civil-from-days. Needed by the recurrence expander,
// which has to do its arithmetic on calendar fields ("the same day next
// month") rather than on epoch seconds, since months and years are not a
// fixed number of them.
void civilFromDays(long z, int &y, int &m, int &d) {
    z += 719468L;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned long doe = (unsigned long)(z - era * 146097L);
    unsigned long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long yr = (long)yoe + era * 400;
    unsigned long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned long mp = (5 * doy + 2) / 153;
    unsigned long dd = doy - (153 * mp + 2) / 5 + 1;
    unsigned long mm = mp + (mp < 10 ? 3 : -9);
    y = (int)(yr + (mm <= 2));
    m = (int)mm;
    d = (int)dd;
}

// 0 = Sunday. 1970-01-01 is day 0 and was a Thursday, hence the +11 before
// the modulo (which also keeps it correct for negative day numbers).
int weekdayFromDays(long z) { return (int)((z % 7 + 11) % 7); }

int daysInMonth(int y, int m) {
    static const int len[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (m == 2) {
        bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
        return leap ? 29 : 28;
    }
    return len[m - 1];
}

// Current local UTC offset in seconds (this newlib build has no tm_gmtoff):
// the gap between the local wall clock and the UTC wall clock right now.
long localUtcOffset() {
    time_t now = time(nullptr);
    struct tm lt, gt;
    localtime_r(&now, &lt);
    gmtime_r(&now, &gt);
    long l = (long)civilToEpoch(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
    long g = (long)civilToEpoch(gt.tm_year + 1900, gt.tm_mon + 1, gt.tm_mday, gt.tm_hour, gt.tm_min, gt.tm_sec);
    return l - g;
}

// Parse a DTSTART value ("YYYYMMDD" or "YYYYMMDDTHHMMSS[Z]"); paramsHadDate
// = the property had ";VALUE=DATE".
bool parseDtStart(const String &params, const String &value, time_t &out, bool &allDay) {
    String v = value;
    v.trim();
    bool dateOnly = params.indexOf("VALUE=DATE") >= 0 || v.indexOf('T') < 0;

    if (v.length() < 8) return false;
    int y  = v.substring(0, 4).toInt();
    int mo = v.substring(4, 6).toInt();
    int d  = v.substring(6, 8).toInt();
    if (y < 2000 || mo < 1 || mo > 12 || d < 1 || d > 31) return false;

    if (dateOnly) {
        out = civilToEpoch(y, mo, d, 0, 0, 0);
        allDay = true;
        return true;
    }

    int h = 0, mi = 0, s = 0;
    if (v.length() >= 15) {   // ...T HH MM SS
        h  = v.substring(9, 11).toInt();
        mi = v.substring(11, 13).toInt();
        s  = v.substring(13, 15).toInt();
    }

    time_t e = civilToEpoch(y, mo, d, h, mi, s);

    // Google usually writes personal events as ";TZID=...:...T..." (floating
    // local wall time, which we keep as-is and display raw). But a trailing
    // 'Z' means true UTC - shift it by the device's local offset so it also
    // displays as the right local wall time.
    bool endsZ = v.length() && (v[v.length() - 1] == 'Z' || v[v.length() - 1] == 'z');
    if (endsZ) e += localUtcOffset();

    out = e;
    allDay = false;
    return true;
}

// Unescape an iCal TEXT value ("\," "\;" "\\" "\n") straight into a fixed
// char buffer - no heap. Trims surrounding whitespace.
void unescapeInto(const String &in, char *out, size_t cap) {
    size_t w = 0;
    int n = in.length();
    for (int i = 0; i < n && w + 1 < cap; i++) {
        char c = in[i];
        if (c == '\\' && i + 1 < n) {
            char e = in[++i];
            out[w++] = (e == 'n' || e == 'N') ? ' ' : e;
        } else {
            out[w++] = c;
        }
    }
    out[w] = 0;
    // trim
    char *s = out;
    while (*s == ' ' || *s == '\t') s++;
    if (s != out) memmove(out, s, strlen(s) + 1);
    for (int i = (int)strlen(out) - 1; i >= 0 && (out[i] == ' ' || out[i] == '\t'); i--)
        out[i] = 0;
}

// One ATTENDEE line -> bump guestCount and (if it fits) append the name.
// nameParams is the part before ':' (holds "CN=..."), val is after (mailto:).
void addAttendee(CalEvent &e, const String &nameParams, const String &val) {
    e.guestCount++;

    char nm[36] = "";
    int p = nameParams.indexOf("CN=");
    if (p >= 0) {
        String s = nameParams.substring(p + 3);
        if (s.length() && s[0] == '"') {           // CN="Doe, Jane"
            s = s.substring(1);
            int q = s.indexOf('"');
            if (q >= 0) s = s.substring(0, q);
        } else {
            int sc = s.indexOf(';');               // CN=Jane Doe;NEXT=...
            if (sc >= 0) s = s.substring(0, sc);
        }
        s.trim();
        s.toCharArray(nm, sizeof(nm));
    }
    if (nm[0] == 0) {                                // no CN - use email local-part
        String m = val;
        int c = m.indexOf(':');                      // "mailto:jane@x.com"
        if (c >= 0) m = m.substring(c + 1);
        int at = m.indexOf('@');
        if (at > 0) m = m.substring(0, at);
        m.trim();
        m.toCharArray(nm, sizeof(nm));
    }
    if (nm[0] == 0) return;

    size_t have = strlen(e.guests);
    size_t need = strlen(nm) + (have ? 2 : 0);
    if (have + need + 1 < sizeof(e.guests)) {
        if (have) strcat(e.guests, ", ");
        strcat(e.guests, nm);
    }
}

// ---- RRULE ----
// Supports what people actually create in Google/Outlook: FREQ with an
// INTERVAL, weekly-on-specific-days, monthly/yearly on either a day of the
// month or an nth weekday ("third Tuesday", "last Friday"), bounded by UNTIL
// or COUNT, with individually deleted occurrences honoured via EXDATE.
//
// Deliberately NOT supported: BYMONTH / BYMONTHDAY lists, BYSETPOS, WKST
// (Monday is assumed, which is its default), and BYYEARDAY / BYWEEKNO. Those
// appear almost exclusively in machine-generated calendars, and a rule using
// one is expanded on its remaining fields rather than being dropped - showing
// an event slightly too often is a better failure than the silent nothing
// this code used to produce for every repeating event.
struct RRule {
    enum Freq { NONE, DAILY, WEEKLY, MONTHLY, YEARLY };
    Freq    freq = NONE;
    int     interval = 1;
    long    count = 0;        // 0 = unbounded
    time_t  until = 0;        // 0 = unbounded
    uint8_t byDayMask = 0;    // weekly: bit 0 = Sunday .. bit 6 = Saturday
    int     byDayOrdinal = 0; // monthly/yearly "3TU" / "-1FR"; 0 = unused
    int     byDayWeekday = -1;
};

int weekdayCode(const String &s) {
    static const char *names[] = { "SU","MO","TU","WE","TH","FR","SA" };
    for (int i = 0; i < 7; i++) if (s == names[i]) return i;
    return -1;
}

void parseRRule(const String &val, RRule &rr) {
    int pos = 0;
    while (pos < (int)val.length()) {
        int semi = val.indexOf(';', pos);
        String part = (semi < 0) ? val.substring(pos) : val.substring(pos, semi);
        pos = (semi < 0) ? val.length() : semi + 1;

        int eq = part.indexOf('=');
        if (eq < 0) continue;
        String key = part.substring(0, eq);
        String v   = part.substring(eq + 1);
        key.trim(); v.trim();

        if (key == "FREQ") {
            if      (v == "DAILY")   rr.freq = RRule::DAILY;
            else if (v == "WEEKLY")  rr.freq = RRule::WEEKLY;
            else if (v == "MONTHLY") rr.freq = RRule::MONTHLY;
            else if (v == "YEARLY")  rr.freq = RRule::YEARLY;
            // HOURLY/MINUTELY/SECONDLY stay NONE - nothing a person puts on
            // a wall calendar, and expanding one would flood the list.
        } else if (key == "INTERVAL") {
            rr.interval = v.toInt();
            if (rr.interval < 1) rr.interval = 1;
        } else if (key == "COUNT") {
            rr.count = v.toInt();
        } else if (key == "UNTIL") {
            bool dummy;
            time_t u;
            if (parseDtStart("", v, u, dummy)) rr.until = u;
        } else if (key == "BYDAY") {
            // Either a plain list ("MO,WE,FR") for weekly, or a single
            // ordinal form ("3TU", "-1FR") for monthly/yearly.
            int p = 0;
            while (p < (int)v.length()) {
                int comma = v.indexOf(',', p);
                String tok = (comma < 0) ? v.substring(p) : v.substring(p, comma);
                p = (comma < 0) ? v.length() : comma + 1;
                tok.trim();
                if (tok.length() < 2) continue;

                int sign = 1, numStart = 0;
                if (tok[0] == '+' || tok[0] == '-') {
                    sign = (tok[0] == '-') ? -1 : 1;
                    numStart = 1;
                }
                int digits = 0;
                while (numStart + digits < (int)tok.length() && isdigit(tok[numStart + digits])) digits++;

                if (digits > 0) {
                    rr.byDayOrdinal = sign * tok.substring(numStart, numStart + digits).toInt();
                    rr.byDayWeekday = weekdayCode(tok.substring(numStart + digits));
                } else {
                    int wd = weekdayCode(tok);
                    if (wd >= 0) rr.byDayMask |= (uint8_t)(1 << wd);
                }
            }
        }
    }
}

// EXDATE holds the start times of occurrences the owner deleted one by one.
// A modest fixed cap: past the cap a stale occurrence may reappear, which is
// far better than the alternatives on a device with no heap to spare.
static const int MAX_EXDATES = 24;

void parseExDates(const String &params, const String &val, time_t *ex, int &exCount) {
    int p = 0;
    while (p < (int)val.length() && exCount < MAX_EXDATES) {
        int comma = val.indexOf(',', p);
        String tok = (comma < 0) ? val.substring(p) : val.substring(p, comma);
        p = (comma < 0) ? val.length() : comma + 1;

        time_t t;
        bool dummy;
        if (parseDtStart(params, tok, t, dummy)) ex[exCount++] = t;
    }
}

// An occurrence is excluded if an EXDATE matches it. Compared by calendar day
// rather than exactly: a feed may write its EXDATEs as dates while DTSTART
// carries a time, or in a different timezone form, and an off-by-some-hours
// mismatch would resurrect an event the owner deleted.
bool isExcluded(time_t occ, const time_t *ex, int exCount) {
    long occDay = (long)(occ / 86400);
    for (int i = 0; i < exCount; i++) {
        if (ex[i] == occ) return true;
        if ((long)(ex[i] / 86400) == occDay) return true;
    }
    return false;
}

void insertSorted(CalEvent *out, int &count, int maxEvents, const CalEvent &e) {
    if (count >= maxEvents && e.start >= out[count - 1].start) return; // later than everything we keep

    int pos = (count < maxEvents) ? count : maxEvents - 1;
    if (count < maxEvents) count++;
    while (pos > 0 && out[pos - 1].start > e.start) {
        out[pos] = out[pos - 1];
        pos--;
    }
    out[pos] = e;
}

// True if an event with this UID already sits in `out` on the same calendar
// day. A feed represents a single edited occurrence of a repeating event as
// its own VEVENT carrying RECURRENCE-ID, alongside the unchanged master rule
// - so without this check, "the 14th moved to 3pm" would show twice: once
// from the override, once generated from the rule. Whichever of the two is
// parsed first wins the slot, and the other is dropped.
bool alreadyHave(const CalEvent *out, int count, uint32_t uidHash, time_t start) {
    if (uidHash == 0) return false;
    long day = (long)(start / 86400);
    for (int i = 0; i < count; i++)
        if (out[i].uidHash == uidHash && (long)(out[i].start / 86400) == day) return true;
    return false;
}

// Walks a rule's occurrences in order and keeps the ones landing inside
// [from, to]. Iteration always starts at DTSTART rather than skipping ahead
// to the window, because COUNT is defined from the first occurrence - but
// each step is integer date maths, and the caps below bound the worst case
// (a decade-old daily event) to a few thousand trivial iterations.
void expandRecurring(const CalEvent &base, const RRule &rr,
                     const time_t *ex, int exCount,
                     time_t from, time_t to,
                     CalEvent *out, int &count, int maxEvents) {
    if (rr.freq == RRule::NONE) return;

    long  baseDay  = (long)(base.start / 86400);
    long  timeOfDay = (long)(base.start - (time_t)baseDay * 86400);
    int   by, bm, bd;
    civilFromDays(baseDay, by, bm, bd);

    long emitted = 0;   // counts occurrences against COUNT, window or not
    time_t limit = to;
    if (rr.until > 0 && rr.until < limit) limit = rr.until;

    auto consider = [&](long day) -> bool {   // false = stop iterating
        time_t occ = (time_t)day * 86400 + timeOfDay;
        if (occ < base.start) return true;            // before the series began
        if (occ > limit) return false;
        emitted++;
        if (rr.count > 0 && emitted > rr.count) return false;

        if (occ >= from && !isExcluded(occ, ex, exCount) &&
            !alreadyHave(out, count, base.uidHash, occ)) {
            CalEvent e = base;
            if (base.end > base.start) e.end = occ + (base.end - base.start);
            e.start = occ;
            insertSorted(out, count, maxEvents, e);
        }
        return true;
    };

    switch (rr.freq) {
        case RRule::DAILY: {
            static const int MAX_STEPS = 4000;
            for (int i = 0; i < MAX_STEPS; i++)
                if (!consider(baseDay + (long)i * rr.interval)) break;
            break;
        }

        case RRule::WEEKLY: {
            // Every day from the start, kept when it falls on one of the
            // rule's weekdays in a week the interval actually covers. Weeks
            // are counted from the Monday of DTSTART's week (WKST defaults to
            // Monday and is not parsed).
            uint8_t mask = rr.byDayMask;
            if (mask == 0) mask = (uint8_t)(1 << weekdayFromDays(baseDay));  // "weekly" with no BYDAY = DTSTART's own day

            int  baseWd    = weekdayFromDays(baseDay);
            long baseMonday = baseDay - ((baseWd + 6) % 7);

            static const int MAX_STEPS = 4000;
            for (int i = 0; i < MAX_STEPS; i++) {
                long day = baseDay + i;
                int  wd  = weekdayFromDays(day);
                if (!(mask & (1 << wd))) continue;

                long monday = day - ((wd + 6) % 7);
                if (((monday - baseMonday) / 7) % rr.interval != 0) continue;

                if (!consider(day)) break;
            }
            break;
        }

        case RRule::MONTHLY:
        case RRule::YEARLY: {
            bool yearly = (rr.freq == RRule::YEARLY);
            static const int MAX_STEPS_M = 600;   // 50 years of monthlies
            static const int MAX_STEPS_Y = 100;
            int steps = yearly ? MAX_STEPS_Y : MAX_STEPS_M;

            for (int i = 0; i < steps; i++) {
                int y = by, m = bm;
                if (yearly) {
                    y += i * rr.interval;
                } else {
                    long total = (long)(bm - 1) + (long)i * rr.interval;
                    y = by + (int)(total / 12);
                    m = (int)(total % 12) + 1;
                }

                long day;
                if (rr.byDayOrdinal != 0 && rr.byDayWeekday >= 0) {
                    // "the Nth <weekday> of the month", or "-1" for the last.
                    int dim = daysInMonth(y, m);
                    if (rr.byDayOrdinal > 0) {
                        long first = daysFromCivil(y, m, 1);
                        int  shift = (rr.byDayWeekday - weekdayFromDays(first) + 7) % 7;
                        long target = first + shift + (long)(rr.byDayOrdinal - 1) * 7;
                        if (target > daysFromCivil(y, m, dim)) continue;  // no 5th Tuesday this month
                        day = target;
                    } else {
                        long last  = daysFromCivil(y, m, dim);
                        int  shift = (weekdayFromDays(last) - rr.byDayWeekday + 7) % 7;
                        long target = last - shift + (long)(rr.byDayOrdinal + 1) * 7;
                        if (target < daysFromCivil(y, m, 1)) continue;
                        day = target;
                    }
                } else {
                    // Same day of the month as DTSTART. A 31st simply does
                    // not occur in a 30-day month, which is what RFC 5545
                    // says should happen (that occurrence is skipped, not
                    // moved), and matches what Google shows.
                    if (bd > daysInMonth(y, m)) continue;
                    day = daysFromCivil(y, m, bd);
                }

                if (!consider(day)) break;
            }
            break;
        }

        default: break;
    }
}

} // namespace

int fetchCalendarEvents(CalEvent *out, int maxEvents, const String &icsUrl, int windowDays) {
    if (icsUrl.length() < 8 || WiFi.status() != WL_CONNECTED) return -1;

    time_t nowT = time(nullptr);
    if (nowT < 1600000000) return -1;   // clock not synced yet
    time_t from = nowT - 12 * 3600;                 // include things earlier today
    time_t to   = nowT + (time_t)windowDays * 86400;

    Serial.printf("[Cal] Fetching %s\n", icsUrl.c_str());

    HTTPClient http;
    http.setTimeout(12000);
    http.setConnectTimeout(8000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(icsUrl)) return -1;
    http.setUserAgent("FlugVel/1.0 (ESP32)");
    http.addHeader("Accept", "text/calendar");
    const char *wantHeaders[] = { "Content-Type" };
    http.collectHeaders(wantHeaders, 1);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[Cal] HTTP %d\n", code);
        http.end();
        return -1;
    }

    // Guard against being handed a web page (e.g. the ".../calendar/u/0?cid="
    // "add to my calendar" link) instead of the iCal feed - otherwise we'd
    // stream a few hundred KB of HTML and find no events.
    String ctype = http.header("Content-Type");
    if (ctype.indexOf("html") >= 0) {
        Serial.printf("[Cal] not an iCal feed (Content-Type: %s)\n", ctype.c_str());
        http.end();
        return -2;   // "wrong kind of URL"
    }

    IcsReader r;
    r.client    = http.getStreamPtr();
    r.deadline  = millis() + 12000;   // whole-body read budget - bail rather than freeze the UI longer
    int len     = http.getSize();
    r.chunked   = (len < 0);
    r.remaining = r.chunked ? 0 : len;

    int count = 0;
    bool inEvent = false;
    CalEvent cur;
    bool hasStart = false;

    RRule  curRule;
    time_t curEx[MAX_EXDATES];
    int    curExCount = 0;

    // Why events were dropped, reported at the end. "0 events" on a feed the
    // owner knows has entries in it is otherwise unanswerable from the
    // outside: every reason below looks identical from the serial log, and
    // they call for completely different fixes (recurring events are not
    // expanded at all, an unparsed DTSTART is a feed-format problem, and
    // everything landing outside the window usually means a stale clock).
    int seenEvents = 0, recurring = 0, skipNoStart = 0, skipBefore = 0, skipAfter = 0;

    String logical, raw;
    bool haveLogical = false;

    auto process = [&](const String &L) {
        if (L.startsWith("BEGIN:VEVENT")) {
            inEvent = true;
            cur = CalEvent();
            hasStart = false;
            curRule = RRule();
            curExCount = 0;
            return;
        }
        if (!inEvent) return;

        if (L.startsWith("END:VEVENT")) {
            inEvent = false;
            seenEvents++;
            if (!hasStart) {
                skipNoStart++;
            } else if (curRule.freq != RRule::NONE) {
                recurring++;
                expandRecurring(cur, curRule, curEx, curExCount, from, to, out, count, maxEvents);
            } else if (cur.start < from) {
                skipBefore++;
            } else if (cur.start > to) {
                skipAfter++;
            } else if (!alreadyHave(out, count, cur.uidHash, cur.start)) {
                insertSorted(out, count, maxEvents, cur);
            }
            return;
        }

        int colon = L.indexOf(':');
        if (colon < 0) return;
        String name = L.substring(0, colon);   // "DTSTART;VALUE=DATE" etc
        String val  = L.substring(colon + 1);

        if (name == "UID" || name.startsWith("UID;")) {
            // FNV-1a. Two feeds syncing the same underlying event (a Google
            // calendar also subscribed to in Outlook, say) normally keep the
            // UID intact even when titles differ by whitespace, which makes
            // it a far better merge key than the text.
            uint32_t h = 2166136261u;
            for (size_t i = 0; i < val.length(); i++) {
                h ^= (uint8_t)val[i];
                h *= 16777619u;
            }
            cur.uidHash = h;
        } else if (name == "SUMMARY" || name.startsWith("SUMMARY;")) {
            unescapeInto(val, cur.title, sizeof(cur.title));
        } else if (name == "LOCATION" || name.startsWith("LOCATION;")) {
            unescapeInto(val, cur.location, sizeof(cur.location));
        } else if (name == "DESCRIPTION" || name.startsWith("DESCRIPTION;")) {
            if (cur.notes[0] == 0) unescapeInto(val, cur.notes, sizeof(cur.notes));
        } else if (name == "ATTENDEE" || name.startsWith("ATTENDEE;")) {
            addAttendee(cur, name, val);
        } else if (name == "DTSTART" || name.startsWith("DTSTART;")) {
            int semi = name.indexOf(';');
            String params = semi >= 0 ? name.substring(semi + 1) : "";
            if (parseDtStart(params, val, cur.start, cur.allDay)) hasStart = true;
        } else if (name == "RRULE" || name.startsWith("RRULE;")) {
            parseRRule(val, curRule);
        } else if (name == "EXDATE" || name.startsWith("EXDATE;")) {
            int semi = name.indexOf(';');
            parseExDates(semi >= 0 ? name.substring(semi + 1) : "", val, curEx, curExCount);
        } else if (name == "DTEND" || name.startsWith("DTEND;")) {
            int semi = name.indexOf(';');
            String params = semi >= 0 ? name.substring(semi + 1) : "";
            bool dummy;
            parseDtStart(params, val, cur.end, dummy);
        }
    };

    while (r.rawLine(raw)) {
        if (raw.length() && (raw[0] == ' ' || raw[0] == '\t')) {     // folded continuation
            if (haveLogical && logical.length() < 300) logical += raw.substring(1);
            continue;
        }
        if (haveLogical) process(logical);
        logical = raw;
        haveLogical = true;
    }
    if (haveLogical) process(logical);

    http.end();
    Serial.printf("[Cal] %d events in the next %d days (read %u bytes, free heap %u)\n",
                  count, windowDays, (unsigned)r.totalRead, (unsigned)ESP.getFreeHeap());
    Serial.printf("[Cal]   %d VEVENTs seen (%d recurring, expanded); skipped %d undated, %d past, %d beyond window\n",
                  seenEvents, recurring, skipNoStart, skipBefore, skipAfter);
    if (seenEvents == 0)
        Serial.println("[Cal]   no VEVENTs at all - the body was empty, truncated, or not an iCal feed");
    for (int i = 0; i < count; i++)
        Serial.printf("[Cal]   #%d start=%ld allDay=%d '%s' @'%s'\n",
                      i, (long)out[i].start, out[i].allDay, out[i].title, out[i].location);
    return count;
}
