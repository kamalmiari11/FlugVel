#include "checkin_api.h"
#include "../version.h"
#include "../network/CaptivePortal.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Same shared-secret pattern as UPDATE_MANIFEST_URL in update_check.cpp -
// hardcoded rather than user-configurable, since this project has exactly
// one dashboard. Must match the DEVICE_API_KEY Cloudflare Pages env var
// exactly (dashboard > flugvel-web > Settings > Environment variables) -
// rotate both together and reflash every unit if you ever change this.
static const char* CHECKIN_URL = "https://flugvel.com/api/devices/checkin";
static const char* DEVICE_API_KEY = "ccb43f2fde381fe49f127cfb6f4c229ea65331dc44f5a536";

void sendCheckin(const String& location) {
    if (WiFi.status() != WL_CONNECTED) return;

    StaticJsonDocument<256> doc;
    doc["device_id"] = CaptivePortal::deviceName();
    doc["location"] = location;
    doc["firmware_version"] = FIRMWARE_VERSION;
    doc["signal_dbm"] = WiFi.RSSI();

    String payload;
    serializeJson(doc, payload);

    HTTPClient http;
    http.setTimeout(8000);
    http.begin(CHECKIN_URL); // https:// - HTTPClient auto-uses a secure client internally, same as the other api/ files in this project
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Device-Key", DEVICE_API_KEY);

    int code = http.POST(payload);
    if (code != 200) {
        Serial.printf("[Checkin] Failed, HTTP %d\n", code);
    }
    http.end();
}
