#pragma once
#include <TFT_eSPI.h>
#include <Arduino.h>
#include "ScreenId.h"

// Base class for all screens
class Screen {
public:
    Screen(TFT_eSPI* display) : tft(display) {}
    virtual ~Screen() = default;

    // Initialize screen (called once when entering)
    virtual void init() = 0;

    // Update screen state (called every loop iteration)
    virtual void update() = 0;

    // Draw screen (called every loop)
    virtual void draw() = 0;

    // Handle encoder up/down input
    virtual void onEncoderUp() {}
    virtual void onEncoderDown() {}

    // Handle button press
    virtual void onButtonPress() {}

    // Get screen name for debugging
    virtual const char* getName() const = 0;

    // Called on EVERY screen after the saved config changed, not just the
    // one showing. Most settings are read at draw time and need nothing
    // here; this is for the ones a screen latched at construction, or that
    // only take effect on the next fetch. Default: nothing to do.
    virtual void onConfigChanged() {}

    // Stable identity, used anywhere a screen has to be named in saved
    // settings (home screen, cycle order, which screens are hidden). Unlike
    // getName() - display text that could be reworded or translated - and
    // unlike the screen's index in ScreenManager - which moves as soon as
    // the user reorders anything - this value never changes. See ScreenId.h.
    virtual ScreenId id() const = 0;

    // Reports what the encoder rotation and KO button do *right now*, in
    // this screen's current internal state, for the persistent bottom
    // action-legend bar (see ActionLegend) - purely descriptive, doesn't
    // change or duplicate any input-handling logic. line1 is the encoder
    // rotation hint; leave it empty if this screen/state genuinely has no
    // rotation behavior (ActionLegend shows a neutral placeholder instead
    // of inventing one). line2 is the KO button hint; screens should
    // update it to match onButtonPress()'s real behavior in each state.
    virtual void getActionLegend(String &line1, String &line2) const {
        line1 = "";
        line2 = "";
    }

    // Whether this screen wants the persistent bottom action-legend bar at
    // all. Defaults to true. Override to return false for a screen that
    // has no meaningful encoder/button actions to report and genuinely has
    // no spare vertical room for it (see PlaneTrackerScreen) - the screen
    // then owns that whole strip for its own content instead.
    // Whether the encoder push-button is currently allowed to cycle away
    // from this screen. Defaults to true. FocusTimerScreen returns false
    // while a focus/break countdown is actively running, so a stray press
    // can't silently abandon an in-progress session - the user has to
    // explicitly pause or cancel it (via KO) first.
    virtual bool allowsScreenSwitch() const { return true; }

    virtual bool wantsActionLegend() const { return true; }

    // ScreenManager pushes this screen's current dot position in the header
    // pager (cycle order) here before every draw(). Only matters for a
    // screen that repaints the header itself (PlaneTrackerScreen, mid
    // fly-over animation) - all others let ScreenManager draw the pager and
    // can ignore it.
    virtual void setPagerPosition(int /*idx*/) {}

    // Likewise for how many dots there are. This is the length of the
    // cycle, not the number of screens compiled in, so it shrinks when the
    // user hides a screen - which is exactly why it has to be pushed every
    // draw rather than seeded once at boot.
    virtual void setPagerCount(int /*count*/) {}

protected:
    TFT_eSPI* tft;
};