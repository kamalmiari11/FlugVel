#pragma once
#include <Arduino.h>

struct UpdateInfo {
    bool checkSucceeded = false; // false if the manifest couldn't be fetched/parsed at all (no WiFi, host down, bad JSON, etc.)

    // Why it failed, so the screen can say something more useful than
    // "check failed": the HTTP status (404 = no manifest published, 302 =
    // an unfollowed redirect), 0 for no WiFi, -1 for a connection error,
    // or -2 when the body arrived but was not valid JSON.
    int  status = 0;
    bool updateAvailable = false;
    String version;
    String url;
    String notes;
};

// Fetches and parses the update manifest JSON from UPDATE_MANIFEST_URL (see
// update_check.cpp - point that at wherever you actually publish releases),
// comparing its "version" field against FIRMWARE_VERSION. Blocking (does
// its own HTTP GET) - call it somewhere that's fine to pause for a moment,
// like right after WiFi connects at boot, or from a Settings screen button
// press, not from inside a tight per-frame loop.
UpdateInfo checkForUpdate();

// Downloads and flashes the .bin at `url` using the device's built-in OTA
// mechanism, then reboots automatically on success - this function only
// ever returns if the update failed (device keeps running old firmware).
// `onProgressPercent`, if non-null, is called repeatedly during the
// download with 0-100 so a caller can show a progress bar.
bool performOTAUpdate(const String& url, void (*onProgressPercent)(int) = nullptr);