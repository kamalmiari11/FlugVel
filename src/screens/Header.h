#pragma once
#include <TFT_eSPI.h>
#include <time.h>
#include <Arduino.h>

// Header is the persistent top status bar.
// It only actually repaints when something visible has changed (title,
// screen index/count, or the displayed minute) - see draw(). Constantly
// redrawing an unchanged 240x20 strip on every single loop() iteration
// (which can run thousands of times a second) was producing a lot of
// pointless SPI traffic and visible flicker across the panel for no
// visual benefit, since the pixels never actually changed in between.
// Seconds are left out of the displayed time - see draw().
class Header {
public:
    Header(TFT_eSPI* display);

    // Initialize header using user-selected timezone (IANA name, e.g.
    // "Europe/Amsterdam" - converted internally to a POSIX TZ rule string).
    void begin(const String &timezone);

    // Re-point the clock at a different zone without begin()'s NTP wait.
    // begin() blocks for up to ten seconds polling for a sync, which is
    // fine once at boot and unacceptable from loop() - the time is already
    // synced by then, only the zone rule is changing.
    void setTimezone(const String &timezone);

    // Draw the header bar: "[ TITLE ]" on the left, date + time in the
    // middle-right, and the dot pager (one dot per top-level screen,
    // current one filled) at the far right. Pass currentScreenIndex < 0
    // (the default) to skip the dot pager, e.g. for contexts with no
    // meaningful "which of N screens" (there are none right now, since
    // every caller is mid-cycle, but it keeps this safe to call from
    // anywhere). Only actually repaints when something visible changed
    // (title, screen index/count, or the displayed minute) - cheap to
    // call every loop iteration either way.
    void draw(const String &title = "", int currentScreenIndex = -1, int screenCount = 0);

    int height() const { return headerHeight; }

    // Forces the next draw() call to actually repaint, even if title/
    // index/count/time all look unchanged from the last draw. Call this
    // whenever something outside Header's knowledge may have overwritten
    // its strip - e.g. ScreenManager calls this on every screen switch,
    // since the incoming screen's init() typically does a full-screen
    // wipe that would otherwise leave the header blank until something
    // about it actually changes.
    void invalidate() { _drawnOnce = false; }

private:
    TFT_eSPI* tft;
    String _timezone;               // store user-selected IANA timezone
    const int headerHeight = 20;

    // Only repaint when something visible actually changed - see draw().
    String _lastTitle;
    int _lastScreenIndex = -2;
    int _lastScreenCount = -1;
    String _lastTimeString;
    bool _drawnOnce = false;

    // configTzTime()/setenv("TZ",...) need a POSIX TZ rule string with
    // explicit DST transition dates (e.g. "CET-1CEST,M3.5.0,M10.5.0/3") -
    // they do NOT understand IANA zone names like "Europe/Amsterdam" at
    // all. Passing an IANA name in silently fails to parse and the clock
    // falls back to plain UTC, which is exactly why the display was
    // showing a time that's a fixed number of hours behind (no DST, no
    // offset applied). This looks up the right POSIX rule for the zones
    // we actually offer (see CaptivePortal.cpp's timezones[] dropdown,
    // plus whatever ip-api.com/Open-Meteo commonly return), falling back
    // to plain UTC0 only if we truly don't recognize the zone.
    static String posixTzFromIana(const String &iana);
};