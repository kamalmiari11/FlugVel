#pragma once
#include "Screen.h"

// A simple Pomodoro-style focus timer: pick a focus duration and a break
// duration, then run through focus -> break -> back to picking, using only
// the rotary encoder (adjust values / navigate) and the KO button (confirm
// / start / cancel), same input model as SettingsScreen.
class FocusTimerScreen : public Screen {
public:
    FocusTimerScreen(TFT_eSPI* display);

    void init() override;
    void update() override;
    void draw() override;
    void onEncoderUp() override;
    void onEncoderDown() override;
    void onButtonPress() override;
    const char* getName() const override { return "Focus"; }
    ScreenId id() const override { return ScreenId::Focus; }
    void getActionLegend(String &line1, String &line2) const override;

    // Locks out the encoder push-button (screen cycling) while a focus or
    // break countdown is actively running, so it can't be abandoned by a
    // stray press - the user has to explicitly pause (KO) or cancel
    // (hold KO while paused) first. Freely switchable in every other state,
    // including while paused.
    // The lock is now opt-out: some people would rather glance at the
    // weather mid-session than be held on this screen. Off means a stray
    // press really can abandon a running countdown, which is the trade.
    bool allowsScreenSwitch() const override;

    // The default focus/break lengths are copied in at construction, so a
    // change made on the phone would otherwise not show up until a reboot.
    void onConfigChanged() override;

private:
    enum State {
        SET_FOCUS,     // choosing focus minutes
        SET_BREAK,     // choosing break minutes
        FOCUS_RUNNING, // counting down the focus period
        BREAK_RUNNING, // counting down the break period
        PAUSED,        // countdown paused mid-focus or mid-break - see _pausedFrom
        DONE_FLASH     // brief "Session complete!" message before looping back to SET_FOCUS
    };

    State _state;
    State _pausedFrom; // which of FOCUS_RUNNING/BREAK_RUNNING PAUSED resumes into
    unsigned long _pauseRemainingMs; // countdown time left, frozen at the moment of pausing
    bool _needsRedraw;

    int _focusMinutes;  // 5-120, step 5, default 25
    int _breakMinutes;  // 1-30,  step 1, default 5

    unsigned long _deadlineMs;    // millis() timestamp the current countdown ends at
    unsigned long _periodTotalMs; // total duration of the current countdown, for the progress bar
    int _lastDrawnSecondsLeft;    // avoids redrawing the mm:ss digits every loop iteration

    unsigned long _doneFlashUntil;
    bool _runningJustStarted; // true right after startCountdown()/init() - forces drawRunning() to paint the static label/bar-outline/hint once, then leaves them alone

    // Hold-to-cancel while paused (KO tap resumes, KO hold cancels back to
    // SET_FOCUS) - same tap-vs-hold polling pattern as SettingsScreen's
    // restart confirmation.
    bool _koWaitingForRelease; // true until the press that opened PAUSED is released - see update()
    bool _koHeld;
    unsigned long _koHoldStartMs;
    static const unsigned long CANCEL_HOLD_MS = 900;

    // true right after init() or after returning to SET_FOCUS from a
    // running/done state - forces one full clear + title repaint, then
    // subsequent encoder ticks only repaint the two number rows (see
    // draw()'s comment for why that matters).
    bool _setScreenNeedsFullPaint;

    // Completed focus->break sessions finished today, shown on the "complete"
    // screen. _sessionsDayOfYear is the tm_yday the count belongs to, so it
    // resets itself after midnight without needing a separate timer.
    int _sessionsToday;
    int _sessionsDayOfYear;
    void bumpSessionCount();

    void startCountdown(State which, int minutes);
    void drawSetScreenTitle(); // "Focus Timer" - painted once per fresh entry, never on an encoder tick
    void drawSetScreenRows();  // the two number rows - repainted on every encoder tick and on SET_FOCUS/SET_BREAK toggle
    void drawRunning(const char* label, uint16_t barColor);
    void drawPaused();
    void drawDoneFlash();

    // Wall-clock time this phase ends at ("14:37"), from now + remainingMs,
    // in the user's 12/24h format. false if the clock isn't synced yet.
    bool phaseEndClock(unsigned long remainingMs, char* out, size_t n);
    // A blocky segmented progress bar (reads better than a hairline fill at
    // this pixel pitch, and matches the dot-matrix look).
    void drawSegBar(int x, int y, int w, int h, int segs, float progress,
                    uint16_t on, uint16_t off);
};