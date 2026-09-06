#include "weather_api.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

// ================ WEATHER CODE -> TEXT (WMO codes) ================
// https://open-meteo.com/en/docs uses the standard WMO weather interpretation
// codes. The wttr.in fallback below reports WorldWeatherOnline codes instead,
// which wwoToWmo() maps onto this same set so the rest of the app only ever
// deals with WMO codes.
String weatherCodeToText(int code) {
    switch (code) {
        case 0:                return "Clear";
        case 1:                return "Mostly Clear";
        case 2:                return "Partly Cloudy";
        case 3:                return "Cloudy";
        case 45: case 48:      return "Fog";
        case 51: case 53: case 55: return "Drizzle";
        case 56: case 57:      return "Icy Drizzle";
        case 61: case 63: case 65: return "Rain";
        case 66: case 67:      return "Icy Rain";
        case 71: case 73: case 75: return "Snow";
        case 77:                return "Snow Grains";
        case 80: case 81: case 82: return "Showers";
        case 85: case 86:      return "Snow Showers";
        case 95:                return "Thunderstorm";
        case 96: case 99:      return "Thunder + Hail";
        default:                return "Unknown";
    }
}

// WorldWeatherOnline code -> nearest WMO code (used for the wttr.in fallback).
static int wwoToWmo(int w) {
    switch (w) {
        case 113: return 0;                       // sunny / clear
        case 116: return 2;                       // partly cloudy
        case 119: return 3;                       // cloudy
        case 122: return 3;                       // overcast
        case 143: case 248: case 260: return 45;  // mist / fog
        case 176: case 263: case 353: return 80;  // patchy / light rain shower
        case 179: case 227: case 323: case 329: case 368: return 71; // light snow
        case 182: case 185: case 281: case 284: case 311: case 314:
        case 317: case 350: case 362: case 365: return 66;           // sleet / freezing
        case 200: return 95;                      // thundery outbreaks
        case 266: case 293: case 296: case 299: case 302: return 61; // light-moderate rain
        case 305: case 308: case 356: case 359: return 65;           // heavy rain
        case 230: case 320: case 332: case 335: case 338: case 371:
        case 374: case 377: return 75;           // moderate-heavy snow / blizzard
        case 386: case 389: return 95;           // thundery rain
        case 392: case 395: return 96;           // thundery snow
        default:  return 3;                       // unknown -> cloudy
    }
}

// ================ DAY-OF-WEEK FROM "YYYY-MM-DD" ================
// Avoids relying on strptime/mktime for a single ISO date string.
// Zeller's congruence - no timezone/locale dependencies.
static String dayLabelForDate(const String &isoDate, bool isToday) {
    if (isToday) return "Today";

    int y, m, d;
    if (sscanf(isoDate.c_str(), "%d-%d-%d", &y, &m, &d) != 3) {
        return isoDate; // fallback: show raw date if parsing fails
    }

    if (m < 3) {
        m += 12;
        y -= 1;
    }

    int K = y % 100;
    int J = y / 100;
    int h = (d + (13 * (m + 1)) / 5 + K + K / 4 + J / 4 + 5 * J) % 7;
    // h: 0=Saturday, 1=Sunday, 2=Monday, ...
    static const char* names[] = { "Sat", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri" };
    return String(names[h]);
}

// ============================================================================
// PRIMARY SOURCE: Open-Meteo (free, no key, small responses)
// ============================================================================
static String buildOpenMeteoUrl(float lat, float lon) {
    String url = "https://api.open-meteo.com/v1/forecast?";
    url += "latitude=" + String(lat, 4);
    url += "&longitude=" + String(lon, 4);
    url += "&current=temperature_2m,weather_code";
    url += "&daily=weather_code,temperature_2m_max,temperature_2m_min";
    url += "&forecast_days=" + String(WeatherData::MAX_DAYS);
    url += "&timezone=auto";
    return url;
}

static bool fetchWeatherOpenMeteo(WeatherData &data, float lat, float lon) {
    String url = buildOpenMeteoUrl(lat, lon);
    Serial.printf("[Weather] open-meteo: %s\n", url.c_str());

    HTTPClient http;
    http.setTimeout(10000);
    http.begin(url);

    int code = http.GET();
    if (code < 0) {
        Serial.printf("[Weather] open-meteo error: %s\n", http.errorToString(code).c_str());
        http.end();
        return false;
    }
    if (code != HTTP_CODE_OK) {
        Serial.printf("[Weather] open-meteo HTTP %d\n", code);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.printf("[Weather] open-meteo JSON error: %s\n", err.c_str());
        return false;
    }
    if (!doc.containsKey("current") || !doc.containsKey("daily")) {
        Serial.println("[Weather] open-meteo: unexpected shape");
        return false;
    }

    WeatherData result;
    result.currentTempC       = (int)round((float)doc["current"]["temperature_2m"]);
    result.currentWeatherCode = doc["current"]["weather_code"] | 0;

    JsonArray dates = doc["daily"]["time"];
    JsonArray codes = doc["daily"]["weather_code"];
    JsonArray highs = doc["daily"]["temperature_2m_max"];
    JsonArray lows  = doc["daily"]["temperature_2m_min"];

    int count = min((int)dates.size(), (int)WeatherData::MAX_DAYS);
    result.dayCount = count;
    for (int i = 0; i < count; i++) {
        const char* dateStr = dates[i];
        String iso = dateStr ? String(dateStr) : "";
        result.days[i].isoDate     = iso;
        result.days[i].dayLabel    = dayLabelForDate(iso, i == 0);
        result.days[i].weatherCode = codes[i] | 0;
        result.days[i].highC       = (int)round((float)highs[i]);
        result.days[i].lowC        = (int)round((float)lows[i]);
    }

    data = result;
    Serial.printf("[Weather] open-meteo OK: %d C, code %d, %d days\n",
                  result.currentTempC, result.currentWeatherCode, result.dayCount);
    return true;
}

static bool fetchHourlyOpenMeteo(WeatherHourly &data, float lat, float lon, const String &isoDate) {
    String url = "https://api.open-meteo.com/v1/forecast?";
    url += "latitude=" + String(lat, 4);
    url += "&longitude=" + String(lon, 4);
    url += "&hourly=temperature_2m,weather_code";
    url += "&start_date=" + isoDate;
    url += "&end_date=" + isoDate;
    url += "&timezone=auto";
    Serial.printf("[Weather] open-meteo hourly %s: %s\n", isoDate.c_str(), url.c_str());

    HTTPClient http;
    http.setTimeout(10000);
    http.begin(url);

    int code = http.GET();
    if (code < 0) {
        Serial.printf("[Weather] open-meteo hourly error: %s\n", http.errorToString(code).c_str());
        http.end();
        return false;
    }
    if (code != HTTP_CODE_OK) {
        Serial.printf("[Weather] open-meteo hourly HTTP %d\n", code);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.printf("[Weather] open-meteo hourly JSON error: %s\n", err.c_str());
        return false;
    }
    if (!doc.containsKey("hourly")) return false;

    JsonArray times = doc["hourly"]["time"];
    JsonArray temps = doc["hourly"]["temperature_2m"];
    JsonArray codes = doc["hourly"]["weather_code"];

    WeatherHourly result;
    int count = min((int)times.size(), (int)WeatherHourly::MAX_HOURS);
    result.hourCount = count;
    for (int i = 0; i < count; i++) {
        const char* t = times[i]; // "YYYY-MM-DDTHH:MM"
        int hour = (t && strlen(t) >= 13) ? (t[11] - '0') * 10 + (t[12] - '0') : 0;
        result.hours[i].hour        = hour;
        result.hours[i].tempC       = (int)round((float)temps[i]);
        result.hours[i].weatherCode = codes[i] | 0;
    }

    data = result;
    Serial.printf("[Weather] open-meteo hourly OK: %d points for %s\n", count, isoDate.c_str());
    return true;
}

// ============================================================================
// BACKUP SOURCE: wttr.in ?format=j1 (free, no key). 3 days, 3-hourly.
// The j1 body is ~40 KB, so it's parsed with an ArduinoJson Filter (the
// kept document stays a couple KB) and only attempted when there's enough
// free heap to hold the transient body - better a stale screen than a
// reboot.
// ============================================================================
static const size_t WTTR_MIN_FREE_HEAP = 120000;

static bool wttrGet(float lat, float lon, String &bodyOut) {
    if (ESP.getFreeHeap() < WTTR_MIN_FREE_HEAP) {
        Serial.printf("[Weather] wttr: skipped, low heap (%u)\n", (unsigned)ESP.getFreeHeap());
        return false;
    }

    String url = "https://wttr.in/" + String(lat, 4) + "," + String(lon, 4) + "?format=j1";
    Serial.printf("[Weather] wttr fallback: %s\n", url.c_str());

    HTTPClient http;
    http.setTimeout(15000);
    http.setConnectTimeout(10000);
    http.begin(url);
    http.setUserAgent("curl/8 (FlugVel ESP32)");

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[Weather] wttr HTTP %d\n", code);
        http.end();
        return false;
    }
    bodyOut = http.getString();
    http.end();
    return bodyOut.length() > 0;
}

static int wttrRepresentativeCode(JsonArray hourly) {
    // Midday entry (index ~4 of 8) is a decent day-representative.
    int n = hourly.size();
    if (n == 0) return 3;
    JsonObject h = hourly[n / 2];
    return wwoToWmo(h["weatherCode"].as<int>());
}

static bool fetchWeatherWttr(WeatherData &data, float lat, float lon) {
    String body;
    if (!wttrGet(lat, lon, body)) return false;

    JsonDocument filter;
    filter["current_condition"][0]["temp_C"]            = true;
    filter["current_condition"][0]["weatherCode"]       = true;
    filter["weather"][0]["date"]                        = true;
    filter["weather"][0]["maxtempC"]                    = true;
    filter["weather"][0]["mintempC"]                    = true;
    filter["weather"][0]["hourly"][0]["weatherCode"]    = true;

    DynamicJsonDocument doc(6144);
    DeserializationError err =
        deserializeJson(doc, body, DeserializationOption::Filter(filter));
    if (err) {
        Serial.printf("[Weather] wttr JSON error: %s\n", err.c_str());
        return false;
    }

    JsonObject cur = doc["current_condition"][0];
    JsonArray  wx  = doc["weather"];
    if (cur.isNull() || wx.isNull() || wx.size() == 0) {
        Serial.println("[Weather] wttr: unexpected shape");
        return false;
    }

    WeatherData result;
    result.currentTempC       = atoi(cur["temp_C"] | "0");
    result.currentWeatherCode = wwoToWmo(atoi(cur["weatherCode"] | "0"));

    int count = min((int)wx.size(), (int)WeatherData::MAX_DAYS);
    result.dayCount = count;
    for (int i = 0; i < count; i++) {
        JsonObject d = wx[i];
        String iso = String((const char*)(d["date"] | ""));
        result.days[i].isoDate     = iso;
        result.days[i].dayLabel    = dayLabelForDate(iso, i == 0);
        result.days[i].highC       = atoi(d["maxtempC"] | "0");
        result.days[i].lowC        = atoi(d["mintempC"] | "0");
        result.days[i].weatherCode = wttrRepresentativeCode(d["hourly"]);
    }

    data = result;
    Serial.printf("[Weather] wttr OK: %d C, %d days\n", result.currentTempC, result.dayCount);
    return true;
}

static bool fetchHourlyWttr(WeatherHourly &data, float lat, float lon, const String &isoDate) {
    String body;
    if (!wttrGet(lat, lon, body)) return false;

    JsonDocument filter;
    filter["weather"][0]["date"]                     = true;
    filter["weather"][0]["hourly"][0]["time"]        = true;
    filter["weather"][0]["hourly"][0]["tempC"]       = true;
    filter["weather"][0]["hourly"][0]["weatherCode"] = true;

    DynamicJsonDocument doc(6144);
    DeserializationError err =
        deserializeJson(doc, body, DeserializationOption::Filter(filter));
    if (err) {
        Serial.printf("[Weather] wttr hourly JSON error: %s\n", err.c_str());
        return false;
    }

    JsonArray wx = doc["weather"];
    if (wx.isNull()) return false;

    for (JsonObject d : wx) {
        if (String((const char*)(d["date"] | "")) != isoDate) continue;

        JsonArray hourly = d["hourly"];
        WeatherHourly result;
        int count = min((int)hourly.size(), (int)WeatherHourly::MAX_HOURS);
        result.hourCount = count;
        for (int i = 0; i < count; i++) {
            JsonObject h = hourly[i];
            result.hours[i].hour        = atoi(h["time"] | "0") / 100;   // "1500" -> 15
            result.hours[i].tempC       = atoi(h["tempC"] | "0");
            result.hours[i].weatherCode = wwoToWmo(atoi(h["weatherCode"] | "0"));
        }
        data = result;
        Serial.printf("[Weather] wttr hourly OK: %d points for %s\n", count, isoDate.c_str());
        return count > 0;
    }

    Serial.printf("[Weather] wttr has no day %s\n", isoDate.c_str());
    return false;
}

// ============================================================================
// PUBLIC: try the primary, fall back to the backup.
// ============================================================================
bool fetchWeather(WeatherData &data, float lat, float lon) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Weather] WiFi not connected");
        return false;
    }
    Serial.println("\n==============================");
    if (fetchWeatherOpenMeteo(data, lat, lon)) { Serial.println("=============================="); return true; }
    Serial.println("[Weather] primary failed - trying backup source");
    bool ok = fetchWeatherWttr(data, lat, lon);
    Serial.println("==============================\n");
    return ok;
}

bool fetchHourlyForecast(WeatherHourly &data, float lat, float lon, const String &isoDate) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Weather] WiFi not connected");
        return false;
    }
    if (isoDate.length() == 0) return false;

    Serial.println("\n==============================");
    if (fetchHourlyOpenMeteo(data, lat, lon, isoDate)) { Serial.println("=============================="); return true; }
    Serial.println("[Weather] primary hourly failed - trying backup source");
    bool ok = fetchHourlyWttr(data, lat, lon, isoDate);
    Serial.println("==============================\n");
    return ok;
}
