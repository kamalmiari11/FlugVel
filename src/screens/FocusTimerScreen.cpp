#include "FocusTimerScreen.h"
#include "../config/Config.h"
#include "../MyColors.h"
#include "../ui/Theme.h"
#include "../ui/UiChrome.h"
#include "../ui/Units.h"
#include <time.h>

FocusTimerScreen::FocusTimerScreen(TFT_eSPI* display)
    : Screen(display), _state(SET_FOCUS), _pausedFrom(FOCUS_RUNNING), _pauseRemainingMs(0),
      _needsRedraw(true),
      _focusMinutes(ConfigStore::get().focusMinutes),
      _breakMinutes(ConfigStore::get().breakMinutes),
      _deadlineMs(0), _periodTotalMs(0), _lastDrawnSecondsLeft(-1),
      _doneFlashUntil(0), _runningJustStarted(true), _setScreenNeedsFullPaint(true),
      _koWaitingForRelease(false), _koHeld(false), _koHoldStartMs(0),
      _sessionsToday(0), _sessionsDayOfYear(-1)
{
}

// "14:37" for now + remainingMs, honouring the 12/24h Units setting.
void FocusTimerScreen::onConfigChanged() {
    // Never while a countdown is live - the user is part-way through a
    // session and the numbers on screen are what they agreed to.
    if (_state == FOCUS_RUNNING || _state == BREAK_RUNNING || _state == PAUSED) return;

    _focusMinutes = ConfigStore::get().focusMinutes;
    _breakMinutes = ConfigStore::get().breakMinutes;
    _setScreenNeedsFullPaint = true;
    _needsRedraw = true;
}

bool FocusTimerScreen::allowsScreenSwitch() const {
    if (!ConfigStore::get().focusLock) return true;
    return !(_state == FOCUS_RUNNING || _state == BREAK_RUNNING);
}

bool FocusTimerScreen::phaseEndClock(unsigned long remainingMs, char* out, size_t n) {
    struct tm tmnow;
    if (!getLocalTime(&tmnow, 5)) return false;
    time_t end = mktime(&tmnow) + (time_t)((remainingMs + 500) / 1000);
    struct tm tmend;
    localtime_r(&end, &tmend);
    strftime(out, n, Units::strftimeTime(), &tmend);
    return true;
}

void FocusTimerScreen::drawSegBar(int x, int y, int w, int h, int segs, float progress,
                                  uint16_t on, uint16_t off) {
    if (progress < 0) progress = 0;
    if (progress > 1) progress = 1;
    int gap = 4;
    int segW = (w - gap * (segs - 1)) / segs;
    if (segW < 2) segW = 2;
    int lit = (int)(progress * segs + 0.5f);
    for (int i = 0; i < segs; i++) {
        int sx = x + i * (segW + gap);
        if (i < lit) tft->fillRect(sx, y, segW, h, on);
        else         tft->drawRect(sx, y, segW, h, off);
    }
}

void FocusTimerScreen::bumpSessionCount() {
    struct tm tmnow;
    int yday = getLocalTime(&tmnow, 5) ? tmnow.tm_yday : -1;
    if (yday != _sessionsDayOfYear) {
        _sessionsDayOfYear = yday;
        _sessionsToday = 0;
    }
    _sessionsToday++;
}

void FocusTimerScreen::init() {
    Serial.println("[FocusTimerScreen] Initialized");
    // Deliberately NOT resetting _state/_deadlineMs/_focusMinutes/
    // _breakMinutes here. ScreenManager calls init() every time this
    // screen is (re)entered, including mid-countdown if the user cycled
    // to another screen and back - resetting here would silently cancel
    // an in-progress focus/break session just for glancing elsewhere.
    // Just force a full, correct repaint of whatever state we're already
    // in (the previous screen's own init()/draw() painted over this area).
    _needsRedraw = true;
    _lastDrawnSecondsLeft = -1;
    // Re-entering this screen (e.g. after cycling away and back) means
    // whatever was on screen has been wiped by another screen's own
    // redraw - force the static parts of the running/paused view to be
    // repainted too, not just the digits.
    _runningJustStarted = true;
    _setScreenNeedsFullPaint = true;
    // Reset the pause hold-to-cancel gesture tracking too - it can only be
    // mid-gesture if the user left this screen while paused (allowed - see
    // allowsScreenSwitch()), and whatever partial press was in progress
    // then is long gone by the time we're re-entered.
    _koWaitingForRelease = false;
    _koHeld = false;
}

void FocusTimerScreen::update() {
    if (_state == FOCUS_RUNNING || _state == BREAK_RUNNING) {
        long remainMs = (long)(_deadlineMs - millis());
        if (remainMs < 0) remainMs = 0;
        int secondsLeft = (remainMs + 999) / 1000; // ceiling, so it reads e.g. 25:00 not 24:59 at the very start

        if (secondsLeft != _lastDrawnSecondsLeft) {
            _lastDrawnSecondsLeft = secondsLeft;
            _needsRedraw = true;
        }

        if (remainMs <= 0) {
            if (_state == FOCUS_RUNNING) {
                if (ConfigStore::get().focusAutoBreak) {
                    Serial.println("[FocusTimerScreen] Focus period done - starting break");
                    startCountdown(BREAK_RUNNING, _breakMinutes);
                } else {
                    // Stop at the end of the focus block instead and let the
                    // user start the break when they are actually ready.
                    Serial.println("[FocusTimerScreen] Focus period done - waiting");
                    bumpSessionCount();
                    _state = DONE_FLASH;
                }
            } else {
                Serial.println("[FocusTimerScreen] Break done - session complete");
                bumpSessionCount();
                _state = DONE_FLASH;
                _doneFlashUntil = millis() + 4000;
                _needsRedraw = true;
            }
        }
    } else if (_state == DONE_FLASH) {
        if ((long)(millis() - _doneFlashUntil) >= 0) {
            _state = SET_FOCUS;
            _needsRedraw = true;
            _setScreenNeedsFullPaint = true; // was showing "Session complete!" - needs a real clear
        }
    } else if (_state == PAUSED) {
        // Poll the button directly rather than reacting to onButtonPress()'s
        // press-edge callback, so a quick tap (resume) can be told apart
        // from a hold (cancel) - by the time onButtonPress() fires the
        // button has only just gone down, so it can't yet know which one
        // this will turn out to be. Same pattern as SettingsScreen's
        // hold-to-restart.
        bool pressed = (digitalRead(KO_BUTTON) == LOW);

        if (_koWaitingForRelease) {
            // Still the same physical press that paused the countdown -
            // ignore it until released, so it can't itself be read as a
            // tap-to-resume or count towards the hold timer.
            if (!pressed) _koWaitingForRelease = false;
        } else if (pressed && !_koHeld) {
            _koHeld = true;
            _koHoldStartMs = millis();
        } else if (pressed && _koHeld) {
            if (millis() - _koHoldStartMs >= CANCEL_HOLD_MS) {
                Serial.println("[FocusTimerScreen] Paused session cancelled by long-press");
                _koHeld = false;
                _state = SET_FOCUS;
                _needsRedraw = true;
                _setScreenNeedsFullPaint = true;
            } else {
                _needsRedraw = true; // keep the hold-progress bar animating
            }
        } else if (!pressed && _koHeld) {
            // Released before the hold threshold - a quick tap means "resume".
            _koHeld = false;
            Serial.println("[FocusTimerScreen] Resuming paused session");
            _state = _pausedFrom;
            _deadlineMs = millis() + _pauseRemainingMs;
            _lastDrawnSecondsLeft = -1;
            _runningJustStarted = true; // repaint the static label/bar once
            _needsRedraw = true;
        }
    }
}

void FocusTimerScreen::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;

    // Only SET_FOCUS/SET_BREAK/DONE_FLASH do a full clear - they're drawn
    // once per state entry, not every second, so a full repaint is fine
    // and simplest there. FOCUS_RUNNING/BREAK_RUNNING are handled entirely
    // inside drawRunning(), which never does a full-area clear - it only
    // touches the digits and the bar fill, which is what actually changes
    // each tick. That's what kills the every-second flicker: nothing else
    // on screen (label, bar outline, hint text) gets wiped and redrawn.
    if (_state == FOCUS_RUNNING || _state == BREAK_RUNNING) {
        const Theme &theme = ThemeManager::current();
        drawRunning(_state == FOCUS_RUNNING ? "FOCUS" : "BREAK",
                    _state == FOCUS_RUNNING ? theme.accent : theme.accent2);
        return;
    }

    if (_state == PAUSED) {
        // Same "static parts once, changed bits every tick" approach as
        // drawRunning() - the hold-progress bar interior is the only thing
        // that actually changes while sitting in this state.
        drawPaused();
        return;
    }

    if (_state == SET_FOCUS || _state == SET_BREAK) {
        // Same idea as drawRunning() above: adjusting the minutes with the
        // encoder used to trigger a full-area clear + full repaint (title
        // included) on every single tick, which is both slower and causes
        // a visible flash. Only the two number rows actually change as the
        // knob turns - the title never does - so a full clear+repaint only
        // happens once, the moment this screen is freshly entered.
        if (_setScreenNeedsFullPaint) {
            tft->fillRect(0, 20, tft->width(), tft->height() - 20, ThemeManager::current().bg);
            drawSetScreenTitle();
            _setScreenNeedsFullPaint = false;
        }
        drawSetScreenRows();
        return;
    }

    tft->fillRect(0, 20, tft->width(), tft->height() - 20, ThemeManager::current().bg);

    switch (_state) {
        case DONE_FLASH:
            drawDoneFlash();
            break;
        default:
            break;
    }
}

void FocusTimerScreen::startCountdown(State which, int minutes) {
    _state = which;
    _periodTotalMs = (unsigned long)minutes * 60000UL;
    _deadlineMs = millis() + _periodTotalMs;
    _lastDrawnSecondsLeft = -1; // force an immediate digit redraw
    _runningJustStarted = true; // force the static parts (label/bar/hint) to be painted once
    _needsRedraw = true;
}

void FocusTimerScreen::drawSetScreenTitle() {
    const Theme &t = ThemeManager::current();
    tft->setTextDatum(TL_DATUM);
    tft->setTextSize(1);
    tft->setTextColor(t.fgDim);
    tft->drawString("SESSION", 14, 34);
    tft->drawFastHLine(14, 46, tft->width() - 28, t.rule);
}

void FocusTimerScreen::drawSetScreenRows() {
    const Theme &t = ThemeManager::current();
    const int W = tft->width();

    auto row = [&](int y, const char* label, int minutes, bool sel) {
        UiChrome::drawPanelRow(tft, 10, y, W - 20, 34, sel, t.bg, t.selectBg);
        if (sel) tft->fillRect(10, y, 4, 34, t.accent);          // left accent bar

        tft->setTextDatum(ML_DATUM);
        tft->setTextSize(2);
        tft->setTextColor(sel ? t.selectFg : t.fgDim);
        tft->drawString(label, 26, y + 18);

        char v[12];
        snprintf(v, sizeof(v), "%d min", minutes);
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(sel ? t.selectFg : t.fg);
        tft->drawString(v, W - 70, y + 18);

        uint16_t chev = sel ? t.selectFg : t.fgDim;
        int cy = y + 18;
        tft->fillTriangle(W - 110, cy, W - 104, cy - 4, W - 104, cy + 4, chev);   // <
        tft->fillTriangle(W - 26,  cy, W - 32,  cy - 4, W - 32,  cy + 4, chev);   // >
    };

    row(56, "FOCUS", _focusMinutes, _state == SET_FOCUS);
    row(96, "BREAK", _breakMinutes, _state == SET_BREAK);

    // Projected finish time - recomputed each tick as the minutes change.
    tft->fillRect(14, 138, W - 28, 12, t.bg);
    tft->setTextSize(1);
    tft->setTextColor(t.fgDim);
    char endc[12];
    if (phaseEndClock((unsigned long)(_focusMinutes + _breakMinutes) * 60000UL, endc, sizeof(endc))) {
        tft->setTextDatum(TL_DATUM);
        tft->drawString("starts now", 14, 138);
        char buf[24];
        snprintf(buf, sizeof(buf), "ends %s", endc);
        tft->setTextDatum(TR_DATUM);
        tft->drawString(buf, W - 14, 138);
    } else {
        char buf[28];
        snprintf(buf, sizeof(buf), "%d + %d min total", _focusMinutes, _breakMinutes);
        tft->setTextDatum(TL_DATUM);
        tft->drawString(buf, 14, 138);
    }
    tft->setTextDatum(TL_DATUM);
}

void FocusTimerScreen::drawRunning(const char* label, uint16_t barColor) {
    const Theme &t = ThemeManager::current();
    const int W = tft->width();

    int secondsLeft = _lastDrawnSecondsLeft < 0 ? 0 : _lastDrawnSecondsLeft;
    char timeStr[8];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", secondsLeft / 60, secondsLeft % 60);

    long remainMs = (long)(_deadlineMs - millis());
    if (remainMs < 0) remainMs = 0;

    const int barX = 24, barY = 168, barW = W - 48, barH = 12;

    if (_runningJustStarted) {
        // Static parts, painted once - never touched again until the state
        // changes, so they can't flicker. The phase end time is fixed for
        // the whole countdown, so it lives here too.
        tft->fillRect(0, 20, W, tft->height() - 20, t.bg);

        tft->setTextDatum(MC_DATUM);
        tft->setTextSize(2);
        tft->setTextColor(barColor);
        tft->drawString(label, W / 2, 44);

        // focus/break phase dots - filled = where we are now
        bool onFocus = (_state == FOCUS_RUNNING);
        int cx = W / 2;
        if (onFocus) { tft->fillCircle(cx - 8, 62, 3, t.accent);  tft->drawCircle(cx + 8, 62, 3, t.accent2); }
        else         { tft->drawCircle(cx - 8, 62, 3, t.accent);  tft->fillCircle(cx + 8, 62, 3, t.accent2); }

        tft->setTextSize(1);
        tft->setTextColor(t.fgDim);
        char endc[12], line[24];
        if (phaseEndClock((unsigned long)remainMs, endc, sizeof(endc))) {
            snprintf(line, sizeof(line), "ends %s", endc);
            tft->drawString(line, W / 2, 150);
        }

        _runningJustStarted = false;
    }

    // Big digits - only this band gets cleared+redrawn each tick.
    tft->setTextDatum(MC_DATUM);
    tft->setTextSize(6);
    tft->fillRect(0, 84, W, 52, t.bg);
    tft->setTextColor(t.fg);
    tft->drawString(timeStr, W / 2, 110);

    // Segmented progress bar.
    unsigned long elapsedMs = _periodTotalMs - (unsigned long)remainMs;
    float progress = (_periodTotalMs > 0) ? (float)elapsedMs / (float)_periodTotalMs : 0.0f;
    tft->fillRect(barX, barY, barW, barH, t.bg);
    drawSegBar(barX, barY, barW, barH, 20, progress, barColor, t.rule);

    // Percent + minutes-left readout.
    int pct = (int)(progress * 100 + 0.5f);
    int minsLeft = ((int)remainMs + 59999) / 60000;
    char rd[28];
    snprintf(rd, sizeof(rd), "%d%%  -  %d min left", pct, minsLeft);
    tft->setTextSize(1);
    tft->fillRect(0, 190, W, 12, t.bg);
    tft->setTextColor(t.fgDim);
    tft->drawString(rd, W / 2, 196);
}

void FocusTimerScreen::drawPaused() {
    const Theme &t = ThemeManager::current();
    const int W = tft->width();

    int secondsLeft = (int)((_pauseRemainingMs + 999) / 1000);
    char timeStr[8];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", secondsLeft / 60, secondsLeft % 60);

    const int barX = 40, barY = 176, barW = W - 80, barH = 12;

    if (_runningJustStarted) {
        // Static parts, painted once on entering PAUSED.
        tft->fillRect(0, 20, W, tft->height() - 20, t.bg);

        tft->setTextDatum(MC_DATUM);
        tft->setTextSize(2);
        tft->setTextColor(t.accent);
        tft->drawString("PAUSED", W / 2, 44);

        // Dimmed, non-counting clock - clearly not running.
        tft->setTextSize(6);
        tft->setTextColor(t.fgDim);
        tft->drawString(timeStr, W / 2, 104);

        tft->setTextSize(2);
        tft->setTextColor(t.fg);
        tft->drawString("TAP KO TO RESUME", W / 2, 148);

        tft->setTextSize(1);
        tft->setTextColor(t.danger);
        tft->drawString("HOLD KO TO CANCEL", W / 2, 168);
        tft->drawRect(barX, barY, barW, barH, t.danger);

        _runningJustStarted = false;
    }

    // Hold-to-cancel progress fill - interior only, and only while a hold
    // is actually being timed.
    unsigned long heldMs = _koHeld ? (millis() - _koHoldStartMs) : 0;
    if (heldMs > CANCEL_HOLD_MS) heldMs = CANCEL_HOLD_MS;
    int filled = (int)((unsigned long)(barW - 4) * heldMs / CANCEL_HOLD_MS);
    tft->fillRect(barX + 2, barY + 2, barW - 4, barH - 4, t.bg);
    if (filled > 0) tft->fillRect(barX + 2, barY + 2, filled, barH - 4, t.danger);
}

void FocusTimerScreen::drawDoneFlash() {
    const Theme &t = ThemeManager::current();
    const int W = tft->width();
    const int cx = W / 2;

    // Check glyph.
    tft->drawCircle(cx, 46, 13, t.accent2);
    tft->drawLine(cx - 6, 46, cx - 2, 51, t.accent2);
    tft->drawLine(cx - 2, 51, cx + 7, 39, t.accent2);

    tft->setTextDatum(MC_DATUM);
    tft->setTextSize(2);
    tft->setTextColor(t.accent2);
    tft->drawString("SESSION COMPLETE", cx, 82);

    // Recap: what ran, and how many sessions today.
    const int bx = 30, by = 104, bw = W - 60, bh = 54;
    tft->drawRect(bx, by, bw, bh, t.rule);
    int c1 = bx + bw / 6, c2 = bx + bw / 2, c3 = bx + 5 * bw / 6;

    tft->setTextSize(1);
    tft->setTextColor(t.fgDim);
    tft->drawString("FOCUS", c1, by + 16);
    tft->drawString("BREAK", c2, by + 16);
    tft->drawString("TODAY", c3, by + 16);

    char a[8], b[8], c[8];
    snprintf(a, sizeof(a), "%dm", _focusMinutes);
    snprintf(b, sizeof(b), "%dm", _breakMinutes);
    snprintf(c, sizeof(c), "#%d", _sessionsToday > 0 ? _sessionsToday : 1);
    tft->setTextSize(2);
    tft->setTextColor(t.fg);
    tft->drawString(a, c1, by + 38);
    tft->drawString(b, c2, by + 38);
    tft->drawString(c, c3, by + 38);

    tft->setTextSize(1);
    tft->setTextColor(t.fgDim);
    tft->drawString("Nice work.", cx, 176);
}

void FocusTimerScreen::getActionLegend(String &line1, String &line2) const {
    switch (_state) {
        case SET_FOCUS:
            line1 = "^v ADJUST FOCUS MIN";
            line2 = "o NEXT: SET BREAK";
            break;
        case SET_BREAK:
            line1 = "^v ADJUST BREAK MIN";
            line2 = "o START FOCUS";
            break;
        case FOCUS_RUNNING:
            line1 = "";
            line2 = "o PAUSE";
            break;
        case BREAK_RUNNING:
            line1 = "";
            line2 = "o PAUSE";
            break;
        case PAUSED:
            line1 = "";
            line2 = "TAP: RESUME  HOLD: CANCEL";
            break;
        case DONE_FLASH:
            line1 = "";
            line2 = "o NEW SESSION";
            break;
    }
}

void FocusTimerScreen::onEncoderUp() {
    if (_state == SET_FOCUS) {
        _focusMinutes = min(120, _focusMinutes + 5);
        _needsRedraw = true;
    } else if (_state == SET_BREAK) {
        _breakMinutes = min(30, _breakMinutes + 1);
        _needsRedraw = true;
    }
}

void FocusTimerScreen::onEncoderDown() {
    if (_state == SET_FOCUS) {
        _focusMinutes = max(5, _focusMinutes - 5);
        _needsRedraw = true;
    } else if (_state == SET_BREAK) {
        _breakMinutes = max(1, _breakMinutes - 1);
        _needsRedraw = true;
    }
}

void FocusTimerScreen::onButtonPress() {
    switch (_state) {
        case SET_FOCUS:
            _state = SET_BREAK;
            _needsRedraw = true;
            break;

        case SET_BREAK:
            Serial.printf("[FocusTimerScreen] Starting focus: %d min (break after: %d min)\n",
                          _focusMinutes, _breakMinutes);
            startCountdown(FOCUS_RUNNING, _focusMinutes);
            break;

        case FOCUS_RUNNING:
        case BREAK_RUNNING:
            Serial.println("[FocusTimerScreen] Paused");
            _pausedFrom = _state;
            _pauseRemainingMs = (unsigned long)max(0, (int)(_deadlineMs - millis()));
            _state = PAUSED;
            _runningJustStarted = true; // repaint the static "Paused" label/bar once
            _koWaitingForRelease = true; // this press opened PAUSED - don't let it also count as the next tap/hold
            _needsRedraw = true;
            break;

        case DONE_FLASH:
            _state = SET_FOCUS;
            _needsRedraw = true;
            _setScreenNeedsFullPaint = true; // was showing "Session complete!" - needs a real clear
            break;

        case PAUSED:
            // Tap-to-resume / hold-to-cancel while paused is handled
            // entirely by update()'s direct button polling (see the
            // comment there) - nothing to do on the press-edge callback.
            break;
    }
}