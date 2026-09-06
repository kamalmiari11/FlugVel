#pragma once
#include <Arduino.h>

// Up to four read-only iCal feeds, merged into one agenda on the Calendar
// screen.
//
// WHY NOT IN Config: Config is a fixed-size POD blob, and a feed URL is a
// couple of hundred characters ("secret address in iCal format" links are
// long). Four of those inline would roughly quadruple the blob for data
// that is written once and read once per refresh. These live as their own
// NVS string keys instead, in the same namespace.
//
// The old single-URL setting (CaptivePortal::getCalendarUrl) is migrated
// into slot 0 the first time begin() finds no feed list, so an existing
// device keeps its calendar without the user touching anything.

static const int CAL_MAX_FEEDS = 4;

struct CalFeed {
    String name;     // short label, shown as an initial beside merged events
    String url;      // https://.../basic.ics
    bool   enabled;

    CalFeed() : enabled(true) {}
    bool usable() const { return enabled && url.length() >= 8; }
};

namespace CalendarFeeds {

    // Load from NVS, migrating the legacy single URL if that is all there
    // is. Safe to call more than once.
    void begin(const String &legacySingleUrl);

    int  count();                    // how many slots are defined (0-4)
    const CalFeed& at(int index);    // index < count()

    // Number of feeds that are both enabled and have a URL - i.e. how many
    // will actually be fetched. The Calendar screen shows a source marker
    // per event only when this is more than one.
    int usableCount();

    // Editing. None of these write to flash; call save() when done.
    void set(int index, const String &name, const String &url, bool enabled);
    bool add(const String &name, const String &url);   // false if full
    void remove(int index);
    void clear();

    void save();

} // namespace CalendarFeeds
