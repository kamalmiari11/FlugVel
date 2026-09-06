#include "update_check.h"
#include "../version.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>

// Hosted wherever you actually publish releases - a raw GitHub file works
// fine and is free (Settings > Repo > raw.githubusercontent.com/.../file).
// REPLACE THIS before relying on the update check for anything real.
//
// Expected JSON shape:
//   { "version": "1.1.0", "url": "https://.../flugvel_1_1_0.bin", "notes": "..." }
static const char* UPDATE_MANIFEST_URL = "https://raw.githubusercontent.com/kamalmiari11/FlugVel/refs/heads/main/update_manifest.json";

// Small "is a newer than b" comparator for plain dotted version strings
// like "1.2.10" - compares each numeric segment in turn rather than as a
// whole string, so "1.10.0" correctly beats "1.9.0" (a plain string
// compare would get that backwards, same trap as comparing IP address
// octets as strings).
static bool isNewerVersion(const String& a, const String& b) {
    int aIndex = 0, bIndex = 0;
    while (aIndex < (int)a.length() || bIndex < (int)b.length()) {
        int aNext = a.indexOf('.', aIndex);
        int bNext = b.indexOf('.', bIndex);
        int aPart = (aNext == -1) ? a.substring(aIndex).toInt() : a.substring(aIndex, aNext).toInt();
        int bPart = (bNext == -1) ? b.substring(bIndex).toInt() : b.substring(bIndex, bNext).toInt();
        if (aPart != bPart) return aPart > bPart;
        aIndex = (aNext == -1) ? a.length() : aNext + 1;
        bIndex = (bNext == -1) ? b.length() : bNext + 1;
    }
    return false; // identical version strings
}

UpdateInfo checkForUpdate() {
    UpdateInfo info;

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Update] Skipped check - no WiFi");
        info.status = 0;
        return info;
    }

    HTTPClient http;
    // GitHub serves raw files from a CDN and redirects to it; without this
    // the GET comes back 301/302 and reads as a failure. calendar_api and
    // flight_api already do the same, for the same reason.
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.begin(UPDATE_MANIFEST_URL); // https:// URL - HTTPClient auto-uses a secure client internally, same as the other api/ files in this project
    http.setTimeout(8000);
    int httpCode = http.GET();
    info.status = httpCode;

    if (httpCode != 200) {
        Serial.printf("[Update] Manifest fetch failed, HTTP %d\n", httpCode);
        http.end();
        return info;
    }

    String body = http.getString();
    http.end();

    DynamicJsonDocument doc(512); // manifest is tiny - version/url/notes only
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[Update] Manifest JSON parse failed: %s\n", err.c_str());
        info.status = -2;
        return info;
    }

    info.checkSucceeded = true;
    info.version = doc["version"].as<String>();
    info.url = doc["url"].as<String>();
    info.notes = doc["notes"].as<String>();
    info.updateAvailable = info.version.length() > 0 && isNewerVersion(info.version, FIRMWARE_VERSION);

    Serial.printf("[Update] Current: %s | Latest: %s | Available: %s\n",
                  FIRMWARE_VERSION, info.version.c_str(), info.updateAvailable ? "yes" : "no");

    return info;
}

static void (*g_progressCallback)(int) = nullptr;

static void onHttpUpdateProgress(int cur, int total) {
    if (g_progressCallback && total > 0) {
        g_progressCallback((cur * 100) / total);
    }
}

bool performOTAUpdate(const String& url, void (*onProgressPercent)(int)) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Update] Cannot update - no WiFi");
        return false;
    }

    g_progressCallback = onProgressPercent;
    httpUpdate.onProgress(onHttpUpdateProgress);

    // GitHub Release asset downloads respond with a 302 redirect to a CDN
    // URL (objects.githubusercontent.com) rather than serving the file
    // directly - without this, HTTPUpdate treats that redirect itself as
    // an invalid response and fails with "Wrong HTTP Code" before ever
    // reaching the actual .bin.
    httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

    Serial.printf("[Update] Downloading: %s\n", url.c_str());

    // Assumes an https:// firmware URL (GitHub Releases or any TLS host
    // both work) - HTTPUpdate, unlike HTTPClient::begin(url), needs the
    // client type to already match the scheme, so this uses
    // WiFiClientSecure directly rather than auto-detecting.
    WiFiClientSecure client;
    client.setInsecure(); // matches this codebase's other HTTPS calls (no certificate pinning elsewhere either)

    t_httpUpdate_return result = httpUpdate.update(client, url);

    switch (result) {
        case HTTP_UPDATE_FAILED:
            Serial.printf("[Update] Failed (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
            return false;
        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("[Update] Server reported no update available");
            return false;
        case HTTP_UPDATE_OK:
            // httpUpdate reboots the device itself on success - this line
            // normally never actually runs.
            Serial.println("[Update] Success - rebooting");
            return true;
    }
    return false;
}