#include "ScreenManager.h"
#include "ScreenRegistry.h"
#include "../config/Config.h"
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
    Screen* s = getCurrentScreen();
    if (s) s->update();
}

void ScreenManager::draw() {
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

void ScreenManager::handleEncoderInput() {
    if (_cycle.empty()) return;

    unsigned long now = millis();
    bool currentState = digitalRead(encoderBtnPin);

    // Detect button press (LOW = pressed)
    if (currentState != lastEncoderState) {
        if (now - lastInputTime > INPUT_DEBOUNCE) {
            lastInputTime = now;
            lastEncoderState = currentState;

            if (currentState == LOW) {
                // Encoder button pressed - step to the next screen, unless
                // the current screen has locked that out (see
                // Screen::allowsScreenSwitch()'s comment - FocusTimerScreen
                // does this while a countdown is running).
                if (!screens[_cycle[_cyclePos]]->allowsScreenSwitch()) {
                    Serial.println("[ScreenManager] Encoder button ignored - current screen has locked screen switching");
                    return;
                }

                // The cycle is already in the right order (home first,
                // Settings last), so "next screen" is one step forward with
                // a wrap - nothing to compute.
                _cyclePos = (_cyclePos + 1) % (int)_cycle.size();

                Screen* s = screens[_cycle[_cyclePos]];
                s_showing = s->id();

                // Remembered so the device can come back to it after a
                // power cut. Written to NVS only on an actual screen change,
                // which is a handful of times a day, not per loop.
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
    // single notch of the knob results in exactly one onEncoderUp/Down call.
    while (encoderAccum >= STEPS_PER_DETENT) {
        s->onEncoderUp();
        encoderAccum -= STEPS_PER_DETENT;
    }
    while (encoderAccum <= -STEPS_PER_DETENT) {
        s->onEncoderDown();
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
                // KO button pressed - call current screen's button handler
                Serial.println("[ScreenManager] KO button pressed");
                screens[_cycle[_cyclePos]]->onButtonPress();
            }
        }
    }
}
