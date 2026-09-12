#pragma once
#include "Screen.h"
#include "Header.h"
#include "ActionLegend.h"
#include <ESP32Encoder.h>
#include <vector>

class ScreenManager {
public:
    ScreenManager(TFT_eSPI* display, int encoderBtnPin, int koButtonPin);

    // Attach the persistent top status bar (title + date/time)
    void setHeader(Header* header) { _header = header; }

    // Attach the persistent bottom action-legend bar
    void setActionLegend(ActionLegend* legend) { _legend = legend; }

    // Add screen to the manager. Order here doesn't decide anything any
    // more - it's just the pool the cycle is drawn from, and rebuildCycle()
    // decides what actually gets shown and in what order.
    void addScreen(Screen* screen);

    // Rebuilds the cycle from the saved config: the user's enabled screens
    // in their chosen order, then the pinned Settings screen last. Call once
    // after every addScreen(), and again via applyConfigChange() whenever
    // the config is edited. Does NOT init() anything.
    void rebuildCycle();

    // Which screen is on the panel right now. Static because the config
    // page needs it and has no reason to know about ScreenManager itself -
    // it only wants to open its preview on whatever the device is showing.
    static ScreenId showingId();

    // Jump straight to a screen by id, without init()ing it - used at boot
    // to land on the screen the device was showing when it lost power.
    // Ignored if that screen is not in the current cycle.
    void selectById(ScreenId id);

    // Repaint everything from scratch: chrome invalidated, current screen
    // re-inited. For changes that invalidate the whole panel rather than
    // just its contents - rotating the display, for one.
    void forceRepaint();

    // Rebuild after the config changed, and init() the current screen if
    // the edit moved us onto a different one (because the screen that was
    // showing has just been hidden). Safe to call from a settings screen.
    void applyConfigChange();

    // Hand every screen (not only the visible one) a chance to pick up a
    // changed setting, then repaint. Screens that latched a value at
    // construction, or that need a refetch, react in onConfigChanged().
    void notifyConfigChanged();

    // Init the screen currently selected. Call this once, explicitly, after
    // BootScreen has finished painting the display - NOT automatically from
    // addScreen() - see addScreen()'s comment.
    void initCurrentScreen();

    // Update and draw current screen
    void update();
    void draw();

    // Input handling
    void handleEncoderInput();    // encoder push-button (short press = next screen, long press = open switcher)
    void handleEncoderRotation(); // encoder rotation (up/down within a screen, or browse the switcher while it's open)
    void handleButtonInput();

    // Find an added screen by its stable id (see ScreenId.h), or nullptr
    // if this build never added it. Lets main.cpp wire up the concrete
    // screens it needs without also owning the construction order.
    Screen* byId(ScreenId id) const;

    // The screen currently showing, or nullptr if nothing is in the cycle.
    Screen* getCurrentScreen() const;

    // How many screens the cycle holds - i.e. how many dots the header
    // pager draws. Hiding a screen genuinely removes a dot.
    int cycleLength() const { return (int)_cycle.size(); }

private:
    TFT_eSPI* tft;
    Header* _header = nullptr;
    ActionLegend* _legend = nullptr;

    // Every screen that was added, in ScreenRegistry table order. Indices
    // into this vector are stable for the life of the program.
    std::vector<Screen*> screens;

    // The cycle: indices into `screens`, in the order the encoder button
    // walks them, with the pinned Settings screen always last. Rebuilt from
    // the saved config, so this is the ONLY place ordering lives - the two
    // hand-rolled index-arithmetic blocks that used to compute "which
    // screen is next" and "which pager dot is this" from a home-screen
    // offset are gone, and with them the chance of the two disagreeing.
    std::vector<int> _cycle;
    int _cyclePos = 0;   // position within _cycle, NOT an index into screens

    int encoderBtnPin;
    int koButtonPin;

    bool lastEncoderState;
    bool lastButtonState;
    unsigned long lastInputTime;
    const unsigned long INPUT_DEBOUNCE = 200;  // ms

    // Rotary encoder (quadrature) state
    ESP32Encoder rotaryEncoder;
    long lastEncoderPos = 0;
    long encoderAccum = 0;
    static const long STEPS_PER_DETENT = 4; // full-quad mode: 4 counts/detent

    // ---- Screen switcher overlay ----
    // Opened by HOLDING the encoder button down (see handleEncoderInput()),
    // not by a normal click - a normal click keeps doing exactly what it
    // always did (advance to the next screen). Releasing the button that
    // opened it does nothing - the switcher stays up so it can actually be
    // browsed hands-free on the knob, closing only on a fresh hold of the
    // encoder (same threshold, same button, just pressed again). While
    // open, turning the knob moves a highlight across every screen in the
    // cycle instead of reaching the underlying screen's onEncoderUp/
    // onEncoderDown, and KO jumps straight to the highlighted screen
    // instead of reaching the underlying screen's onButtonPress(). A short
    // press/release while it's open does nothing either way - only KO
    // (choose) or another hold (cancel) end it.
    bool _switcherOpen = false;
    int  _switcherHighlight = 0;     // index into _cycle, not into screens

    // Long-press bookkeeping for the encoder button, tracked separately
    // from the short-press handling in handleEncoderInput() so one physical
    // button can mean two different things depending on how long it's held.
    unsigned long _encoderPressStart = 0;
    bool _longPressFired = false;
    static const unsigned long LONG_PRESS_MS = 500;

    // Moves _cyclePos to newPos and does everything a real screen change
    // needs (persist lastScreenId, init() the new screen, invalidate header/
    // legend). Shared by the original "click = next screen" path and by
    // confirming a switcher selection - those are the same operation, just
    // landing on a different index.
    void switchTo(int newCyclePos);

    void openSwitcher();
    void moveSwitcherHighlight(int direction);
    void closeSwitcherCancel();
    void closeSwitcherConfirm();
    void drawSwitcher();
    static void drawScreenIcon(TFT_eSPI* tft, ScreenId id, int cx, int cy, uint16_t fg, uint16_t bg);
};
