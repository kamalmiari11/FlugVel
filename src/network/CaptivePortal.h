#pragma once

#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <Arduino.h>

class CaptivePortal
{
public:
    // The name the setup AP answers to. In AP mode the device runs its own
    // DNS server that resolves every query to itself, so this name works
    // with no internet at all. But a phone using DNS-over-HTTPS or Android
    // "private DNS" ignores the resolver the AP handed out and asks a
    // public one instead - so setup.flugvel.com is also a real public A
    // record pointing at 192.168.4.1, and both paths land in the same
    // place. A private address published in a public zone is deliberate:
    // it is only ever reachable from inside this AP.
    static constexpr const char *kSetupHost = "setup.flugvel.com";

    // This device's name on the local network: "flugvel-a4c1", from the
    // last two bytes of the Wi-Fi MAC. Unique per unit without any
    // provisioning step or stored state, which matters once more than one
    // of these is on the same network - mDNS does have a protocol-level
    // way to resolve two devices claiming one name, but the ESP32
    // implementation handles it poorly, so it is easier never to collide.
    // Safe to call before Wi-Fi is connected; the MAC is factory-burned.
    static String deviceName();

    // deviceName() + ".local" - what a browser can actually be pointed at.
    // ".local" is reserved by RFC 6762 and resolved by multicast on the
    // LAN, not by any nameserver, so this is unrelated to (and independent
    // of) kSetupHost above.
    static String mdnsHost();

    CaptivePortal(const char *ssid, const char *password);

    // ---- lifecycle ----
    // The portal is reachable two ways, and the difference is only in how
    // the phone gets to it. beginAP() is first-run setup: the device has no
    // Wi-Fi yet, so it becomes an access point and hijacks DNS so any
    // request lands on the form. beginSTA() is everything after that: the
    // device is already on the user's network and their phone almost
    // certainly is too, so it just listens on the existing connection - no
    // AP to join, no internet to lose. Both serve the same routes.
    void beginAP();
    void beginSTA();

    // Shut the web server down and release the socket. Always call this
    // when leaving the config page - and before an OTA, since WebServer
    // plus TLS buffers plus HTTPUpdate at once is the RAM-tight moment in
    // this firmware.
    void stop();

    bool isRunning() const { return _running; }
    bool isApMode()  const { return _apMode; }

    // Four-digit code shown on the device's screen and required by the
    // page when reachable over the LAN. On the setup AP there is nothing to
    // protect (no settings exist yet) and knowing the AP password is
    // already the gate, so no PIN is asked for there. Regenerated on every
    // beginSTA(), so a code someone glimpsed once stops working.
    uint16_t configPin() const { return _configPin; }

    // True once (and once only) after the config page saved a new screen
    // order. The web server runs on the main task, but a route handler is
    // still a bad place to re-init screens - so the handler only raises
    // this flag and loop() does the work at a point where nothing else is
    // mid-draw.
    bool consumeConfigChanged();

    // Raised when the page's final Save succeeds. The device closes the
    // portal and leaves the "Customise on phone" screen on the next tick -
    // finishing on the phone should put the device back where it was, not
    // leave a server listening and a QR on the panel.
    bool closeRequested() const { return _closeRequested; }

    // True once after the location was edited. Saving it is only half the
    // job: the flight search is centred on those coordinates and the
    // forecast is fetched for them, so both have to be told.
    bool consumeLocationChanged();

    void handle();
    IPAddress getIP();

    // Wipe every persisted setting - EEPROM (Wi-Fi credentials, location,
    // interval, home screen, theme, game best scores) and the NVS store -
    // then reboot into first-run setup. Does not return.
    void factoryReset();

    // Getters for WiFi settings
    String getCity() { return _city; }
    String getCountry() { return _country; }
    float getLatitude() { return _latitude; }
    float getLongitude() { return _longitude; }
    String getTimezone() { return _timezone; }

    // Optional plane-tracker customization from the manual location form.
    // Both are entirely optional - empty/unset means "use the built-in
    // default" (the "KML" bounce text and DISPLAY_BEARING=270 in
    // PlaneTrackerScreen).
    String getBounceText() { return _bounceText; } // up to 3 letters, already sanitized/uppercased
    String getFacingDirection() { return _facingDirection; } // "", "N", "NE", "E", "SE", "S", "SW", "W", or "NW"

    // How often the background task re-fetches the nearest flight, in
    // seconds. Adjustable from the Settings screen's "API" submenu.
    // In-memory only - call saveLocationEEPROM() afterwards to persist,
    // same pattern the rest of this class already uses.
    int getFlightCheckIntervalSec() { return _flightCheckIntervalSec; }
    void setFlightCheckIntervalSec(int seconds) { _flightCheckIntervalSec = seconds; }

    // Which top-level screen to boot into, as an index matching the order
    // screens are added in main.cpp: 0=Plane, 1=Weather, 2=Games, 3=Focus.
    // (Settings itself isn't selectable as a home screen.) In-memory only -
    // call saveLocationEEPROM() afterwards to persist, same pattern as above.
    int getHomeScreenIndex() { return _homeScreenIndex; }
    void setHomeScreenIndex(int index) { _homeScreenIndex = index; }

    // Best score reached in Flappy Plane. In-memory only - call
    // saveLocationEEPROM() afterwards to persist, same pattern as above.
    int getFlappyBestScore() { return _flappyBestScore; }
    void setFlappyBestScore(int score) { _flappyBestScore = score; }

    // Best score reached in Paddle Catch. Same in-memory + call
    // saveLocationEEPROM() pattern as the other settings above.
    int getPaddleCatchBestScore() { return _paddleCatchBestScore; }
    void setPaddleCatchBestScore(int score) { _paddleCatchBestScore = score; }

    // Which UI theme is active, as a small numeric id matching
    // ThemeRegistry's table (see src/ui/Theme.h/.cpp) - 0 is always the
    // default (Mono Dot-Matrix). In-memory only - call saveLocationEEPROM()
    // afterwards to persist, same pattern as above. Not currently exposed
    // in the Settings UI - this just makes the storage/read path exist so
    // it can be wired up later without any further plumbing.
    uint8_t getThemeId() { return _themeId; }
    void setThemeId(uint8_t id) { _themeId = id; }

    // Date/clock format (index 0-3, see src/ui/Units.h) and measurement
    // system (false = Metric, true = Imperial - drives temperature, speed
    // and altitude). Same in-memory + call saveLocationEEPROM() pattern as
    // everything above.
    uint8_t getDateTimeFormat() { return _dateTimeFormat; }
    void setDateTimeFormat(uint8_t f) { _dateTimeFormat = f; }
    bool getImperial() { return _imperial; }
    void setImperial(bool v) { _imperial = v; }

    // Read-only iCal (.ics) feed URL for the Calendar screen (Google /
    // iCloud / Outlook "secret address in iCal format"). Optional - empty
    // means the Calendar screen just shows a "not set up" message. Stored
    // in NVS (it's far too long for the EEPROM block); loadCalendarUrl()
    // pulls it in at boot.
    String getCalendarUrl() { return _calendarUrl; }
    void   setCalendarUrl(const String &url); // persists to NVS immediately
    void   loadCalendarUrl();                 // call once at startup

    // Converts getFacingDirection() to a compass bearing in degrees.
    // Returns -1 if no direction was set (i.e. use the caller's default).
    float getFacingBearingDeg();

    // True if the user did NOT fill in the manual location form and no
    // browser-GPS coordinates were captured either - main.cpp should run
    // IP-based auto-detection once WiFi is actually connected.
    bool needsLocationAutoDetect() { return _locationAutoDetectNeeded; }

    // True if we already have accurate GPS coordinates (from the browser's
    // "Use my location" button) that should NOT be overwritten by a
    // lower-accuracy IP-geolocation lookup.
    bool hasGpsCoords() { return _hasGpsCoords; }

    // Called by main.cpp after a successful IP-geolocation lookup.
    // If overwriteCoords is false, the existing (GPS) lat/lon are kept and
    // only city/country/timezone are updated.
    void setDetectedLocation(const String &city, const String &country,
                              float lat, float lon, const String &timezone,
                              bool overwriteCoords);

    // EEPROM functions
    void saveLocationEEPROM();
    void loadLocationEEPROM();
    void saveWiFiEEPROM(String ssid, String password);
    bool loadWiFiEEPROM(String &ssid, String &password);

private:
    const char *_ssid;
    const char *_password;

    String _city;
    String _country;
    float _latitude;
    float _longitude;
    String _timezone;
    String _bounceText;      // "" = use default ("KML")
    String _facingDirection; // "" = use default (DISPLAY_BEARING=270)
    String _calendarUrl;     // iCal feed URL, from NVS (see loadCalendarUrl)

    // Default matches the original hardcoded FLIGHT_CHECK_INTERVAL (30000ms).
    int _flightCheckIntervalSec = 30;

    // Default matches the original hardcoded boot behavior (always started
    // on the Plane screen, index 0).
    int _homeScreenIndex = 0;

    int _flappyBestScore = 0;
    int _paddleCatchBestScore = 0;

    // Default matches ThemeRegistry's default (index 0, Mono Dot-Matrix).
    uint8_t _themeId = 0;
    uint8_t _dateTimeFormat = 0;   // index into Units' format table
    bool    _imperial = false;     // false = Metric, true = Imperial

    bool _locationAutoDetectNeeded = false;
    bool _hasGpsCoords = false;

    // Web server
    WebServer server{80};
    DNSServer dnsServer;
    String scannedNetworksHTML;

    bool     _running  = false;
    bool     _apMode   = false;
    bool     _mdnsUp   = false;  // responder running - STA mode only, and
                                 // only while the web server is up
    bool     _routesUp = false;  // setupRoutes() registers handlers on the
                                 // server object itself, so it must run once
                                 // and only once for the life of the object
    uint16_t _configPin = 0;
    bool     _closeRequested   = false;
    bool     _locationChanged  = false;

    // A single _unlocked flag meant the first person to type the PIN
    // unlocked the page for everyone else on the network - the code stopped
    // protecting anything the moment it was used once. Access is now tied
    // to a cookie carrying this token, issued only in exchange for the PIN,
    // so a second browser still has to enter it.
    String   _sessionToken;
    bool     _configChanged = false;

    // Helper methods
    void scanNetworks();          // blocking - AP setup only
    void startScanAsync();        // kicks a scan off and returns immediately
    void collectScanResults();    // folds a finished async scan into the list
    void setupRoutes();

    // 302s an unrecognised request at the config page - absolute to
    // kSetupHost on the setup AP, relative when on someone else's LAN.
    // Shared by onNotFound and the captive-detection probe routes.
    void redirectToPortal();

    // Serves the PIN prompt instead of the real page. Returns true if it
    // did, i.e. the caller should stop and not serve anything else.
    bool guardWithPin();

    // The screen order / visibility page, built from ScreenRegistry and the
    // saved config so it always reflects what the device actually has -
    // including screens added by a firmware update that the stored config
    // has never heard of.
    String configPageHtml(bool justSaved);
    String donePageHtml(bool reconnecting);
    void   applyConfigPost();
    void   applyLocationArgs();
};