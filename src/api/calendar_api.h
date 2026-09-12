#pragma once
#include <Arduino.h>
#include <time.h>

// One calendar entry, parsed from an iCal (.ics) feed. Fixed char buffers,
// no String members: the parse runs on a heap that other background tasks
// have already fragmented, and String ops fail silently there.
struct CalEvent {
    char   title[64];
    char   location[72];
    char   guests[80];   // comma-separated first few invitee names ("" = none)
    char   notes[128];   // start of the DESCRIPTION, unescaped + truncated
    int    guestCount;   // total ATTENDEEs (may exceed the names that fit in `guests`)
    time_t start;        // unix epoch. All-day events: 00:00 of the date.
    time_t end;          // 0 if the feed didn't give one
    bool   allDay;

    // Hash of the feed's UID property, for spotting the same event arriving
    // from two calendars. Stored as a hash rather than the UID text because
    // real UIDs run to 60+ characters and this struct is held in an array
    // of 20 - that would be over a kilobyte of RAM for a field only ever
    // used in an equality test. 0 means the feed gave no UID.
    uint32_t uidHash;

    // Which configured feed this came from (index into CalendarFeeds).
    uint8_t  feedIndex;

    CalEvent() : guestCount(0), start(0), end(0), allDay(false),
                 uidHash(0), feedIndex(0) {
        title[0] = 0; location[0] = 0; guests[0] = 0; notes[0] = 0;
    }
};

// Fetch upcoming events from an iCal feed URL (Google / iCloud / Outlook
// "secret address in iCal format"), keeping only those starting within
// roughly [now, now + windowDays]. Fills `out` (up to maxEvents), sorted
// ascending by start time.
//
// Recurring events are expanded: FREQ (daily/weekly/monthly/yearly) with
// INTERVAL, weekly-on-specific-days, monthly/yearly on either a day of the
// month or an nth weekday ("third Tuesday", "last Friday"), bounded by UNTIL
// or COUNT, with individually deleted occurrences honoured via EXDATE. Each
// occurrence inside the window is returned as its own CalEvent. Rules using
// BYMONTH / BYMONTHDAY lists, BYSETPOS or a non-Monday WKST are expanded on
// their remaining fields rather than dropped, so such an event may appear
// slightly more often than it should.
//
// Returns the number of events written (>= 0), -1 on failure (empty URL,
// network / HTTP error), or -2 if the URL returned a web page rather than
// an iCal feed (wrong link pasted). On a negative result the caller should
// keep whatever it had.
int fetchCalendarEvents(CalEvent *out, int maxEvents, const String &icsUrl, int windowDays);
