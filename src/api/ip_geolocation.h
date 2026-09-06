#pragma once
#include <Arduino.h>

// Fallback location auto-detection, used when the user didn't fill in the
// manual location fields AND didn't grant browser GPS access during setup.
// Uses ip-api.com (free, no API key) to estimate city/country/lat/lon/
// timezone from the device's public IP once it's actually online.
// Accuracy is city-level - good enough for weather + nearby-flight lookups,
// but not a substitute for real GPS.
bool fetchIpLocation(String &city, String &country, float &lat, float &lon, String &timezone);