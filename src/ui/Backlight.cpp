#include "Backlight.h"

// Diagnostic escape hatch: build with -D BACKLIGHT_NO_PWM to drive the
// backlight pin as a plain on/off output (digitalWrite) instead of LEDC
// PWM. If the flicker/dimming goes away with this defined, the cause is
// the PWM path (LEDC clock, channel conflict); if it persists, it's a
// power/wiring problem on the panel's LED rail, not this code.
namespace {
    constexpr uint8_t  kChannel    = 4;      // LEDC 0-7 are high-speed; 0/1 are used by tone()
    constexpr uint32_t kFrequency  = 20000;  // well above anything visible or audible
    constexpr uint8_t  kResBits    = 8;
    constexpr uint8_t  kMinPercent = 5;      // "on" never goes below this

    bool    s_started = false;
    uint8_t s_percent = 100;

    // Map 0-100 (perceptual) to a 0-255 PWM duty. Gamma ~2.2 so the dim
    // end has usable resolution instead of jumping from black to bright in
    // the first few percent.
    uint32_t dutyForPercent(uint8_t percent) {
        if (percent == 0) return 0;
        float p = percent / 100.0f;
        float corrected = powf(p, 2.2f);
        uint32_t duty = (uint32_t)lroundf(corrected * 255.0f);
        if (duty < 1) duty = 1;
        return duty;
    }
}

namespace Backlight {

void begin(uint8_t startPercent) {
    if (!s_started) {
#ifdef BACKLIGHT_NO_PWM
        pinMode(TFT_BL, OUTPUT);
#else
        ledcSetup(kChannel, kFrequency, kResBits);
        ledcAttachPin(TFT_BL, kChannel);
#endif
        s_started = true;
    }
    set(startPercent);
}

void set(uint8_t percent) {
    if (!s_started) begin(percent);

    if (percent != 0 && percent < kMinPercent) percent = kMinPercent;
    if (percent > 100) percent = 100;
    s_percent = percent;

#ifdef BACKLIGHT_NO_PWM
    digitalWrite(TFT_BL, percent > 0 ? HIGH : LOW);
#else
    ledcWrite(kChannel, dutyForPercent(percent));
#endif
}

uint8_t get() {
    return s_percent;
}

void fadeTo(uint8_t percent, uint16_t durationMs) {
    if (!s_started) begin(0);

    if (percent != 0 && percent < kMinPercent) percent = kMinPercent;
    if (percent > 100) percent = 100;

#ifdef BACKLIGHT_NO_PWM
    set(percent);   // no dimming without PWM - just snap
    return;
#else
    int from = s_percent;
    int to   = percent;
    if (from == to) { set(percent); return; }

    const int steps = 24;
    const int stepDelay = max(1, durationMs / steps);
    for (int i = 1; i <= steps; i++) {
        int v = from + (to - from) * i / steps;
        set((uint8_t)v);
        delay(stepDelay);
    }
    set(percent);
#endif
}

} // namespace Backlight
