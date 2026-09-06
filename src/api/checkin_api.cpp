#include "checkin_api.h"
#include "../version.h"
#include "../network/CaptivePortal.h"
#include "update_check.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Defined in main.cpp - wraps CaptivePortal::factoryReset() (it owns the
// EEPROM/NVS layout). Same function the physical 15s button-hold and the
// on-device Settings > Factory reset menu item already call; this just
// gives the dashboard a third way to trigger it. Does not return.
extern void factoryReset();

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
        http.end();
        return;
    }

    // The response can carry a one-time "push this update now" instruction
    // (see web/functions/api/devices/checkin.js) - an admin queued a
    // specific release for this device from the dashboard, e.g. to fix a
    // struggling unit remotely without asking the owner to touch it. The
    // server hands it out exactly once and clears it on its side the
    // moment it's included in a response, so there's nothing here to
    // acknowledge - just apply it.
    String responseBody = http.getString();
    http.end();

    DynamicJsonDocument resp(512); // matches update_check.cpp's manifest parse - similarly small, version/url-shaped payload
    DeserializationError err = deserializeJson(resp, responseBody);
    if (err) return; // nothing more to do - the check-in itself already succeeded

    if (!resp["update"].isNull()) {
        String updateUrl = resp["update"]["url"].as<String>();
        String updateVersion = resp["update"]["version"].as<String>();
        if (updateUrl.length() > 0) {
            Serial.printf("[Checkin] Remote update queued: %s (%s)\n", updateVersion.c_str(), updateUrl.c_str());
            // Blocking, and reboots the device itself on success - fine to
            // call from here, this whole function already runs on the
            // background task's own core (see main.cpp's backgroundNetworkTask),
            // same reasoning as the flight/weather/quote fetches beside it.
            bool ok = performOTAUpdate(updateUrl, nullptr);
            if (!ok) {
                Serial.println("[Checkin] Remote-triggered update failed - staying on current firmware");
            }
        }
    }

    // Remote factory reset - deliberately the only remote command that
    // exists (see web/functions/api/devices/[id]/command.js). Wipes WiFi
    // credentials along with everything else and reboots into setup mode,
    // so this device won't check in again until someone is physically
    // there to reconnect it - that's the intended, understood trade-off
    // for a unit stuck in a bad state that a restart won't fix.
    String cmd = resp["command"].as<String>();
    if (cmd == "factory_reset") {
        Serial.println("[Checkin] Remote factory reset requested - wiping and rebooting into setup mode");
        factoryReset(); // does not return
    }
}
