#include "ip_geolocation.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

bool fetchIpLocation(String &city, String &country, float &lat, float &lon, String &timezone) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[IPGeo] WiFi not connected");
        return false;
    }

    // ip-api.com's free tier is plain HTTP only (no HTTPS) - fine here since
    // we're only fetching a rough location estimate, nothing sensitive.
    String url = "http://ip-api.com/json/?fields=status,message,country,city,lat,lon,timezone";

    Serial.println("\n==============================");
    Serial.println("[IPGeo] Auto-detecting location from IP...");

    HTTPClient http;
    http.setTimeout(10000);
    http.begin(url);

    int code = http.GET();

    if (code < 0) {
        Serial.printf("[IPGeo] ERROR: %s\n", http.errorToString(code).c_str());
        http.end();
        return false;
    }

    if (code != HTTP_CODE_OK) {
        Serial.printf("[IPGeo] HTTP error %d\n", code);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(doc, payload);

    if (err) {
        Serial.print("[IPGeo] JSON ERROR: ");
        Serial.println(err.c_str());
        return false;
    }

    const char* status = doc["status"];
    if (!status || String(status) != "success") {
        Serial.print("[IPGeo] Lookup failed: ");
        Serial.println(doc["message"] | "unknown error");
        return false;
    }

    city = String((const char*)(doc["city"] | ""));
    country = String((const char*)(doc["country"] | ""));
    lat = doc["lat"] | 0.0f;
    lon = doc["lon"] | 0.0f;
    timezone = String((const char*)(doc["timezone"] | "UTC")); // already an IANA tz name, e.g. "Europe/Amsterdam"

    Serial.printf("[IPGeo] Detected: %s, %s (%.4f, %.4f) tz=%s\n",
                  city.c_str(), country.c_str(), lat, lon, timezone.c_str());
    Serial.println("==============================\n");

    return true;
}