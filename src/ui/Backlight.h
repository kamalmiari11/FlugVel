#pragma once
#include <Arduino.h>

// ============ TFT BACKLIGHT (PWM) ============
// The panel's LED backlight is on TFT_BL (see platformio.ini). It used to
// be driven with a bare digitalWrite(TFT_BL, HIGH), which has two problems:
//
//   1. No dimming - it's either full brightness or off.
//   2. The hard 0->full step at boot happens *before* the panel RAM is
//      cleared, so for a few ms you see a bright flash of whatever garbage
//      was in the ST7789's frame buffer. Users read that as a flicker.
//
// This wraps the pin in an LEDC PWM channel (20 kHz - well above anything
// visible or audible) so brightness is adjustable and the boot-time light
// can be faded up smoothly once the screen is already black.
//
// All functions are safe to call before or after tft.init(); they only
// touch the LEDC peripheral and the TFT_BL pin.
namespace Backlight {

// Configure the LEDC channel and set the initial brightness (0-100).
// Pass 0 to bring the pin up dark, then call fadeTo()/set() once the
// panel has been cleared.
void begin(uint8_t startPercent = 100);

// Set brightness immediately, 0-100. Values are clamped to [MIN, 100] so
// "on" is never so dim the screen looks dead; use 0 only to fully cut the
// light. A perceptual curve is applied so the low end isn't all bunched up.
void set(uint8_t percent);

// Current brightness percent (the last value passed to set()/begin()/
// fadeTo(), not re-derived from the hardware).
uint8_t get();

// Blocking linear fade from the current brightness to `percent` over
// roughly `durationMs`. Cheap - used at boot and on theme/sleep changes,
// not in the animation hot path.
void fadeTo(uint8_t percent, uint16_t durationMs);

} // namespace Backlight
