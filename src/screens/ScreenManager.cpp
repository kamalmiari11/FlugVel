#include "ScreenManager.h"
#include "ScreenRegistry.h"
#include "../config/Config.h"
#include "../ui/Theme.h"
#include "../ui/UiChrome.h"
#include <Arduino.h>

ScreenManager::ScreenManager(TFT_eSPI* display, int encoderBtnPin, int koButtonPin)
    : tft(display), encoderBtnPin(encoderBtnPin),
      koButtonPin(koButtonPin), lastEncoderState(HIGH), lastButtonState(HIGH),
      lastInputTime(0)
{
    pinMode(encoderBtnPin, INPUT_PULLUP);
    pinMode(koButtonPin, INPUT_PULLUP);

    // ---- Rotary encoder (quadrature) setup ----
    // ENCODER_A / ENCODER_B come from platformio.ini build_flags.
    ESP32Encoder::useInternalWeakPullResistors = puType::up;
    rotaryEncoder.attachFullQuad(ENCODER_A, ENCODER_B);
    rotaryEncoder.setCount(0);
    lastEncoderPos = 0;
    encoderAccum = 0;

    Serial.println("[ScreenManager] Initialized");
}

void ScreenManager::addScreen(Screen* screen) {
    if (screen) {
        screens.push_back(screen);
        // NOTE: deliberately NOT calling screen->init() here. On boot,
        // screens are added long before BootScreen::show() runs, and
        // BootScreen paints over the whole panel (fillScreen + its own
        // text) after this point. If the first screen drew its border
        // here, boot would immediately erase it, and nothing would ever
        // redraw it since draw() intentionally never redraws the border
        // (see PlaneTrackerScreen::drawBorder()). Call initCurrentScreen()
        // once, after boot has fully finished painting, instead.
    }
}

Screen* ScreenManager::byId(ScreenId id) const {
    for (Screen* s : screens) {
        if (s && s->id() == id) return s;
    }
    return nullptr;
}

// Updated whenever the shown screen changes; read by the config page.
static ScreenId s_showing = ScreenId::Plane;

ScreenId ScreenManager::showingId() { return s_showing; }

Screen* ScreenManager::getCurrentScreen() const {
    if (_cycle.empty()) return nullptr;
    return screens[_cycle[_cyclePos]];
}

// Builds the cycle: the user's enabled screens in their saved order, then
// every pinned screen (Settings) appended last. Screens the config doesn't
// mention, or names by an id this build doesn't have, are simply skipped -
// which is what makes a downgrade harmless.
void ScreenManager::rebuildCycle() {
    // Remember what was showing so we can stay on it if it survives.
    bool hadCurrent = !_cycle.empty();
    ScreenId prevId = ScreenId::Settings;
    if (hadCurrent) prevId = screens[_cycle[_cyclePos]]->id();

    _cycle.clear();

    const Config &cfg = ConfigStore::get();
    for (int i = 0; i < cfg.screenCount; i++) {
        if (!cfg.screens[i].enabled) continue;
        for (size_t s = 0; s < screens.size(); s++) {
            if (ScreenRegistry::isPinned(screens[s]->id())) continue;
            if ((uint8_t)screens[s]->id() == cfg.screens[i].id) {
                _cycle.push_back((int)s);
                break;
            }
        }
    }

    // Safety net. A config with nothing enabled would leave a "cycle" of
    // Settings alone, which looks like a broken device - and the only way
    // out would be a factory reset. Fall back to showing everything.
    if (_cycle.empty()) {
        Serial.println("[ScreenManager] Config enabled no screens - falling back to all");
        for (size_t s = 0; s < screens.size(); s++) {
            if (!ScreenRegistry::isPinned(screens[s]->id())) _cycle.push_back((int)s);
        }
    }

    // Pinned screens always come last, so the encoder button reaches
    // Settings as the final stop before wrapping back round to home. This
    // one line replaces the whole "treat the non-Settings screens as their
    // own circular sequence starting at homeScreen" arithmetic.
    for (size_t s = 0; s < screens.size(); s++) {
        if (ScreenRegistry::isPinned(screens[s]->id())) _cycle.push_back((int)s);
    }

    // Home is simply _cycle[0]; stay put if the screen we were on is still
    // in the cycle, otherwise fall back to home.
    _cyclePos = 0;
    if (hadCurrent) {
        for (size_t i = 0; i < _cycle.size(); i++) {
            if (screens[_cycle[i]]->id() == prevId) { _cyclePos = (int)i; break; }
        }
    }

    if (!_cycle.empty()) s_showing = screens[_cycle[_cyclePos]]->id();
    if (_header) _header->invalidate();
}

void ScreenManager::applyConfigChange() {
    Screen* before = getCurrentScreen();
    rebuildCycle();
    Screen* after = getCurrentScreen();

    // Only re-init if the edit actually moved us - e.g. the screen that was
    // showing has just been hidden, so rebuildCycle() snapped back to home.
    if (after && after != before) after->init();

    if (_header) _header->invalidate();
    if (_legend) _legend->invalidate();
}

void ScreenManager::notifyConfigChanged() {
    for (Screen* s : screens) if (s) s->onConfigChanged();

    // A full repaint, because most options are read at draw time and a
    // screen that only redraws what changed would otherwise keep showing
    // the old layout until something else happened to dirty it.
    forceRepaint();
}

void ScreenManager::selectById(ScreenId id) {
    for (size_t i = 0; i < _cycle.size(); i++) {
        if (screens[_cycle[i]]->id() == id) { _cyclePos = (int)i; s_showing = id; return; }
    }
}

void ScreenManager::forceRepaint() {
    if (_header) _header->invalidate();
    if (_legend) _legend->invalidate();
    Screen* s = getCurrentScreen();
    if (s) s->init();
}

void ScreenManager::initCurrentScreen() {
    if (_cycle.empty()) rebuildCycle();
    Screen* s = getCurrentScreen();
    if (s) {
        s->init();
        Serial.printf("[ScreenManager] Initialized screen %d/%d (%s)\n",
                      _cyclePos + 1, (int)_cycle.size(), s->getName());
    }
}

void ScreenManager::update() {
    // Frozen while the switcher is up - a running game or countdown
    // shouldn't keep advancing invisibly behind an overlay you're just
    // browsing. See handleEncoderInput()/drawSwitcher().
    if (_switcherOpen) return;

    Screen* s = getCurrentScreen();
    if (s) s->update();
}

void ScreenManager::draw() {
    // The switcher paints itself once, on open and on every highlight move
    // (see drawSwitcher()) - not every loop like a normal screen. Letting
    // the current screen's own draw() keep running here would just paint
    // straight back over it every frame.
    if (_switcherOpen) return;

    Screen* s = getCurrentScreen();
    const int count = (int)_cycle.size();

    if (s) {
        // Tell the current screen where it sits in the pager BEFORE it
        // draws - the plane screen repaints the header itself mid-animation
        // and would otherwise use a stale dot index or count.
        s->setPagerPosition(_cyclePos);
        s->setPagerCount(count);
        s->draw();
    }

    // The header owns the top 20px on every screen and is always redrawn
    // last so it can never be left blank/stale by a screen's own redraw.
    if (_header) {
        const char* title = s ? s->getName() : "";
        _header->draw(title, _cyclePos, count);
    }

    // Same "always redraw this thin strip last" pattern for the bottom
    // action-legend bar - text is sourced from the current screen's own
    // getActionLegend(), which reports its real onEncoderUp/Down/
    // onButtonPress behavior for whatever state it's currently in.
    // Screens with nothing meaningful to show there (and no spare room for
    // it) can opt out entirely via wantsActionLegend() - PlaneTrackerScreen
    // uses that space for its own content instead.
    if (_legend) {
        // Two ways the strip can be absent: the user turned it off for the
        // whole device, or this screen wants the space for itself.
        bool wantsLegend = ConfigStore::get().legend && (!s || s->wantsActionLegend());
        if (wantsLegend) {
            String line1 = "", line2 = "";
            if (s) s->getActionLegend(line1, line2);
            _legend->draw(line1, line2);
        }
    }
}

// Shared tail of every real screen change - the original "click = next
// screen" path and confirming a switcher selection both just pick a
// destination index and land here.
void ScreenManager::switchTo(int newCyclePos) {
    _cyclePos = newCyclePos;
    Screen* s = screens[_cycle[_cyclePos]];
    s_showing = s->id();

    // Remembered so the device can come back to it after a power cut.
    // Written to NVS only on an actual screen change, which is a handful of
    // times a day, not per loop.
    Config &cfg = ConfigStore::get();
    if (cfg.startupResume && cfg.lastScreenId != (uint8_t)s->id()) {
        cfg.lastScreenId = (uint8_t)s->id();
        ConfigStore::save();
    }

    s->init();
    if (_header) _header->invalidate();
    if (_legend) _legend->invalidate();
    Serial.printf("[ScreenManager] Switched to screen %d/%d (%s)\n",
                  _cyclePos + 1, (int)_cycle.size(), s->getName());
}

void ScreenManager::handleEncoderInput() {
    if (_cycle.empty()) return;

    unsigned long now = millis();
    bool currentState = digitalRead(encoderBtnPin);

    // Detect a state change (press or release; LOW = pressed).
    if (currentState != lastEncoderState) {
        if (now - lastInputTime > INPUT_DEBOUNCE) {
            lastInputTime = now;
            lastEncoderState = currentState;

            if (currentState == LOW) {
                // Just went down - don't act yet. Whether this turns into
                // "next screen" or "open the switcher" depends on how long
                // it stays down, which we can't know until either it's
                // released (short) or LONG_PRESS_MS elapses while still
                // held (long, handled by the poll below).
                _encoderPressStart = now;
                _longPressFired = false;
                return;
            }

            // Released.
            if (_longPressFired) {
                // Already acted on this hold (opened or closed the
                // switcher - see the poll below) while it was still down.
                // Releasing the button afterward is not a separate event -
                // in particular, letting go of the hold that just OPENED
                // the switcher must not also close it, or it could never
                // stay up long enough to browse.
                return;
            }

            if (_switcherOpen) {
                // A short press/release while the switcher is already open
                // does nothing - KO is what actually picks a screen, and
                // only a fresh hold (below) closes it without picking one.
                return;
            }

            // A genuine short press/release with the switcher closed:
            // original behavior, unchanged - step to the next screen,
            // unless the current screen has locked that out (see
            // Screen::allowsScreenSwitch()'s comment - FocusTimerScreen
            // does this while a countdown is running).
            if (!screens[_cycle[_cyclePos]]->allowsScreenSwitch()) {
                Serial.println("[ScreenManager] Encoder button ignored - current screen has locked screen switching");
                return;
            }

            // The cycle is already in the right order (home first,
            // Settings last), so "next screen" is one step forward with a
            // wrap - nothing to compute.
            switchTo((_cyclePos + 1) % (int)_cycle.size());
        }
    }

    // Long-press detection while the button is still held down - polled on
    // every call (not gated by the debounce above, which only guards state
    // *changes*) so it can fire without waiting for a release. Runs whether
    // the switcher is open or closed: closed, a hold opens it; open, a
    // fresh hold closes it (cancel) - the same gesture toggles both ways.
    if (currentState == LOW && !_longPressFired) {
        if (now - _encoderPressStart >= LONG_PRESS_MS) {
            _longPressFired = true;
            if (_switcherOpen) {
                closeSwitcherCancel();
            } else if (screens[_cycle[_cyclePos]]->allowsScreenSwitch()) {
                openSwitcher();
            } else {
                // Same lock the short press already respects - a running
                // focus/break countdown can't be interrupted by the
                // switcher either.
                Serial.println("[ScreenManager] Long-press ignored - current screen has locked screen switching");
            }
        }
    }
}

void ScreenManager::handleEncoderRotation() {
    if (_cycle.empty()) return;

    long pos = rotaryEncoder.getCount();
    long delta = pos - lastEncoderPos;
    lastEncoderPos = pos;

    if (delta == 0) return;

    // Left-handed means the whole board has been physically turned around,
    // so the encoder is upside down too: what the user sees as a clockwise
    // turn arrives as counts going the other way. Negating here keeps
    // "turn clockwise" meaning "move down the list" in both orientations.
    if (ConfigStore::get().leftHanded)   delta = -delta;
    // Separate from handedness: this one is for a board wired with
    // ENCODER_A / ENCODER_B the other way round, where the knob felt
    // backwards to begin with. Both can apply, and cancel out if they do.
    if (ConfigStore::get().knobReversed) delta = -delta;

    encoderAccum += delta;

    Screen* s = screens[_cycle[_cyclePos]];

    // Turn the raw quadrature counts into "detents" (physical clicks) so a
    // single notch of the knob results in exactly one onEncoderUp/Down call
    // - or, while the switcher is open, exactly one step of its highlight
    // instead. The underlying screen never sees these ticks at all in that
    // case, same as it never sees update()/draw() calls (see update()).
    while (encoderAccum >= STEPS_PER_DETENT) {
        if (_switcherOpen) moveSwitcherHighlight(+1);
        else s->onEncoderUp();
        encoderAccum -= STEPS_PER_DETENT;
    }
    while (encoderAccum <= -STEPS_PER_DETENT) {
        if (_switcherOpen) moveSwitcherHighlight(-1);
        else s->onEncoderDown();
        encoderAccum += STEPS_PER_DETENT;
    }
    // NOTE: if turning the knob feels "backwards" on your hardware, swap
    // ENCODER_A / ENCODER_B in platformio.ini rather than editing logic here.
}

void ScreenManager::handleButtonInput() {
    if (_cycle.empty()) return;

    unsigned long now = millis();
    bool currentState = digitalRead(koButtonPin);

    // Detect button press (LOW = pressed)
    if (currentState != lastButtonState) {
        if (now - lastInputTime > INPUT_DEBOUNCE) {
            lastInputTime = now;
            lastButtonState = currentState;

            if (currentState == LOW) {
                if (_switcherOpen) {
                    // KO is "choose this one" while the switcher is
                    // showing, instead of reaching the underlying screen.
                    Serial.println("[ScreenManager] KO pressed - confirming switcher selection");
                    closeSwitcherConfirm();
                } else {
                    // KO button pressed - call current screen's button handler
                    Serial.println("[ScreenManager] KO button pressed");
                    screens[_cycle[_cyclePos]]->onButtonPress();
                }
            }
        }
    }
}

// ============================================================================
// SCREEN SWITCHER OVERLAY
// ============================================================================
// A long-press-to-open, hold-nothing, KO-to-confirm picker over every screen
// in the cycle (same list the header's dot pager already walks). See the
// member comments in ScreenManager.h for the input-handling side of this;
// everything below is just opening/closing it and drawing it.

void ScreenManager::openSwitcher() {
    _switcherOpen = true;
    _switcherHighlight = _cyclePos;
    Serial.println("[ScreenManager] Switcher opened");
    drawSwitcher();
}

void ScreenManager::moveSwitcherHighlight(int direction) {
    int n = (int)_cycle.size();
    if (n <= 0) return;
    _switcherHighlight = ((_switcherHighlight + direction) % n + n) % n;
    drawSwitcher();
}

void ScreenManager::closeSwitcherCancel() {
    _switcherOpen = false;
    Serial.println("[ScreenManager] Switcher cancelled - staying put");

    // Nothing was chosen, so this deliberately does NOT go through
    // switchTo() - _cyclePos never moved, and re-init()ing a screen you
    // never actually left would reset things init() treats as a fresh
    // entry (GamesScreen drops back to its menu, for one). All that
    // actually happened here is the switcher painted over this screen's
    // pixels, so all it owes back is a real repaint of what's already
    // there - which init() also happens to do, and which every screen
    // already has to get right for the ordinary "cycled away and back"
    // case. FocusTimerScreen's init() comment spells out why that's safe
    // even mid-countdown; GamesScreen mid-round is the one case here that
    // still drops to its menu on cancel, exactly like cycling away and
    // back already does today - not a new rough edge, just an inherited
    // one, and worth a dedicated fix in GamesScreen separately if it's
    // ever worth doing.
    Screen* s = getCurrentScreen();
    if (s) s->init();
    if (_header) _header->invalidate();
    if (_legend) _legend->invalidate();
}

void ScreenManager::closeSwitcherConfirm() {
    _switcherOpen = false;
    switchTo(_switcherHighlight);
}

// Layout for the switcher's tile strip. Local to this file, not the header,
// since nothing outside drawSwitcher()/drawScreenIcon() needs them.
static const int SWITCHER_HEADER_H = 20;
static const int SWITCHER_TILE     = 50;
static const int SWITCHER_GAP      = 8;
static const int SWITCHER_VISIBLE  = 4;   // tiles shown at once, highlight always in slot 1

void ScreenManager::drawSwitcher() {
    if (_cycle.empty()) return;

    const Theme &theme = ThemeManager::current();
    const int n = (int)_cycle.size();

    // Clear everything below the header - the header itself is left alone
    // on purpose (still shows whatever screen is actually current, which
    // hasn't changed yet), so nothing needs invalidating there while this
    // is up.
    const int top = SWITCHER_HEADER_H;
    tft->fillRect(0, top, tft->width(), tft->height() - top, theme.bg);

    UiChrome::drawBracketTitle(tft, "SWITCH SCREEN", 12, top + 14, theme.fgDim);

    const int stripW = SWITCHER_VISIBLE * SWITCHER_TILE + (SWITCHER_VISIBLE - 1) * SWITCHER_GAP;
    const int stripX = (tft->width() - stripW) / 2;
    const int tileY  = top + 70;

    for (int slot = 0; slot < SWITCHER_VISIBLE; slot++) {
        // Slot 1 (second from the left) always shows the highlighted
        // screen; slot 0 is one step back, slots 2/3 are one and two steps
        // ahead. Wraps cleanly for any cycle length via the usual
        // "add n before the mod" trick for a possibly-negative index.
        int cycleIdx = ((_switcherHighlight - 1 + slot) % n + n) % n;
        bool highlighted = (slot == 1);
        int x = stripX + slot * (SWITCHER_TILE + SWITCHER_GAP);

        uint16_t tileBg    = highlighted ? theme.selectBg : theme.bg;
        uint16_t iconColor = highlighted ? theme.selectFg : theme.fgDim;

        tft->fillRoundRect(x, tileY, SWITCHER_TILE, SWITCHER_TILE, 8, tileBg);
        tft->drawRoundRect(x, tileY, SWITCHER_TILE, SWITCHER_TILE, 8, theme.rule);

        drawScreenIcon(tft, screens[_cycle[cycleIdx]]->id(),
                       x + SWITCHER_TILE / 2, tileY + SWITCHER_TILE / 2, iconColor, tileBg);
    }

    // Name of the highlighted screen, centered under the strip - confirms
    // what KO is actually about to switch to.
    Screen* hs = screens[_cycle[_switcherHighlight]];
    tft->setTextDatum(TC_DATUM);
    tft->setTextSize(2);
    tft->setTextColor(theme.fg, theme.bg);
    tft->drawString(hs->getName(), tft->width() / 2, tileY + SWITCHER_TILE + 14);

    tft->setTextSize(1);
    tft->setTextColor(theme.fgDim, theme.bg);
    tft->drawString("turn: browse   KO: switch   hold again: cancel",
                     tft->width() / 2, tileY + SWITCHER_TILE + 42);

    tft->setTextDatum(TL_DATUM);   // restore the default every other screen assumes
}

// Small hand-drawn glyphs in the same "primitive shapes, no fonts/bitmaps"
// style as the rest of this UI (see e.g. GamesScreen's Air Traffic plane) -
// this panel's fonts have no emoji, and nothing else in the firmware loads
// a custom font or bitmap, so a real icon set would be a new dependency for
// one small feature. Every icon below follows the same recipe on purpose,
// for a consistent set rather than seven one-off drawings: a bold solid
// silhouette in `fg`, with fine detail cut into it as negative space in
// `bg` (the exact color of whatever this tile's own background is - the
// caller passes the highlighted tile's selectBg or the dim tile's bg, so
// the cutouts always match, not just on one of the two states). One case
// per ScreenId; unknown ids get a plain dot rather than nothing, in case a
// screen is ever added here before its icon is.
void ScreenManager::drawScreenIcon(TFT_eSPI* tft, ScreenId id, int cx, int cy, uint16_t fg, uint16_t bg) {
    switch (id) {
        case ScreenId::Plane: {
            // Paper-plane / "send" silhouette, nose pointing right, with a
            // fold crease cut into the lower wing - the one detail that
            // turns a plain triangle into a recognizable paper plane. Both
            // crease lines run nose-to-base, so both endpoints sit on the
            // triangle's own boundary and the whole line stays inside its
            // fill (a segment between two points of a convex shape never
            // leaves it) instead of spilling out past the bottom edge.
            tft->fillTriangle(cx - 18, cy - 13, cx + 19, cy, cx - 18, cy + 13, fg);
            tft->drawLine(cx + 19, cy, cx - 18, cy + 6, bg);
            tft->drawLine(cx + 19, cy, cx - 18, cy + 8, bg);   // 2nd line for a ~2px crease
            break;
        }

        case ScreenId::Weather: {
            // Puffy cloud: three overlapping circles of different sizes
            // plus a filled "valley" between them so it reads as one solid
            // shape rather than three separate dots.
            tft->fillCircle(cx - 9, cy + 3, 9, fg);
            tft->fillCircle(cx + 3, cy - 6, 11, fg);
            tft->fillCircle(cx + 15, cy + 3, 8, fg);
            tft->fillRect(cx - 18, cy + 2, 41, 9, fg);
            break;
        }

        case ScreenId::Games: {
            // Two-lobe game-controller silhouette (a wide rounded body
            // reads better at this size than a "handles + bridge" shape),
            // with a d-pad and two face buttons cut into it.
            tft->fillRoundRect(cx - 20, cy - 10, 40, 20, 9, fg);
            tft->fillRect(cx - 13, cy - 1, 7, 2, bg);
            tft->fillRect(cx - 11, cy - 3, 2, 6, bg);
            tft->fillCircle(cx + 9, cy - 4, 2, bg);
            tft->fillCircle(cx + 14, cy, 2, bg);
            break;
        }

        case ScreenId::Focus: {
            // Hourglass: two triangles meeting at a point, with rounded
            // top/bottom caps.
            tft->fillRoundRect(cx - 11, cy - 13, 22, 3, 1, fg);
            tft->fillRoundRect(cx - 11, cy + 10, 22, 3, 1, fg);
            tft->fillTriangle(cx - 9, cy - 10, cx + 9, cy - 10, cx, cy, fg);
            tft->fillTriangle(cx - 9, cy + 10, cx + 9, cy + 10, cx, cy, fg);
            // A few grains already through the neck - small, but it's the
            // detail that says "timer" instead of just "geometric shape".
            tft->fillRect(cx - 1, cy + 2, 2, 2, bg);
            tft->fillRect(cx - 2, cy + 5, 2, 2, bg);
            break;
        }

        case ScreenId::Calendar: {
            // Solid page with two binder tabs poking out the top, a
            // cut-in header separator, and a small grid of date dots.
            tft->fillRoundRect(cx - 13, cy - 11, 26, 22, 3, fg);
            tft->fillRoundRect(cx - 8, cy - 15, 4, 6, 1, fg);
            tft->fillRoundRect(cx + 4, cy - 15, 4, 6, 1, fg);
            tft->fillRect(cx - 13, cy - 4, 26, 2, bg);
            for (int r = 0; r < 2; r++) {
                for (int c = 0; c < 3; c++) {
                    tft->fillRect(cx - 8 + c * 7, cy + r * 6, 2, 2, bg);
                }
            }
            break;
        }

        case ScreenId::Notes: {
            // Solid page with a folded top-right corner and a few lines of
            // "text" cut in.
            tft->fillRoundRect(cx - 11, cy - 13, 22, 26, 2, fg);
            tft->fillTriangle(cx + 5, cy - 13, cx + 11, cy - 13, cx + 11, cy - 7, bg);
            tft->fillRect(cx - 6, cy - 4, 14, 2, bg);
            tft->fillRect(cx - 6, cy + 1, 14, 2, bg);
            tft->fillRect(cx - 6, cy + 6, 9, 2, bg);
            break;
        }

        case ScreenId::Settings: {
            // Gear: a hub, a ring of eight teeth at fixed offsets (no trig
            // needed for a small fixed shape like this), and a punched-out
            // center hole.
            tft->fillCircle(cx, cy, 12, fg);
            static const int8_t OFFS[8][2] = {
                {0, -15}, {0, 15}, {-15, 0}, {15, 0},
                {11, -11}, {11, 11}, {-11, -11}, {-11, 11},
            };
            for (int i = 0; i < 8; i++) {
                tft->fillRect(cx + OFFS[i][0] - 3, cy + OFFS[i][1] - 3, 6, 6, fg);
            }
            tft->fillCircle(cx, cy, 5, bg);
            break;
        }

        default:
            tft->fillCircle(cx, cy, 4, fg);
            break;
    }
}
