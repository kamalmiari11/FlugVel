#include "calendar_api.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <string.h>

// ============================================================================
// iCal (.ics) feed reader.
//
// The feeds Google/iCloud/Outlook hand out contain the WHOLE calendar
// (years of history), typically 100 KB+ and served chunked, so nothing is
// buffered: the body is read through a chunked-aware byte reader that
// yields one unfolded iCal line at a time, and only SUMMARY / LOCATION /
// DTSTART are kept for VEVENTs that start inside the requested window.
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
    bool hasStart = false, hasRRule = false;

    String logical, raw;
    bool haveLogical = false;

    auto process = [&](const String &L) {
        if (L.startsWith("BEGIN:VEVENT")) {
            inEvent = true; cur = CalEvent(); hasStart = false; hasRRule = false;
            return;
        }
        if (!inEvent) return;

        if (L.startsWith("END:VEVENT")) {
            inEvent = false;
            if (hasStart && !hasRRule && cur.start >= from && cur.start <= to)
                insertSorted(out, count, maxEvents, cur);
            return;
        }
        if (L.startsWith("RRULE")) { hasRRule = true; return; }

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
    for (int i = 0; i < count; i++)
        Serial.printf("[Cal]   #%d start=%ld allDay=%d '%s' @'%s'\n",
                      i, (long)out[i].start, out[i].allDay, out[i].title, out[i].location);
    return count;
}
