#pragma once
#include <Arduino.h>

// One day of forecast data
struct WeatherDay {
    String dayLabel;     // "Today", "Mon", "Tue", ...
    String isoDate;      // "YYYY-MM-DD" - needed to fetch that day's hourly detail
    int weatherCode;     // WMO weather code (see weatherCodeToText)
    int highC;
    int lowC;
};

struct WeatherData {
    static const int MAX_DAYS = 7;

    int currentTempC;
    int currentWeatherCode;

    WeatherDay days[MAX_DAYS];
    int dayCount;

    WeatherData() : currentTempC(0), currentWeatherCode(0), dayCount(0) {}
};

// One hour of forecast data (used by the day-detail view)
struct HourPoint {
    int hour;         // 0-23
    int tempC;
    int weatherCode;
};

struct WeatherHourly {
    static const int MAX_HOURS = 24;

    HourPoint hours[MAX_HOURS];
    int hourCount;

    WeatherHourly() : hourCount(0) {}
};

// Fetches current conditions + a short forecast. Primary source is
// Open-Meteo (https://open-meteo.com - free, no key); if that fails for
// any reason it falls back to wttr.in (also free, no key, but only 3 days
// and 3-hourly, so dayCount / hourCount may be smaller). Returns true and
// fills `data` on success; on total failure `data` is left untouched so
// callers can keep showing the last-known-good values.
bool fetchWeather(WeatherData &data, float lat, float lon);

// Fetches an hour-by-hour breakdown for a single day (isoDate = "YYYY-MM-DD",
// as found in WeatherDay::isoDate). Only requests that one day's worth of
// data, so it stays cheap on RAM/bandwidth compared to pulling hourly data
// for the whole week up front.
bool fetchHourlyForecast(WeatherHourly &data, float lat, float lon, const String &isoDate);

// Maps an Open-Meteo/WMO weather code to a short human-readable label,
// e.g. 0 -> "Clear", 61 -> "Rain".
String weatherCodeToText(int code);