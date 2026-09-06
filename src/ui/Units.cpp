#include "Units.h"

namespace {
    struct Fmt {
        const char* date;
        const char* time;
        const char* label;
    };
    // Indexed by dateTimeFormat (0..3).
    const Fmt kFmts[Units::DATETIME_FORMAT_COUNT] = {
        { "%d-%m-%Y", "%H:%M",    "24h D-M" },
        { "%m-%d-%Y", "%H:%M",    "24h M-D" },
        { "%d-%m-%Y", "%I:%M %p", "12h D-M" },
        { "%m-%d-%Y", "%I:%M %p", "12h M-D" },
    };

    uint8_t s_fmt = 0;
    bool    s_imperial = false;
}

namespace Units {

void begin(uint8_t dateTimeFormat, bool imperialTemp) {
    setDateTimeFormat(dateTimeFormat);
    s_imperial = imperialTemp;
}

void setDateTimeFormat(uint8_t fmt) {
    s_fmt = (fmt < DATETIME_FORMAT_COUNT) ? fmt : 0;
}
uint8_t dateTimeFormat()   { return s_fmt; }

const char* strftimeDate()  { return kFmts[s_fmt].date; }
const char* strftimeTime()  { return kFmts[s_fmt].time; }
const char* dateTimeLabel() { return kFmts[s_fmt].label; }

void setImperial(bool on) { s_imperial = on; }
bool imperial()           { return s_imperial; }

int temp(int celsius) {
    if (!s_imperial) return celsius;
    return (int)lroundf(celsius * 9.0f / 5.0f + 32.0f);
}
char tempUnit() { return s_imperial ? 'F' : 'C'; }

int speed(float metresPerSec) {
    float v = s_imperial ? metresPerSec * 2.236936f   // m/s -> mph
                         : metresPerSec * 3.6f;        // m/s -> km/h
    return (int)lroundf(v);
}
const char* speedUnit() { return s_imperial ? "mph" : "km/h"; }

int altitude(int metres) {
    if (!s_imperial) return metres;
    return (int)lroundf(metres * 3.280840f);           // m -> ft
}
const char* altitudeUnit() { return s_imperial ? "ft" : "m"; }

} // namespace Units
