#pragma once
#include "Screen.h"
#include "../api/flight_api.h"
#include "Header.h"

class PlaneTrackerScreen : public Screen {
public:
    PlaneTrackerScreen(TFT_eSPI* display);
    
    void init() override;
    void update() override;
    void draw() override;
    void onButtonPress() override;
    const char* getName() const override { return "Planes"; }
    ScreenId id() const override { return ScreenId::Plane; }
    void getActionLegend(String &line1, String &line2) const override;

    // This screen has nothing meaningful to put in the bottom action-
    // legend bar (see getActionLegend() above - both lines are always
    // empty) and no spare room for it, so it opts out entirely and uses
    // that space for its own content instead.
    bool wantsActionLegend() const override { return false; }


    // ScreenManager keeps this in sync with the live cycle order (the user
    // can change the home screen, which shifts every screen's dot), so the
    // header this screen repaints mid-animation shows the right pager dot.
    void setPagerPosition(int idx) override { _pagerIndex = idx; }
    void setPagerCount(int count) override { _pagerCount = count; }
    
    // Set the current flight data
    void setFlight(const Flight& flight);
    bool hasFlight() const { return _hasFlight; }

    // Call when a flight check comes back with nothing nearby, so the
    // screen falls back to the bouncing-text state instead of leaving the
    // last flight's info stuck on screen forever.
    void clearFlight();

    // Animation control
    void triggerAnimation();

    // So animatePlane()'s blocking frame loop can keep the clock/title bar
    // ticking during the (multi-second) flyover instead of freezing it
    // until the animation finishes - see animatePlane().
    void setHeader(Header* header) { _header = header; }

    // Optional overrides from the captive portal's manual setup form - both
    // no-ops (keep the built-in default) if passed an empty/invalid value,
    // so "leave the field blank" naturally means "use the default".
    void setBounceText(const String& text);   // up to 3 letters, e.g. "ABC" - default "KML"
    void setDisplayBearing(float bearingDeg); // 0-360, which way the screen faces - default 270 (west)

    // "Quote of the day", drawn in the strip of space below the border box.
    // Safe to call any time after this screen has been constructed (even
    // before init() has run the first time) - it just stores the values;
    // actual drawing only happens once border geometry is known.
    void setQuote(const String& text, const String& author);

private:
    Flight _currentFlight;
    bool _hasFlight;
    bool _showAnimation;
    unsigned long _animationStartTime;
    unsigned long _lastFlightTime;
    String _lastDrawnFlight;  // Track what was last drawn to prevent flickering
    String _bounceLabel;      // text shown bouncing in the no-flight state, default "KML"

    String _quoteText;        // "quote of the day" - empty until the first successful fetch
    String _quoteAuthor;

    Header* _header = nullptr; // not owned - used to keep the header alive mid-animation
    int _pagerIndex = 0;
    int _pagerCount = 0;

    // Bouncing text (for no flights state)
    int kmlX, kmlY, kmlDX, kmlDY;
    unsigned long lastKmlUpdate;

    void drawFlightInfo();
    void updateKMLPosition();
    void animatePlane();
    void drawBorder();
    void clearContentArea(); // clears only the interior of the border box - never the header, never the border itself
    void drawQuote();        // draws _quoteText/_quoteAuthor in the space below the border box
    float getRelativeHeading(float h) const;

    // The shared top status bar (Header class) owns this many pixels at the
    // top of every screen and redraws itself every loop - this screen must
    // never fillScreen/fillRect over that area, or it flickers away and
    // has to be redrawn by ScreenManager afterward.
    static const int HEADER_HEIGHT = 20;

    // Border box geometry, computed once in init() from the actual panel
    // size and never recomputed/redrawn after that - it's drawn exactly
    // once per visit to this screen and every clear operation below stays
    // strictly inside it, so the border line itself is never touched again.
    int _borderX, _borderY, _borderW, _borderH;
    bool _borderDrawn;

    // Bounce-area margins for the "no flight" text animation, and for the
    // border box drawn around exactly this region. Left/right kept small
    // (not 0) so the border line itself isn't drawn flush against the
    // physical panel edge, which some displays clip/vignette slightly.
    static const int KML_MARGIN_LEFT   = 4;
    static const int KML_MARGIN_RIGHT  = 4;
    static const int KML_MARGIN_TOP    = 30;
    // This is NOT space for the bottom action-legend bar (this screen has
    // none - see wantsActionLegend() below) - it's the room reserved below
    // the border box for the quote-of-the-day strip (see drawQuote()),
    // same as it always was.
    static const int KML_MARGIN_BOTTOM = 50;

    // Desk/display orientation, same idea as the OLED build: rotate the
    // plane's real-world heading so "up" on screen means "away from you".
    // 0 = display's top edge faces true North. Defaults to 270 (west);
    // overridable via setDisplayBearing() from the captive portal form.
    float _displayBearing;
};