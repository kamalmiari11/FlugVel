#pragma once
#include <Arduino.h>

// User-selectable display formats (Settings > Date & clock / Temperature).
// A tiny static holder, same pattern as ThemeManager: main.cpp seeds it
// from the persisted CaptivePortal values at boot, the Settings screen
// updates it live, and every screen that shows a clock or a temperature
// reads it here instead of hardcoding a format.
namespace Units {

// ---- Date & clock ----
// 0: 24h  DD-MM-YYYY      2: 12h  DD-MM-YYYY
// 1: 24h  MM-DD-YYYY      3: 12h  MM-DD-YYYY
static const int DATETIME_FORMAT_COUNT = 4;

void begin(uint8_t dateTimeFormat, bool imperial);

void    setDateTimeFormat(uint8_t fmt);   // clamped to [0, DATETIME_FORMAT_COUNT)
uint8_t dateTimeFormat();

const char* strftimeDate();   // e.g. "%d-%m-%Y"
const char* strftimeTime();   // "%H:%M" or "%I:%M %p"
const char* dateTimeLabel();  // short menu tag, e.g. "24h D-M"

// ---- Measurement system (Metric / Imperial) ----
// One toggle drives temperature, speed and altitude at once.
void setImperial(bool on);
bool imperial();

int  temp(int celsius);        // -> Celsius as-is, or converted to Fahrenheit
char tempUnit();               // 'C' or 'F'

int  speed(float metresPerSec);   // -> km/h (metric) or mph (imperial), rounded
const char* speedUnit();          // "km/h" or "mph"

int  altitude(int metres);        // -> metres (metric) or feet (imperial), rounded
const char* altitudeUnit();       // "m" or "ft"

} // namespace Units
