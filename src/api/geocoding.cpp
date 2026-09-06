#include "geocoding.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Minimal URL-encoding - good enough for city names (spaces, accented
// letters, etc). HTTPClient won't do this for us.
static String urlEncode(const String &s) {
    String out;
    out.reserve(s.length() * 3);
    const char* hex = "0123456789ABCDEF";
    for (size_t i = 0; i < s.length(); i++) {
        uint8_t c = (uint8_t)s[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

bool geocodeCity(const String &city, const String &countryHint,
                  float &lat, float &lon,
                  String &resolvedCity, String &resolvedCountry,
                  String &timezone)
{
    if (city.length() == 0) return false;

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Geocode] WiFi not connected");
        return false;
    }

    String url = "https://geocoding-api.open-meteo.com/v1/search?name=" + urlEncode(city);
    url += "&count=10&language=en&format=json";

    Serial.println("\n==============================");
    Serial.printf("[Geocode] Looking up: %s\n", city.c_str());

    HTTPClient http;
    http.setTimeout(10000);
    http.begin(url);

    int code = http.GET();
    if (code < 0) {
        Serial.printf("[Geocode] ERROR: %s\n", http.errorToString(code).c_str());
        http.end();
        return false;
    }
    if (code != HTTP_CODE_OK) {
        Serial.printf("[Geocode] HTTP error %d\n", code);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.print("[Geocode] JSON ERROR: ");
        Serial.println(err.c_str());
        return false;
    }

    JsonArray results = doc["results"];
    if (results.isNull() || results.size() == 0) {
        Serial.println("[Geocode] No matches found");
        return false;
    }

    // Open-Meteo already ranks results by relevance/population, so the
    // first entry is normally the right one. If the user gave a country
    // hint, prefer a result matching it (handles e.g. "Springfield" which
    // exists in many countries) but still fall back to the top result if
    // nothing matches the hint.
    JsonObject best = results[0];

    if (countryHint.length() > 0) {
        for (JsonObject r : results) {
            const char* country = r["country"];
            const char* countryCode = r["country_code"];
            if ((country && countryHint.equalsIgnoreCase(country)) ||
                (countryCode && countryHint.equalsIgnoreCase(countryCode))) {
                best = r;
                break;
            }
        }
    }

    lat = best["latitude"] | 0.0f;
    lon = best["longitude"] | 0.0f;
    resolvedCity = String((const char*)(best["name"] | city.c_str()));
    resolvedCountry = String((const char*)(best["country"] | ""));
    timezone = String((const char*)(best["timezone"] | "UTC"));

    Serial.printf("[Geocode] Matched: %s, %s (%.4f, %.4f) tz=%s\n",
                  resolvedCity.c_str(), resolvedCountry.c_str(), lat, lon, timezone.c_str());
    Serial.println("==============================\n");

    return true;
}