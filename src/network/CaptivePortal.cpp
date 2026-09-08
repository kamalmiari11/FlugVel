#include "CaptivePortal.h"
#include "../config/Config.h"
#include "../screens/ScreenRegistry.h"
#include "../config/CalendarFeeds.h"
#include "../config/NotesSource.h"
#include "../screens/GamesScreen.h"
#include "../ui/Theme.h"
#include <nvs_flash.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <esp_mac.h>

// Timezone array for dropdown (only used in manual mode)
// Exposed for PortalPage.cpp, which builds the timezone dropdown.
const char *timezones[] = {
    "UTC", "Europe/London", "Europe/Amsterdam", "Europe/Berlin", "Europe/Paris",
    "America/New_York", "America/Chicago", "America/Denver", "America/Los_Angeles",
    "Asia/Tokyo", "Asia/Shanghai", "Asia/Kolkata", "Australia/Sydney"
    // ... add more as needed
};
// extern, because a namespace-scope const has internal linkage in C++ by
// default and PortalPage.cpp needs to see it.
extern const int timezoneCount;
extern const int timezoneCount = sizeof(timezones) / sizeof(timezones[0]);

String CaptivePortal::deviceName()
{
    uint8_t mac[6];
    // Read the station MAC specifically. WiFi.macAddress() would return
    // whatever the current mode implies, and the AP MAC differs from the
    // STA one by a byte - so the name would change depending on when it
    // was asked. esp_read_mac(WIFI_STA) is stable in every mode.
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    char name[20];
    snprintf(name, sizeof(name), "flugvel-%02x%02x", mac[4], mac[5]);
    return String(name);
}

String CaptivePortal::mdnsHost()
{
    return deviceName() + ".local";
}

CaptivePortal::CaptivePortal(const char *ssid, const char *password)
    : _ssid(ssid), _password(password), _city(""), _country(""), _latitude(0), _longitude(0), _timezone(""),
      _bounceText(""), _facingDirection("") {}

// ---- Calendar (.ics) URL - NVS-backed, too long for the EEPROM block ----
void CaptivePortal::loadCalendarUrl()
{
    Preferences p;
    if (p.begin("flugvel", true)) {          // read-only
        _calendarUrl = p.getString("calurl", "");
        p.end();
    }
}

void CaptivePortal::setCalendarUrl(const String &url)
{
    _calendarUrl = url;
    Preferences p;
    if (p.begin("flugvel", false)) {
        p.putString("calurl", url);
        p.end();
    }
}

void CaptivePortal::factoryReset()
{
    Serial.println("[CaptivePortal] Factory reset - wiping all persisted data");

    WiFi.disconnect(true, true);

    EEPROM.begin(512);
    for (int i = 0; i < 512; i++)
        EEPROM.write(i, 0xFF);
    EEPROM.commit();

    nvs_flash_erase();
    nvs_flash_init();

    delay(1000);
    ESP.restart();
}

void CaptivePortal::beginAP()
{
    if (_running) return;

    // 1️⃣ Temporarily STA mode for scanning
    WiFi.mode(WIFI_STA);
    delay(200); // give Wi-Fi hardware time to initialize

    // 2️⃣ Scan networks
    scanNetworks();

    // 3️⃣ Start AP after scanning
    WiFi.mode(WIFI_AP); // AP only
    WiFi.softAP(_ssid, _password, 1);
    delay(200);

    // 4️⃣ Start DNS and HTTP server
    dnsServer.start(53, "*", WiFi.softAPIP());
    if (!_routesUp) { setupRoutes(); _routesUp = true; }
    server.begin();

    _running  = true;
    _apMode   = true;
    _closeRequested = false;
    _sessionToken = "";   // the AP password is the gate here; see configPin()

    Serial.println("Captive Portal started!");
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
}

// Serve the same pages over the Wi-Fi the device is already joined to.
// No softAP, and no DNS hijack - hijacking DNS on someone's home network
// would break every other device on it.
void CaptivePortal::beginSTA()
{
    if (_running) return;
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[CaptivePortal] beginSTA() with no connection - caller should fall back to beginAP()");
        return;
    }

    // A fresh code each time, so one that was seen over someone's shoulder
    // does not keep working.
    _configPin    = (uint16_t)random(1000, 10000);
    _sessionToken = String(random(0x10000000, 0x7FFFFFFF), HEX) +
                    String(random(0x10000000, 0x7FFFFFFF), HEX);
    _closeRequested = false;

    // Kick a scan off but do NOT wait for it. A blocking scan is several
    // seconds, and this runs on the main task with the QR page about to be
    // drawn - the panel would just sit there. By the time anyone has
    // pointed a phone at the code and loaded the page, the results are in;
    // collectScanResults() picks them up when the page is built.
    startScanAsync();

    if (!_routesUp) { setupRoutes(); _routesUp = true; }
    server.begin();

    _running = true;
    _apMode  = false;

    // Answer to flugvel-xxxx.local on this LAN. There is no server behind
    // this - the device joins a multicast group and replies to queries for
    // its own name itself, so it works on a network with no internet and
    // needs nothing configured on the router.
    //
    // Deliberately not fatal if it fails: mDNS is a convenience on top of
    // the IP address, never the only way in. Android in particular often
    // will not resolve a typed .local URL in the browser, which is why the
    // panel still prints the raw address.
    if (MDNS.begin(deviceName().c_str())) {
        MDNS.addService("http", "tcp", 80);
        _mdnsUp = true;
        Serial.printf("[CaptivePortal] mDNS up as %s\n", mdnsHost().c_str());
    } else {
        Serial.println("[CaptivePortal] mDNS failed to start - IP still works");
    }

    Serial.print("[CaptivePortal] Config page at http://");
    Serial.print(WiFi.localIP());
    Serial.printf("/  PIN %04u\n", _configPin);
}

void CaptivePortal::stop()
{
    if (!_running) return;

    server.stop();
    if (_apMode) {
        dnsServer.stop();
        WiFi.softAPdisconnect(true);
    }

    // Stop advertising once the web server is gone, so the name does not
    // resolve to a device with nothing listening on port 80. This also
    // frees the responder before an OTA, which is the RAM-tight moment
    // this method's contract exists to protect.
    if (_mdnsUp) {
        MDNS.end();
        _mdnsUp = false;
    }

    _running  = false;
    _apMode   = false;
    _sessionToken   = "";
    _closeRequested = false;

    Serial.println("[CaptivePortal] Stopped");
}

// In STA mode the page sits on the user's LAN, where anyone can reach it -
// and it can factory-reset the device. Requiring a code that is only
// visible on the panel binds "can configure it" to "can see it". Kept
// deliberately simple: one flag for the life of the session, no cookies,
// because there is exactly one device and one person standing in front of
// it. Returns true if it served the prompt and the caller must stop.
bool CaptivePortal::guardWithPin()
{
    if (_apMode) return false;

    // Already holding a valid session cookie.
    if (_sessionToken.length() && server.hasHeader("Cookie")) {
        String cookie = server.header("Cookie");
        if (cookie.indexOf("fv=" + _sessionToken) >= 0) return false;
    }

    // Correct PIN: hand out the cookie. Anyone else who follows the link
    // without it still lands here and has to read the code off the panel.
    if (server.hasArg("pin") && server.arg("pin").toInt() == (long)_configPin) {
        server.sendHeader("Set-Cookie", "fv=" + _sessionToken + "; Path=/; Max-Age=3600; SameSite=Lax");
        return false;
    }

    bool wrong = server.hasArg("pin");
    // h1/label/input brought in line with the main config page's own scale
    // (PortalPage.cpp: label 11px uppercase dim, input font:inherit, button
    // font-size 14px) - this page used to freelance its own sizes (17px
    // title next to a 22px/8px-spaced input with no label at all), which
    // read as mismatched next to the rest of the site's forms.
    String page = R"rawliteral(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1.0"><meta charset="utf-8">
<title>FlugVel</title><style>
body{margin:0;padding:60px 16px;background:#c8d0b8;color:#23271d;
 font-family:ui-monospace,Menlo,Consolas,monospace;font-size:15px;text-align:center;}
h1{font-size:20px;letter-spacing:4px;margin:0 0 6px;font-weight:700;}
p{color:#5a6048;font-size:13px;margin:0 0 26px;}
form{max-width:280px;margin:0 auto;}
label{display:block;font-size:11px;letter-spacing:1px;text-transform:uppercase;color:#5a6048;margin:0 0 6px;text-align:left;}
input{width:100%;padding:14px;border:1px solid #8a9078;background:#eef1e4;color:#23271d;
 font:inherit;font-size:20px;letter-spacing:6px;text-align:center;border-radius:0;}
button{width:100%;padding:14px;margin-top:14px;border:1px solid #c05a1e;background:#c05a1e;
 color:#fff;font:inherit;font-size:14px;letter-spacing:2px;text-transform:uppercase;cursor:pointer;border-radius:0;}
.err{color:#9c3312;font-size:12px;margin-top:12px;}
</style></head><body>
<h1>[ FLUGVEL ]</h1><p>Enter the code shown on the device</p>
<form method="POST" action="/">
<label for="pin">Code</label>
<input id="pin" type="text" name="pin" inputmode="numeric" maxlength="4" autofocus>
<button type="submit">Unlock</button>)rawliteral";

    if (wrong) page += "<p class=\"err\">That code did not match.</p>";
    page += "</form></body></html>";

    server.send(200, "text/html", page);
    return true;
}

void CaptivePortal::startScanAsync()
{
    scannedNetworksHTML = "";        // "still scanning" until results land
    WiFi.scanDelete();
    WiFi.scanNetworks(/*async=*/true);
}

// Turns a finished async scan into the option list. Safe to call whenever -
// it does nothing while a scan is still running, and nothing again once the
// results have already been folded in.
void CaptivePortal::collectScanResults()
{
    if (scannedNetworksHTML.length() > 0) return;

    int n = WiFi.scanComplete();
    if (n < 0) return;               // -1 running, -2 never started

    String current = WiFi.SSID();
    scannedNetworksHTML = "<select name='ssid' required>";
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;
        int rssi = WiFi.RSSI(i);
        const char *quality = rssi >= -55 ? "excellent"
                            : rssi >= -67 ? "good"
                            : rssi >= -78 ? "weak" : "poor";
        bool isCurrent = (current.length() > 0 && ssid == current);
        scannedNetworksHTML += String("<option value='") + ssid + "'" +
                               (isCurrent ? " selected" : "") + ">" + ssid +
                               "  (" + String(rssi) + " dBm, " + quality + ")" +
                               (isCurrent ? "  \u2014 connected" : "") + "</option>";
    }
    scannedNetworksHTML += "</select>";
    WiFi.scanDelete();
}

void CaptivePortal::scanNetworks()
{
    Serial.println("Scanning Wi-Fi networks...");

    int n = WiFi.scanNetworks(); // blocking scan
    // A plain <select>, so the browser submits the choice with no
    // JavaScript involved - this is the one form that has to work even if
    // everything else on the page fails. Signal strength rides along in the
    // option text. The network the device is already on is preselected.
    String current = WiFi.SSID();
    scannedNetworksHTML = "<select name='ssid' required>";

    if (n <= 0)
    {
        scannedNetworksHTML += "<option value=''>No networks found</option>";
        Serial.println("No Wi-Fi networks found!");
    }
    else
    {
        for (int i = 0; i < n; i++)
        {
            String ssid = WiFi.SSID(i);
            int rssi = WiFi.RSSI(i);

            // skip hidden SSIDs
            if (ssid.length() == 0)
                continue;

            const char *quality = rssi >= -55 ? "excellent"
                                : rssi >= -67 ? "good"
                                : rssi >= -78 ? "weak" : "poor";
            bool isCurrent = (current.length() > 0 && ssid == current);
            scannedNetworksHTML += String("<option value='") + ssid + "'" +
                                   (isCurrent ? " selected" : "") + ">" + ssid +
                                   "  (" + String(rssi) + " dBm, " + quality + ")" +
                                   (isCurrent ? "  \u2014 connected" : "") + "</option>";
            Serial.println("Found SSID: " + ssid + " (" + String(rssi) + " dBm)");
        }
    }



    scannedNetworksHTML += "</select>";

    // Free memory used by scan
    WiFi.scanDelete();
}

// Applies the location half of a form post. Shared by /connect (first-run
// setup, where Wi-Fi and location are saved together) and /location (a
// later visit, where the device is already online and only this part is
// being changed).
//
// Idle text, facing and the calendar link live on the Customise tab now, so
// they are only touched when the post actually carries them - otherwise a
// location-only save would silently blank settings made elsewhere.
void CaptivePortal::applyLocationArgs()
{
    _locationChanged = true;

    bool manual = server.arg("manualLocation") == "1";

    String typedCity = manual ? server.arg("city") : "";
    String typedCountry = manual ? server.arg("country") : "";
    String typedTz = manual ? server.arg("timezone") : "";
    String typedLat = manual ? server.arg("latitude") : "";
    String typedLon = manual ? server.arg("longitude") : "";
    String typedBounce = manual ? server.arg("bounceText") : "";
    String typedFacing = manual ? server.arg("facing") : "";
    String gpsLat = server.arg("gpsLat");
    String gpsLon = server.arg("gpsLon");

    // Bounce text: letters only, uppercased. The form now requires
    // EXACTLY 3 letters; enforce that server-side too - anything that
    // isn't precisely 3 alpha chars falls back to "" here, which
    // PlaneTrackerScreen treats as "use the default (KML)".
    if (server.hasArg("bounceText")) {
    String sanitizedBounce = "";
    for (int i = 0; i < (int)typedBounce.length() && sanitizedBounce.length() < 3; i++) {
        char c = typedBounce[i];
        if (isAlpha(c)) sanitizedBounce += (char)toupper(c);
    }
        if (sanitizedBounce.length() != 3) sanitizedBounce = "";
        _bounceText = sanitizedBounce;
    }

    // Facing direction: only accept one of the 8 known values, anything
    // else (including blank) is treated as "not set".
    if (server.hasArg("facing")) {
    static const char* validFacings[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    _facingDirection = "";
    for (const char* f : validFacings) {
        if (typedFacing.equalsIgnoreCase(f)) {
            _facingDirection = f;
            break;
        }
    }
    }

    // Calendar iCal URL - accept anything that looks like an http(s)
    // link; blank clears it. Persisted straight to NVS.
    if (server.hasArg("calUrl")) {
    String calUrl = server.arg("calUrl");
    calUrl.trim();
    if (calUrl.length() == 0 || calUrl.startsWith("http")) {
        setCalendarUrl(calUrl);
    }
    }

    bool haveTypedCoords = typedLat.length() > 0 && typedLon.length() > 0 &&
                            (typedLat.toFloat() != 0.0f || typedLon.toFloat() != 0.0f);
    bool haveGpsCoords = gpsLat.length() > 0 && gpsLon.length() > 0 &&
                         (gpsLat.toFloat() != 0.0f || gpsLon.toFloat() != 0.0f);

    if (haveTypedCoords) {
        _latitude = typedLat.toFloat();
        _longitude = typedLon.toFloat();
        _hasGpsCoords = true; // reuse: "trust these, don't overwrite with IP lookup"
    } else if (haveGpsCoords) {
        _latitude = gpsLat.toFloat();
        _longitude = gpsLon.toFloat();
        _hasGpsCoords = true;
    } else {
        _latitude = 0;
        _longitude = 0;
        _hasGpsCoords = false;
    }

    _city = typedCity;       // may be empty - that's fine, auto-detect fills it
    _country = typedCountry;
    _timezone = typedTz;

    bool haveEverything = _hasGpsCoords && _city.length() > 0 &&
                           _country.length() > 0 && _timezone.length() > 0;
    _locationAutoDetectNeeded = !haveEverything;
}

void CaptivePortal::setupRoutes()
{
    // WebServer discards request headers it was not told to keep, and the
    // session cookie is how the PIN gate remembers who has been let in.
    static const char *kept[] = { "Cookie" };
    server.collectHeaders(kept, 1);

    // The config page is the whole point of the STA-mode server, so land
    // there directly - the Wi-Fi form below only makes sense during first
    // run, when there is no connection yet.
    server.on("/config", HTTP_GET, [this]() {
        if (guardWithPin()) return;
        server.sendHeader("Cache-Control", "no-store");
        server.send(200, "text/html", configPageHtml(false));
    });
    server.on("/config", HTTP_POST, [this]() {
        if (guardWithPin()) return;
        applyConfigPost();
        server.sendHeader("Cache-Control", "no-store");
        server.send(200, "text/html", configPageHtml(true));
    });

    // One page, three tabs, both modes. In AP mode it opens on Wi-Fi
    // because that is what first-run setup needs; once the device is on the
    // network it opens on Customise, since setup is already done.
    // Location on its own, for a device that is already online. /connect
    // would rewrite the Wi-Fi credentials and reconnect, which is exactly
    // what someone editing their city does not want.
    // The page is a three-step wizard with one Save at the end, so one
    // endpoint takes the lot: Wi-Fi, location and every customisation
    // option, in that order. Wi-Fi goes last because reconnecting can drop
    // the very socket this response has to go out on.
    server.on("/save", HTTP_POST, [this]() {
        if (guardWithPin()) return;

        applyLocationArgs();
        applyConfigPost();      // also calls saveLocationEEPROM()

        String ssid = server.arg("ssid");
        String pass = server.arg("password");

        // An empty password means "keep the one you have" - the field is
        // deliberately not pre-filled with the real password, so blank has
        // to mean unchanged rather than "set it to nothing".
        String curSsid, curPass;
        bool haveSaved = loadWiFiEEPROM(curSsid, curPass);
        if (pass.length() == 0 && haveSaved && ssid == curSsid) pass = curPass;

        bool wifiChanged = ssid.length() > 0 &&
                           (!haveSaved || ssid != curSsid || pass != curPass);

        server.send(200, "text/html", donePageHtml(wifiChanged));
        _closeRequested = true;

        if (wifiChanged) {
            saveWiFiEEPROM(ssid, pass);
            Serial.printf("[CaptivePortal] Wi-Fi changed to %s - reconnecting\n", ssid.c_str());
            WiFi.mode(WIFI_STA);
            WiFi.begin(ssid.c_str(), pass.c_str());
        }
    });

    server.on("/", [this]() {
        if (guardWithPin()) return;
        server.sendHeader("Cache-Control", "no-store");
        server.send(200, "text/html", configPageHtml(false));
    });

    server.on("/refresh", HTTP_GET, [this]()
              {
    // Only the captive-portal case needs the mode dance. Doing it while
    // the device is on the user's network would drop that connection - and
    // the page the request came in on with it.
    if (_apMode) {
        WiFi.mode(WIFI_STA); // switch to STA for scanning
        delay(200);
        scanNetworks();
        WiFi.mode(WIFI_AP); // switch back to AP
        WiFi.softAP(_ssid, _password, 1);
    } else {
        // Already connected, so a blocking scan here is fine: this is an
        // explicit "Refresh networks" and the user is waiting on it.
        scanNetworks();
    }
    
    server.sendHeader("Location", "/");
    server.send(303); });
    server.on("/connect", HTTP_POST, [this]()
              {
        String ssid = server.arg("ssid");
        String password = server.arg("password");

        // Every location field is independently optional now: whatever the
        // user actually filled in (manually or via GPS) is trusted as-is;
        // anything left blank/zero is auto-detected later via IP-geolocation
        // once the device is online. This fixes the case where someone
        // ticks "manual" but leaves lat/lon at 0 - that's treated as "not
        // provided", not as a literal coordinate in the Gulf of Guinea.
        applyLocationArgs();

        saveLocationEEPROM();
        saveWiFiEEPROM(ssid, password);

        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), password.c_str());

        int attempt = 0;
        while (WiFi.status() != WL_CONNECTED && attempt < 20) {
            delay(500);
            attempt++;
        }

        String body;
        if (WiFi.status() == WL_CONNECTED) {
            body = "<h1><span class=b>[</span> CONNECTED <span class=b>]</span></h1>"
                   "<p>The device is online at <b>" + WiFi.localIP().toString() +
                   "</b>.<br>You can close this page.</p>";
            WiFi.softAPdisconnect(true);
        } else {
            body = "<h1><span class=b>[</span> NOT CONNECTED <span class=b>]</span></h1>"
                   "<p>Could not join that network &mdash; wrong password?<br>"
                   "<a href=\"/\">Back to setup</a></p>";
        }

        String response =
            "<!DOCTYPE html><html><head><meta name=viewport content=\"width=device-width,initial-scale=1\">"
            "<meta charset=utf-8><title>FlugVel Setup</title><style>"
            "body{margin:0;padding:40px 16px;background:#c8d0b8;color:#23271d;"
            "font-family:ui-monospace,Menlo,Consolas,monospace;text-align:center;line-height:1.6;}"
            "h1{font-size:17px;letter-spacing:2px;}.b{color:#5a6048;}"
            "a{color:#c05a1e;}b{color:#c05a1e;}</style></head><body>" + body +
            "</body></html>";

        server.send(200, "text/html", response); });

    // Every phone OS decides "is this network captive?" by fetching a URL
    // it knows the answer to and checking whether it got that answer back.
    // Android wants an empty 204, Apple and Windows want a specific short
    // body. Anything else - including the 302 below - means "something
    // intercepted me", which is exactly the conclusion we want, and is
    // what makes the sign-in sheet appear on its own instead of the user
    // having to open a browser and guess an address.
    //
    // These are listed explicitly rather than left to onNotFound only so
    // the intent is greppable; the catch-all would handle them too.
    static const char *kProbes[] = {
        "/generate_204", "/gen_204",            // Android
        "/hotspot-detect.html", "/library/test/success.html",  // Apple
        "/ncsi.txt", "/connecttest.txt",        // Windows
        "/canonical.html", "/success.txt"       // Firefox / misc
    };
    for (const char *probe : kProbes)
        server.on(probe, HTTP_GET, [this]() { redirectToPortal(); });

    server.onNotFound([this]() { redirectToPortal(); });
}

// Where an unrecognised request gets sent.
//
// In AP mode this has to be an ABSOLUTE url, not "/". A relative redirect
// would keep whatever host the phone originally asked for in the address
// bar - captive detection probes ask for names like connectivitycheck.
// gstatic.com - and the user would be looking at a page that appears to
// come from Google. Sending them to the real name instead puts
// setup.flugvel.com on screen, which is both honest and memorable.
//
// In STA mode the device is a guest on someone's LAN and has no claim to
// any name there, so a relative redirect to its own IP is correct. Sending
// setup.flugvel.com would resolve to 192.168.4.1 - an address that is not
// the device and may well belong to something else on that network.
void CaptivePortal::redirectToPortal()
{
    if (_apMode) {
        // Already on the right host? Then this was a genuine 404, and
        // redirecting to the same place would loop.
        if (server.hostHeader() == kSetupHost) {
            server.sendHeader("Location", "/", true);
        } else {
            server.sendHeader("Location", String("http://") + kSetupHost + "/", true);
        }
    } else {
        server.sendHeader("Location", "/", true);
    }
    server.sendHeader("Cache-Control", "no-store");
    server.send(302, "text/plain", "Redirecting...");
}


bool CaptivePortal::consumeLocationChanged() {
    if (!_locationChanged) return false;
    _locationChanged = false;
    return true;
}

bool CaptivePortal::consumeConfigChanged() {
    if (!_configChanged) return false;
    _configChanged = false;
    return true;
}

// Parses "id:enabled,id:enabled,..." back into the config. Ids this build
// does not recognise, and the pinned Settings id, are rejected rather than
// stored - the browser should never send them, but a hand-crafted POST
// could, and an unknown id in the cycle would be a screen that can never be
// shown or removed.
void CaptivePortal::applyConfigPost()
{
    Config &cfg = ConfigStore::get();

    // The screen order arrives in a hidden field that the page's JS fills in
    // on submit. Everything else on the form is a plain input the browser
    // submits by itself, so the two are parsed independently now. They used
    // to share an early return, which meant any hiccup building that one
    // field silently discarded every option on the page along with it -
    // brightness included.
    String v = server.arg("screens");
    ScreenSlot parsed[CONFIG_MAX_SCREENS];
    uint8_t count = 0;
    int enabledCount = 0;

    int start = 0;
    while (start <= (int)v.length() && count < CONFIG_MAX_SCREENS) {
        int comma  = v.indexOf(',', start);
        String tok = (comma < 0) ? v.substring(start) : v.substring(start, comma);
        int colon  = tok.indexOf(':');
        if (colon > 0) {
            int id = tok.substring(0, colon).toInt();
            int on = tok.substring(colon + 1).toInt();
            const ScreenDef* d = ScreenRegistry::byId((ScreenId)id);
            if (d && !ScreenRegistry::isPinned(d->id)) {
                parsed[count].id      = (uint8_t)id;
                parsed[count].enabled = on ? 1 : 0;
                if (parsed[count].enabled) enabledCount++;
                count++;
            }
        }
        if (comma < 0) break;
        start = comma + 1;
    }

    if (count > 0) {
        // Never save a set with nothing shown. ScreenManager has a fallback
        // for it, but a device that silently ignores what you saved is worse
        // than one that quietly keeps the first screen on.
        if (enabledCount == 0) { parsed[0].enabled = 1; enabledCount = 1; }

        memcpy(cfg.screens, parsed, sizeof(ScreenSlot) * count);
        cfg.screenCount = count;
    }

    // Options. Every one is clamped rather than trusted - these arrive from
    // a form that anyone on the LAN can hand-edit, and a bad value here
    // ends up indexing an array or driving a draw loop.
    auto num = [&](const char* name, int lo, int hi, int fallback) -> uint8_t {
        if (!server.hasArg(name)) return (uint8_t)fallback;
        int v = server.arg(name).toInt();
        if (v < lo) v = lo;
        if (v > hi) v = hi;
        return (uint8_t)v;
    };
    auto flag = [&](const char* name, uint8_t fallback) -> uint8_t {
        if (!server.hasArg(name)) return fallback;
        return server.arg(name).toInt() ? 1 : 0;
    };

    cfg.brightness       = num("bright", 10, 100, cfg.brightness);
    cfg.legend           = flag("legend",   cfg.legend);
    cfg.showDate         = flag("showdate", cfg.showDate);
    cfg.leftHanded       = flag("lefth",    cfg.leftHanded);
    cfg.planeFlyover     = flag("pfly",     cfg.planeFlyover);
    cfg.planeQuote       = flag("pq",       cfg.planeQuote);
    cfg.weatherDays      = num("wdays",  3,  7, cfg.weatherDays);
    cfg.focusMinutes     = num("ffoc",   5, 60, cfg.focusMinutes);
    cfg.breakMinutes     = num("fbrk",   1, 30, cfg.breakMinutes);
    cfg.calLookaheadDays = num("clook", 14, 90, cfg.calLookaheadDays);
    cfg.calAllDay        = flag("call",     cfg.calAllDay);
    cfg.dateTimeFormat   = num("clock", 0, 3, cfg.dateTimeFormat);
    cfg.imperial         = num("units", 0, 1, cfg.imperial);
    cfg.pagerStyle       = num("pager", 0, 2, cfg.pagerStyle);
    cfg.knobReversed     = num("knob",  0, 1, cfg.knobReversed);
    cfg.startupResume    = num("start", 0, 1, cfg.startupResume);
    cfg.weatherRefresh10 = num("wref",  0, 3, 0);
    { static const int kRef10[] = { 3, 6, 18, 36 };
      cfg.weatherRefresh10 = kRef10[cfg.weatherRefresh10]; }
    cfg.weatherIcons     = flag("wicon", cfg.weatherIcons);
    cfg.weatherPrefetch  = flag("wpre",  cfg.weatherPrefetch);
    cfg.focusLock        = flag("flock", cfg.focusLock);
    cfg.focusAutoBreak   = flag("fauto", cfg.focusAutoBreak);
    cfg.calMerge         = flag("cmrg",  cfg.calMerge);
    cfg.calHidePast      = flag("cpast", cfg.calHidePast);
    cfg.calMaxEvents     = num("cmax", 10, 60, cfg.calMaxEvents);

    { static const int kAlt100[] = { 0, 10, 30, 60 };
      cfg.planeMinAlt100m = kAlt100[num("palt", 0, 3, 0)]; }

    // One bit per game. Rebuilt from the form each save, so a game with no
    // field present (a build with fewer games) simply drops out.
    {
        uint8_t mask = 0;
        for (int i = 0; i < GamesScreen::entryCount() && i < 8; i++) {
            if (server.arg((String("g") + i).c_str()).toInt()) mask |= (1 << i);
        }
        if (mask == 0) mask = 1;   // never leave the list empty
        cfg.gamesMask = mask;
    }

    // Plane fields that live in the portal's own EEPROM block rather than
    // the config blob, kept in step here so one Save covers everything.
    if (server.hasArg("pidle")) {
        String t = server.arg("pidle");
        t.trim(); t.toUpperCase();
        if (t.length() == 3) _bounceText = t;
    }
    if (server.hasArg("pface")) {
        static const char* const kFace[] = { "", "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
        int fi = server.arg("pface").toInt();
        if (fi >= 0 && fi < 9) _facingDirection = kFace[fi];
    }
    { static const int kIvSec[] = { 30, 60, 120, 300, 600, 900, 1800 };
      int ii = num("piv", 0, 6, 0);
      _flightCheckIntervalSec = kIvSec[ii];
      cfg.flightIntervalSec   = (uint16_t)kIvSec[ii]; }

    // saveLocationEEPROM() below copies this class's own members INTO the
    // config - it predates the web page, when those members were the only
    // source. Anything the page just wrote that also has a member here has
    // to be pushed back the other way first, or the save quietly reverts it.
    // This was silently undoing the clock format, the units, and (via the
    // home-screen rotation) the entire screen order on every save.
    _dateTimeFormat  = cfg.dateTimeFormat;
    _imperial        = (cfg.imperial != 0);
    _themeId         = cfg.themeId;
    _homeScreenIndex = ConfigStore::homeSelectable();  // from the NEW order

    saveLocationEEPROM();

    // Calendar feeds. Rebuilt from scratch each save so that clearing a URL
    // genuinely removes that feed and the remaining ones close up, rather
    // than leaving a hole that later indexes have to step over.
    {
        CalendarFeeds::clear();
        for (int i = 0; i < CAL_MAX_FEEDS; i++) {
            String url = server.arg((String("cu") + i).c_str());
            url.trim();
            if (url.length() < 8) continue;
            String label = server.arg((String("cn") + i).c_str());
            label.trim();
            CalendarFeeds::add(label, url);
        }
        CalendarFeeds::save();
    }

    // Notes source - a single Notion page, unlike the calendar's several
    // feeds, so this is just an overwrite rather than a rebuild. A cleared
    // token or page genuinely disables the screen rather than leaving a
    // stale value that quietly keeps fetching.
    {
        String tok  = server.arg("ntok");  tok.trim();
        String page = server.arg("npage"); page.trim();
        int notesMax = num("nmax", 5, 15, NotesSource::maxItems());
        bool notesChecked = flag("nchk", NotesSource::showChecked() ? 1 : 0) != 0;
        bool notesGroup   = flag("ngrp", NotesSource::groupByDay() ? 1 : 0) != 0;
        NotesSource::set(tok, page, notesMax, notesChecked, notesGroup);
        NotesSource::save();
    }

    ConfigStore::save();
    _configChanged = true;

    Serial.printf("[CaptivePortal] Saved %u screens, %d shown\n", count,
                  enabledCount == 0 ? 1 : enabledCount);
    Serial.printf("[CaptivePortal] Options: brightness %u%% legend %u date %u left %u\n",
                  cfg.brightness, cfg.legend, cfg.showDate, cfg.leftHanded);
}

void CaptivePortal::handle()
{
    if (!_running) return;
    // The wildcard DNS answer is what makes a captive portal pop up. On the
    // user's own network it would answer for every lookup any device on the
    // LAN made, so it runs in AP mode only.
    if (_apMode) dnsServer.processNextRequest();
    server.handleClient();
}

IPAddress CaptivePortal::getIP()
{
    return WiFi.softAPIP();
}

float CaptivePortal::getFacingBearingDeg()
{
    if (_facingDirection == "N")  return 0.0f;
    if (_facingDirection == "NE") return 45.0f;
    if (_facingDirection == "E")  return 90.0f;
    if (_facingDirection == "SE") return 135.0f;
    if (_facingDirection == "S")  return 180.0f;
    if (_facingDirection == "SW") return 225.0f;
    if (_facingDirection == "W")  return 270.0f;
    if (_facingDirection == "NW") return 315.0f;
    return -1.0f; // not set - caller should keep its own default
}

void CaptivePortal::setDetectedLocation(const String &city, const String &country,
                                         float lat, float lon, const String &timezone,
                                         bool overwriteCoords)
{
    // Only fill in the blanks - don't clobber anything the user actually
    // typed themselves (e.g. they may have typed a city but left the
    // country/timezone empty).
    if (_city.length() == 0) _city = city;
    if (_country.length() == 0) _country = country;
    if (_timezone.length() == 0) _timezone = timezone;

    if (overwriteCoords) {
        _latitude = lat;
        _longitude = lon;
    }
    // else: keep the existing (user-typed or GPS) coordinates - they're
    // more accurate than an IP-based lookup.

    _locationAutoDetectNeeded = false;

    saveLocationEEPROM();
}

// ---- EEPROM ----
void CaptivePortal::saveLocationEEPROM()
{
    EEPROM.begin(512);

    // city
    int len = _city.length();
    EEPROM.write(0, len);
    for (int i = 0; i < len; i++)
        EEPROM.write(1 + i, _city[i]);

    // country
    len = _country.length();
    EEPROM.write(50, len);
    for (int i = 0; i < len; i++)
        EEPROM.write(51 + i, _country[i]);

    // latitude
    EEPROM.put(100, _latitude);

    // longitude
    EEPROM.put(104, _longitude);

    // timezone
    len = _timezone.length();
    EEPROM.write(108, len);
    for (int i = 0; i < len; i++)
        EEPROM.write(109 + i, _timezone[i]);

    // bounce text (max 3 chars, letters only - see /connect's sanitizing)
    len = _bounceText.length();
    EEPROM.write(150, len);
    for (int i = 0; i < len; i++)
        EEPROM.write(151 + i, _bounceText[i]);

    // facing direction (max 2 chars, e.g. "NE")
    len = _facingDirection.length();
    EEPROM.write(160, len);
    for (int i = 0; i < len; i++)
        EEPROM.write(161 + i, _facingDirection[i]);

    EEPROM.commit();

    // Everything that is a user *preference* rather than a location now
    // lives in NVS (see config/Config.h for why). Addresses 170-185 in the
    // block above used to hold these and are deliberately left untouched:
    // a device that rolls back to older firmware still finds its old
    // settings there, and ConfigStore only reads them once, on the first
    // boot that finds no NVS config.
    Config &cfg = ConfigStore::get();
    cfg.flightIntervalSec = (uint16_t)_flightCheckIntervalSec;
    cfg.flappyBest        = _flappyBestScore;
    cfg.paddleBest        = _paddleCatchBestScore;
    cfg.themeId           = _themeId;
    cfg.dateTimeFormat    = _dateTimeFormat;
    cfg.imperial          = _imperial ? 1 : 0;
    ConfigStore::setHomeSelectable(_homeScreenIndex);
    ConfigStore::save();
}

void CaptivePortal::loadLocationEEPROM()
{
    EEPROM.begin(512);

    // city
    int len = EEPROM.read(0);
    _city = "";
    for (int i = 0; i < len; i++)
        _city += char(EEPROM.read(1 + i));

    // country
    len = EEPROM.read(50);
    _country = "";
    for (int i = 0; i < len; i++)
        _country += char(EEPROM.read(51 + i));

    // latitude
    EEPROM.get(100, _latitude);

    // longitude
    EEPROM.get(104, _longitude);

    // timezone
    len = EEPROM.read(108);
    _timezone = "";
    for (int i = 0; i < len; i++)
        _timezone += char(EEPROM.read(109 + i));

    // bounce text
    len = EEPROM.read(150);
    _bounceText = "";
    for (int i = 0; i < len && i < 3; i++)
        _bounceText += char(EEPROM.read(151 + i));

    // facing direction
    len = EEPROM.read(160);
    _facingDirection = "";
    for (int i = 0; i < len && i < 2; i++)
        _facingDirection += char(EEPROM.read(161 + i));

    // The preference fields that used to live at 170-185 come from NVS
    // now. No range-checking here any more: ConfigStore validated these
    // once during its migration and every value written since then went
    // through this class's setters, so there is no erased-0xFF case left
    // to defend against.
    Config &cfg = ConfigStore::get();
    _flightCheckIntervalSec = cfg.flightIntervalSec;
    _flappyBestScore        = cfg.flappyBest;
    _paddleCatchBestScore   = cfg.paddleBest;
    _themeId                = cfg.themeId;
    _dateTimeFormat         = cfg.dateTimeFormat;
    _imperial               = (cfg.imperial != 0);
    _homeScreenIndex        = ConfigStore::homeSelectable();
}

// ---- Wi-Fi EEPROM ----
void CaptivePortal::saveWiFiEEPROM(String ssid, String password)
{
    EEPROM.begin(512);

    // SSID
    int ssidLen = ssid.length();
    EEPROM.write(200, ssidLen);
    for (int i = 0; i < ssidLen; i++)
        EEPROM.write(201 + i, ssid[i]);

    // Password
    int passLen = password.length();
    EEPROM.write(250, passLen);
    for (int i = 0; i < passLen; i++)
        EEPROM.write(251 + i, password[i]);

    EEPROM.commit();
}

bool CaptivePortal::loadWiFiEEPROM(String &ssid, String &password)
{
    EEPROM.begin(512);

    // SSID
    int ssidLen = EEPROM.read(200);
    if (ssidLen == 0 || ssidLen > 32)
        return false;
    ssid = "";
    for (int i = 0; i < ssidLen; i++)
        ssid += char(EEPROM.read(201 + i));

    // Password
    int passLen = EEPROM.read(250);
    password = "";
    for (int i = 0; i < passLen; i++)
        password += char(EEPROM.read(251 + i));

    return true;
}