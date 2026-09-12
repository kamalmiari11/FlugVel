#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <EEPROM.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "screens/BootScreen.h"
#include "MyColors.h"
#include "screens/WiFiSetupScreen.h"
#include "network/CaptivePortal.h"
#include "screens/Header.h"
#include "screens/ActionLegend.h"
#include "ui/Theme.h"
#include "ui/Backlight.h"
#include "ui/Units.h"

#include "screens/ScreenManager.h"
#include "screens/ScreenRegistry.h"
#include "config/Config.h"
#include "config/CalendarFeeds.h"
#include "config/NotesSource.h"
#include "screens/PlaneTrackerScreen.h"
#include "screens/DashboardScreen.h"
#include "screens/SettingsScreen.h"
#include "screens/GamesScreen.h"
#include "screens/FocusTimerScreen.h"
#include "screens/CalendarScreen.h"

#include "api/flight_api.h"
#include "api/ip_geolocation.h"
#include "api/geocoding.h"
#include "api/quote_api.h"
#include "api/update_check.h"
#include "api/checkin_api.h"

#include "screens/UpdatePromptScreen.h"
#include "version.h"

// ============ HARDWARE DEFINITIONS ============
#define RESET_HOLD_TIME 15000
// Default flight-fetch interval, in ms - now adjustable at runtime from the
// Settings screen's "API" submenu (see CaptivePortal::getFlightCheckIntervalSec()).
// This define isn't read anywhere any more (backgroundNetworkTask() reads
// portal.getFlightCheckIntervalSec() directly) - kept only so this comment's
// number matches the real default in Config.cpp/CaptivePortal.h instead of
// quietly going stale.
#define FLIGHT_CHECK_INTERVAL 60000  // 1 minute

// ============ DISPLAY & I/O ============
TFT_eSPI tft = TFT_eSPI();
BootScreen boot(&tft);
WiFiSetupScreen wifiScreen(&tft, KO_BUTTON);
Header header(&tft);
ActionLegend actionLegend(&tft);
UpdatePromptScreen updatePromptScreen(&tft, KO_BUTTON, ENCODER_BTN);

// ============ WiFi & PORTAL ============
CaptivePortal portal(
    wifiScreen.getSSID(),
    wifiScreen.getPassword()
);

bool wifiConnected = false;
bool portalRunning = false;
unsigned long resetStartTime = 0;
bool resetTriggered = false;

// ============ SETUP FLAGS ============
bool setupScreenShown = false;  // Track if we've already shown setup screen

// Which orientation the panel is currently in, so a config save can tell
// whether handedness actually changed and only repaint when it did.
bool orientationApplied = false;

// ============ SCREEN MANAGER ============
ScreenManager* screenManager = nullptr;
PlaneTrackerScreen* planeScreen = nullptr;
DashboardScreen* dashboardScreen = nullptr;
SettingsScreen* settingsScreen = nullptr;
GamesScreen* gamesScreen = nullptr;
FocusTimerScreen* focusTimerScreen = nullptr;
CalendarScreen* calendarScreen = nullptr;

// ============ FLIGHT TRACKING ============
Flight currentFlight;
unsigned long lastFlightCheck = 0;
bool flightCheckInProgress = false;

// ============ LOCATION DATA ============
float userLat = 52.5450;  // Default, will be overridden
float userLon = 4.6690;   // Default, will be overridden

// ============ QUOTE OF THE DAY ============
QuoteData currentQuote;
int lastQuoteFetchDay = -1; // tm_yday of the last successful fetch, -1 = never

// ============ BACKGROUND NETWORK TASK ============
// The periodic flight check (every 30s) and the daily quote fetch both hit
// HTTP APIs and can take anywhere from a few hundred ms to a few seconds.
// Doing that synchronously inside loop() (as checkFlights()/
// checkQuoteOfTheDay() used to) blocks EVERYTHING for that whole time -
// the header clock, the focus timer countdown, screen input, all of it -
// no matter which screen is currently showing. Instead, a dedicated
// FreeRTOS task (pinned to the opposite core from Arduino's loop()) does
// the actual network I/O and JSON parsing, and just drops the result into
// these mutex-protected "pending" slots. loop() only ever does a quick,
// non-blocking check-and-apply each iteration - no network calls happen
// on the main task after boot.
//
// IMPORTANT: the background task must NEVER touch tft/screens directly -
// TFT_eSPI's SPI transactions aren't safe to call concurrently from two
// tasks. All screen updates (setFlight/clearFlight/setQuote/etc.) happen
// on the main task, from applyFlightResult()/applyQuoteResult(), which
// themselves never block.
SemaphoreHandle_t dataMutex = nullptr;
TaskHandle_t bgTaskHandle = nullptr;

volatile bool pendingFlightReady = false;
bool pendingFlightFound = false;
bool pendingFlightApiFailed = false;  // true = every provider errored; keep current display
Flight pendingFlightData;

volatile bool pendingQuoteReady = false;
QuoteData pendingQuoteData;

// ============ FACTORY RESET ============
// Single implementation lives in CaptivePortal::factoryReset() (it owns the
// EEPROM layout) - both the KO+encoder 15s hold below and the Settings >
// Factory reset menu item call the same code. Does not return.
void factoryReset() {
    Serial.println("Factory Reset Triggered!");
    portal.factoryReset();
}

// ============ PANEL ORIENTATION ============
// Rotation 1 is this build's normal landscape. Left-handed adds 180
// degrees: the user physically turns the board around so the encoder falls
// under their other hand, and this is what stops the display ending up
// upside down as a result. The encoder delta is negated to match - see
// ScreenManager::handleEncoderRotation().
static void applyOrientation() {
    tft.setRotation(ConfigStore::get().leftHanded ? 3 : 1);
}

// ============ RETRIEVE USER LOCATION FROM PORTAL ============
void loadUserLocation() {
    // The CaptivePortal stores location from the web form during setup
    // Try to retrieve it - uncomment the method that matches your CaptivePortal
    
    // Option 1: If CaptivePortal has getLatitude() and getLongitude()
    userLat = portal.getLatitude();
    userLon = portal.getLongitude();
    
    // Option 2: If CaptivePortal has getLat() and getLon() (uncomment if Option 1 doesn't work)
    // userLat = portal.getLat();
    // userLon = portal.getLon();
    
    Serial.printf("[Main] User Location: %.4f, %.4f\n", userLat, userLon);
    Serial.printf("[Main] Latitude:  %.4f\n", userLat);
    Serial.printf("[Main] Longitude: %.4f\n", userLon);
}

// ============ AUTO-DETECT LOCATION (IP GEOLOCATION FALLBACK) ============
// Called once WiFi is actually connected (needs real internet access, not
// just the ESP32's setup AP). Runs when the user skipped both manual entry
// and the browser's "Use my location" button during setup - or, as a safety
// net, if a previous boot never finished resolving it (e.g. power loss).
void ensureLocationResolved() {
    if (!wifiConnected) return;

    bool stillUnresolved = portal.needsLocationAutoDetect() || portal.getCity().length() == 0;
    if (!stillUnresolved) return;

    // Priority 1: the user typed a city name but no coordinates - geocode
    // that specific city instead of falling back to a coarse IP-based
    // estimate (which is only accurate to "somewhere in your ISP's service
    // area" and can easily be a few towns off).
    if (!portal.hasGpsCoords() && portal.getCity().length() > 0) {
        Serial.println("[Main] City given without coordinates, geocoding it...");

        float lat, lon;
        String resolvedCity, resolvedCountry, tz;
        if (geocodeCity(portal.getCity(), portal.getCountry(), lat, lon, resolvedCity, resolvedCountry, tz)) {
            // Keep the user's own typed city spelling; fill in whatever
            // they left blank (country/timezone) and the precise coords.
            portal.setDetectedLocation(resolvedCity, resolvedCountry, lat, lon, tz, true);
            loadUserLocation();

            if (dashboardScreen) {
                dashboardScreen->setTimezone(portal.getTimezone());
                dashboardScreen->setLocation(portal.getCity(), portal.getCountry());
                dashboardScreen->setLocationCoords(userLat, userLon);
            }
            return;
        }

        Serial.println("[Main] Geocoding failed, falling back to IP-based location");
        // fall through to IP geolocation below
    }

    Serial.println("[Main] Location not yet resolved, auto-detecting via IP...");

    String city, country, tz;
    float lat, lon;
    if (fetchIpLocation(city, country, lat, lon, tz)) {
        // Keep GPS coordinates if we already have them (more accurate than
        // IP-based lookup) - only overwrite lat/lon if we don't.
        portal.setDetectedLocation(city, country, lat, lon, tz, !portal.hasGpsCoords());
        loadUserLocation(); // refresh the userLat/userLon globals used elsewhere

        if (dashboardScreen) {
            dashboardScreen->setTimezone(portal.getTimezone());
            dashboardScreen->setLocation(portal.getCity(), portal.getCountry());
            dashboardScreen->setLocationCoords(userLat, userLon);
        }
    } else {
        Serial.println("[Main] IP auto-detect failed - will retry on next boot");
    }
}

// ============ FLIGHT TRACKING ============
// Used ONLY for the synchronous boot-time prefetch (prefetchOnConnect(),
// below) - that one call is expected/OK to block, since it happens while
// the boot loading bar is already on screen. The recurring 30s check uses
// fetchFlightBG()/applyFlightResult() instead - see the background task
// section above for why.
// True if this aircraft is below the floor the user set. Airliners cruise
// well above it; the point is to skip helicopters, training circuits and
// anything on approach that would otherwise dominate a screen meant for
// the flights actually passing overhead.
static bool flightBelowFloor(const Flight &f) {
    uint8_t floor100m = ConfigStore::get().planeMinAlt100m;
    if (floor100m == 0) return false;
    return f.altitude < (float)floor100m * 100.0f;
}

void doFlightCheck(std::function<void(const char *)> onProviderTry = nullptr) {
    Serial.println("[Main] Fetching flight data...");

    Flight newFlight;
    bool apiFailed = false;
    if (fetchNearestFlight(newFlight, userLat, userLon, &apiFailed, onProviderTry)) {
        currentFlight = newFlight;

        if (planeScreen) {
            planeScreen->setFlight(newFlight);
            planeScreen->triggerAnimation();
        }

        if (dashboardScreen) {
            dashboardScreen->recordFlight();
        }

        Serial.println("[Main] Flight detected: " + newFlight.callsign);
    } else if (apiFailed) {
        // Every provider errored/timed out/was rate-limited - this is NOT
        // "no flights", so leave whatever's on screen alone.
        Serial.println("[Main] Flight lookup failed on all providers - keeping current display");
    } else {
        if (planeScreen) {
            planeScreen->clearFlight(); // genuine "nothing overhead" - bouncing-text state
        }
        Serial.println("[Main] No flights detected");
    }
}

// Network fetch ONLY - runs on the background task. Never touches tft or
// any Screen object; just stages the result for the main task to pick up.
void fetchFlightBG() {
    Serial.println("[BG] Fetching flight data...");

    Flight newFlight;
    bool apiFailed = false;
    bool found = fetchNearestFlight(newFlight, userLat, userLon, &apiFailed);

    // Filtered here, on the network task, rather than at display time - a
    // rejected aircraft should read as "nothing overhead" and let the
    // bouncing-text state take over, not as a flight the screen refuses to
    // draw.
    if (found && flightBelowFloor(newFlight)) {
        Serial.printf("[Main] Ignoring %s at %.0f m - below the configured floor\n",
                      newFlight.callsign.c_str(), newFlight.altitude);
        found = false;
    }

    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        pendingFlightFound = found;
        pendingFlightApiFailed = apiFailed;
        if (found) pendingFlightData = newFlight;
        pendingFlightReady = true;
        xSemaphoreGive(dataMutex);
    }

    Serial.println(found ? "[BG] Flight detected: " + newFlight.callsign
                         : apiFailed ? String("[BG] All providers failed - keeping current display")
                                     : String("[BG] No flights detected"));
}

// Runs on the main task, every loop() iteration. Cheap: a non-blocking
// mutex try-lock plus, at most, a couple of fast state-setter calls into
// the screens (no drawing happens here - screens just remember the new
// data and draw it on their own next draw() call).
void applyFlightResult() {
    if (!pendingFlightReady) return;
    if (xSemaphoreTake(dataMutex, 0) != pdTRUE) return; // don't block if BG task is mid-write, just try again next loop

    bool ready = pendingFlightReady;
    bool found = pendingFlightFound;
    bool apiFailed = pendingFlightApiFailed;
    Flight flight = pendingFlightData;
    pendingFlightReady = false;

    xSemaphoreGive(dataMutex);

    if (!ready) return;

    // Whatever the outcome below, the fetch that was in flight has now
    // resolved - if PlaneTrackerScreen was showing its bouncing-plane
    // loading indicator for it (see PlaneTrackerScreen::init()/
    // setFetching()), it's done regardless of found/apiFailed. setFlight()/
    // clearFlight() below also clear it themselves, but apiFailed hits
    // neither of those, so without this line a fetch that fails on every
    // provider would leave the loading indicator stuck on screen forever.
    if (planeScreen) planeScreen->setFetching(false);

    if (found) {
        currentFlight = flight;
        if (planeScreen) {
            planeScreen->setFlight(flight);
            planeScreen->triggerAnimation();
        }
        if (dashboardScreen) {
            dashboardScreen->recordFlight();
        }
    } else if (!apiFailed) {
        // Genuine "no aircraft overhead" from a provider that answered.
        if (planeScreen) planeScreen->clearFlight();
    }
    // apiFailed: every provider errored - keep showing the last flight
    // instead of falsely flipping to the "no flights" state.
}

// ============ QUOTE OF THE DAY ============
// Same split as the flight check above: doQuoteFetch() is only for the
// synchronous boot-time prefetch; the daily background refresh uses
// fetchQuoteBG()/applyQuoteResult() so it never blocks loop().
void doQuoteFetch() {
    QuoteData q;
    if (fetchQuoteOfTheDay(q)) {
        currentQuote = q;
        if (planeScreen) planeScreen->setQuote(currentQuote.text, currentQuote.author);
    } else {
        Serial.println("[Main] Quote fetch failed - keeping whatever was showing");
    }

    // Record which day we fetched on (if the clock is synced yet) so the
    // midnight check below doesn't re-fetch again for the rest of today.
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 100)) {
        lastQuoteFetchDay = timeinfo.tm_yday;
    }
}

void fetchQuoteBG() {
    Serial.println("[BG] Fetching quote of the day...");

    QuoteData q;
    bool success = fetchQuoteOfTheDay(q);

    if (success && xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        pendingQuoteData = q;
        pendingQuoteReady = true;
        xSemaphoreGive(dataMutex);
    } else if (!success) {
        Serial.println("[BG] Quote fetch failed - keeping whatever was showing");
    }

    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 100)) {
        lastQuoteFetchDay = timeinfo.tm_yday; // only ever touched from the BG task once it's running
    }
}

void applyQuoteResult() {
    if (!pendingQuoteReady) return;
    if (xSemaphoreTake(dataMutex, 0) != pdTRUE) return;

    bool ready = pendingQuoteReady;
    QuoteData q = pendingQuoteData;
    pendingQuoteReady = false;

    xSemaphoreGive(dataMutex);

    if (!ready) return;

    currentQuote = q;
    if (planeScreen) planeScreen->setQuote(currentQuote.text, currentQuote.author);
}

// ZenQuotes' own "today" endpoint refreshes once per day at UTC midnight,
// so refetching right at local midnight isn't quite exact for non-UTC
// timezones, but it's a reasonable "once a day, first thing" cadence.
// tm_yday guards against firing more than once during that
// 00:00-00:00:59 minute window. Runs on the background task.
void checkQuoteOfTheDayBG() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 5)) return; // clock not synced yet

    if (timeinfo.tm_hour == 0 && timeinfo.tm_min == 0 && timeinfo.tm_yday != lastQuoteFetchDay) {
        Serial.println("[BG] Midnight - refreshing quote of the day");
        fetchQuoteBG();
    }
}

// The actual background task loop. Pinned to core 0 - Arduino's loop()
// runs on core 1 by default - so a slow HTTP round-trip here genuinely
// cannot stall drawing/input on the other core. Polls every 500ms, which
// is frequent enough to catch the flight interval (60s by default,
// adjustable 5-3600s in Settings > API - see Config.cpp) and the midnight
// quote check promptly without spinning the CPU.
void backgroundNetworkTask(void* pvParameters) {
    unsigned long bgLastFlightCheck = 0;

    // How often this unit phones home to the dashboard (see
    // src/api/checkin_api.cpp + web/functions/api/devices/checkin.js).
    // 30s keeps "last seen" feeling live without troubling D1's free-tier
    // write quota (100k rows/day - one device at 30s is ~2,880/day, so the
    // quota comfortably covers dozens of these before it'd ever matter).
    // The dashboard calls a unit offline after 90s of silence (3 missed
    // beats), so this interval and that threshold should change together.
    const unsigned long CHECKIN_INTERVAL_MS = 30000UL;
    unsigned long bgLastCheckin = 0;

    for (;;) {
        if (wifiConnected) {
            unsigned long now = millis();

            // Flight-checking only runs while the Plane screen is actually
            // the one showing (ScreenManager::showingId() - a static read,
            // safe to call from this task same as the config screen already
            // does from the main task). Every other screen makes zero
            // flight-API calls no matter how long it's left showing, which
            // is most of a device's uptime for most users. This also gives
            // an immediate fetch "for free" the moment you switch onto the
            // Plane screen: bgLastFlightCheck was last touched (if ever)
            // during some earlier visit, so by the time you come back the
            // interval below has almost always already elapsed - no extra
            // "just switched in" bookkeeping needed. (See
            // PlaneTrackerScreen::init(), which shows a loading indicator
            // for exactly that first fetch.)
            if (ScreenManager::showingId() == ScreenId::Plane) {
                // Read the current setting every iteration (cheap int read)
                // so a change made in Settings takes effect on the very
                // next check instead of requiring a reboot.
                unsigned long flightCheckIntervalMs = (unsigned long)portal.getFlightCheckIntervalSec() * 1000UL;
                if (now - bgLastFlightCheck >= flightCheckIntervalMs) {
                    bgLastFlightCheck = now;
                    fetchFlightBG();
                }
            }

            checkQuoteOfTheDayBG();

            if (now - bgLastCheckin >= CHECKIN_INTERVAL_MS) {
                bgLastCheckin = now;
                String loc = portal.getCity();
                if (portal.getCountry().length() > 0) {
                    loc += (loc.length() > 0 ? ", " : "") + portal.getCountry();
                }
                sendCheckin(loc);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ============ BOOT-TIME PREFETCH ============
// Called by BootScreen the instant WiFi connects, while the loading bar is
// still on screen. Resolves location and fetches weather + nearby flight
// data up front, so the app is fully populated the moment boot finishes -
// no second "loading" wait after the bar completes.
void prefetchOnConnect(BootScreen::BootProgressFn progress) {
    wifiConnected = true;

    // Advance the boot bar between each blocking step (and, for the flight
    // scan, between each provider) so it never sits frozen mid-fetch.
    auto tick = [&](float f, const String &label) {
        if (progress) progress(f, label);
    };

    tick(0.00f, "Resolving location...");
    ensureLocationResolved();

    tick(0.18f, "Fetching weather...");
    if (dashboardScreen) {
        dashboardScreen->setTimezone(portal.getTimezone());
        dashboardScreen->setLocation(portal.getCity(), portal.getCountry());
        dashboardScreen->setLocationCoords(userLat, userLon); // triggers an immediate fetch
        dashboardScreen->update();                            // actually run that fetch now
    }

    // Prime the flight-check timer so loop()'s normal interval starts
    // counting from now, instead of firing again immediately after boot.
    tick(0.50f, "Scanning for flights...");
    lastFlightCheck = millis();
    flightCheckInProgress = true;
    {
        int provIdx = 0;
        doFlightCheck([&](const char *name) {
            float f = 0.50f + 0.10f * (float)provIdx;
            if (f > 0.80f) f = 0.80f;
            provIdx++;
            tick(f, String("Checking ") + name + "...");
        });
    }
    flightCheckInProgress = false;

    // Populate the quote of the day immediately too, so it's not blank the
    // first time the plane screen is shown - the background task's
    // checkQuoteOfTheDayBG() then takes over refreshing it once a day, at
    // local midnight, without blocking loop().
    tick(0.85f, "Loading quote...");
    doQuoteFetch();

    tick(1.00f, "Ready!");
}

// ============ SETUP ============
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n=== FlugVel Starting ===\n");

    // ---- GPIO Setup ----
    // Bring the backlight up DARK first (PWM, not a bare digitalWrite) so
    // the panel's uninitialised frame buffer isn't briefly lit on screen -
    // that flash was the "flicker" seen at power-on. We fade the light in
    // below, once the panel has been cleared to black.
    Backlight::begin(0);

    pinMode(KO_BUTTON, INPUT_PULLUP);
    pinMode(ENCODER_BTN, INPUT_PULLUP);

    // Core 0 is the dedicated network core: netTask (flight/quote) and the
    // dashboard's weather-prefetch task both run there and both do heavy,
    // non-yielding mbedTLS handshakes. Back-to-back handshakes across both
    // tasks legitimately starve IDLE0 for >5s, which the idle-task watchdog
    // reads as a hang and panics. Disable it on core 0 - the loop() (core
    // 1) watchdog stays on to catch real main-thread hangs.
    disableCore0WDT();

    // ---- Saved config ----
    // Loaded before the panel is oriented or lit, because both the rotation
    // and the backlight level come from it.
    ConfigStore::begin();

    // ---- TFT Setup ----
    tft.init();
    applyOrientation();
    // Record what we just applied, so a later config save can tell whether
    // handedness actually changed rather than repainting on every save.
    orientationApplied = ConfigStore::get().leftHanded != 0;
    tft.fillScreen(MY_BLACK);

    // Panel is now black - safe to turn the light on. A short fade instead
    // of a hard snap reads as a clean power-on rather than a flash.
    Backlight::fadeTo(ConfigStore::get().brightness, 160);

    // ---- WiFi Setup ----
    WiFi.mode(WIFI_STA);
    // Name the device on the DHCP lease, so it shows up in the router's
    // client list as "flugvel-a4c1" rather than "espressif" or a bare MAC.
    // Must be set before the first WiFi.begin() - which happens later, in
    // BootScreen - because the hostname is sent as part of the DHCP
    // request and is not re-sent on an already-established lease.
    WiFi.setHostname(CaptivePortal::deviceName().c_str());
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    // Disable WiFi modem-sleep. With it on, the radio power-cycles its
    // receive circuitry on a steady rhythm to listen for each DTIM beacon,
    // and that periodic current draw visibly dims/pulses the backlight -
    // a power-rail issue (voltage sag when the radio keys up), not a PWM
    // timing one; changing the backlight's own PWM clock source didn't
    // touch it. Leaving modem-sleep off keeps the radio's draw steady
    // instead of pulsed, so the backlight stays steady too. Costs a little
    // idle power/runs the ESP32 a bit warmer - see Backlight.cpp for the
    // real fix (a bulk capacitor on the supply rail) if that's worth doing.
    WiFi.setSleep(false);

    // ---- Load Saved WiFi + Location (before the boot screen runs, so the
    // loading bar can use them for a real connection attempt) ----
    String savedSSID;
    String savedPASS;
    bool hasWiFi = portal.loadWiFiEEPROM(savedSSID, savedPASS);
    portal.loadCalendarUrl(); // NVS-backed, independent of the WiFi/EEPROM block

    // Feed list for the Calendar screen. Takes the old single-URL setting
    // as a seed, so a device that already had one calendar keeps it.
    CalendarFeeds::begin(portal.getCalendarUrl());
    NotesSource::begin();

    if (hasWiFi) {
        portal.loadLocationEEPROM();
        loadUserLocation();
        Serial.printf("[Main] Saved WiFi found. Last known location: %.4f, %.4f\n", userLat, userLon);
    } else {
        Serial.println("[Main] No saved WiFi found.");
    }

    // Apply the persisted theme choice (defaults to Mono Dot-Matrix if
    // there's nothing saved yet, e.g. first-ever boot or no saved WiFi to
    // have loaded it from) before the boot screen draws anything.
    ThemeManager::begin(portal.getThemeId());

    // Apply persisted date/clock format + temperature unit (Settings >
    // Date & clock / Temperature) before anything draws a time or a temp.
    Units::begin(portal.getDateTimeFormat(), portal.getImperial());

    // ---- Create screens up front (before boot) so the boot-time prefetch
    // callback below has something to populate ----
    screenManager = new ScreenManager(&tft, ENCODER_BTN, KO_BUTTON);

    // Build every screen from ScreenRegistry's table (see ScreenRegistry.h)
    // and add it in that order, instead of six hand-written constructor
    // calls followed by six matching addScreen() calls that had to stay in
    // step with each other. Adding a screen is now one row in that table.
    for (int i = 0; i < ScreenRegistry::count(); i++) {
        const ScreenDef &def = ScreenRegistry::all()[i];
        screenManager->addScreen(def.make(&tft, &portal));
    }

    // Work out what the cycle actually looks like: the user's enabled
    // screens in their saved order, Settings pinned last. Everything that
    // used to be derived from a "home screen index" - which screen boots,
    // what the next screen is, which pager dot is filled - now falls out of
    // this one ordering.
    screenManager->rebuildCycle();

    // "Resume last used" instead of always starting at home. Only takes
    // effect if that screen is still in the cycle - hiding it in the
    // meantime falls back to home rather than to nothing.
    if (ConfigStore::get().startupResume) {
        screenManager->selectById((ScreenId)ConfigStore::get().lastScreenId);
    }

    // The concrete pointers below are only for the wiring that follows -
    // handing screens their location, chrome and so on. Looking them up by
    // stable id keeps main.cpp out of the business of knowing what order
    // the registry listed them in. The casts are safe because the id is
    // exactly what identifies the concrete type.
    planeScreen      = static_cast<PlaneTrackerScreen*>(screenManager->byId(ScreenId::Plane));
    dashboardScreen  = static_cast<DashboardScreen*>   (screenManager->byId(ScreenId::Weather));
    gamesScreen      = static_cast<GamesScreen*>       (screenManager->byId(ScreenId::Games));
    focusTimerScreen = static_cast<FocusTimerScreen*>  (screenManager->byId(ScreenId::Focus));
    calendarScreen   = static_cast<CalendarScreen*>    (screenManager->byId(ScreenId::Calendar));
    settingsScreen   = static_cast<SettingsScreen*>    (screenManager->byId(ScreenId::Settings));

    // Optional per-device overrides from the captive portal's manual setup
    // form - both are no-ops if the user left the field blank, so this is
    // safe to call unconditionally on every boot.
    planeScreen->setBounceText(portal.getBounceText());
    planeScreen->setDisplayBearing(portal.getFacingBearingDeg());

    // The Theme submenu recolours the whole panel live as you preview - let
    // it repaint the shared header/legend strips too, not just its own body.
    settingsScreen->setChrome(&header, &actionLegend);

    // NOTE: the plane screen's pager index and count are pushed by
    // ScreenManager::draw() before every frame (setPagerPosition /
    // setPagerCount), so there is nothing to seed here - and seeding it
    // would go stale the moment the user hides a screen.

    // No "start screen" to set: rebuildCycle() above already put the
    // configured home screen at position 0, and the cycle starts there.
    // Nothing is init()ed yet either - the initCurrentScreen() calls
    // further down do that once the panel has finished painting.

    screenManager->setHeader(&header);
    screenManager->setActionLegend(&actionLegend);

    // ---- Background network task ----
    // Create the mutex + task now so it's ready the instant WiFi connects
    // (the task itself just polls `wifiConnected` and no-ops until then).
    // See the big comment above pendingFlightReady/etc. for why this
    // exists: it keeps the 30s flight check and daily quote fetch off the
    // main task entirely, so loop() (drawing, input, the focus timer) never
    // freezes while waiting on an HTTP request.
    dataMutex = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(
        backgroundNetworkTask,
        "netTask",
        12288,   // stack size in bytes - generous headroom for HTTPClient/TLS + JSON parsing
        nullptr,
        1,       // priority
        &bgTaskHandle,
        0        // pin to core 0 - Arduino's loop() runs on core 1
    );

    // ---- Boot Screen ----
    // If we have saved WiFi, this actually connects using it (not a guess),
    // and the moment it succeeds, prefetchOnConnect() runs WHILE the bar is
    // still showing - fetching weather + nearby flight data so the app is
    // fully ready the instant the bar finishes. If we don't have saved
    // WiFi, the bar just fills quickly and we go straight to setup.
    boot.show(12000, wifiConnected, hasWiFi, savedSSID, savedPASS, prefetchOnConnect);

    if (wifiConnected) {
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);

        header.begin(portal.getTimezone());

        // Check for a firmware update now that WiFi is confirmed working -
        // before the first real screen inits, so the prompt (if there's
        // anything to prompt about) is the very first thing shown instead
        // of flashing the normal UI first. A failed/empty check just falls
        // straight through to normal boot below.
        Serial.println("[Main] Checking for firmware update...");
        UpdateInfo latestUpdate = checkForUpdate();
        if (latestUpdate.updateAvailable) {
            bool wantsUpdate = updatePromptScreen.show(latestUpdate);
            if (wantsUpdate) {
                updatePromptScreen.showUpdatingScreen();
                bool ok = performOTAUpdate(latestUpdate.url, [](int percent) {
                    updatePromptScreen.updateProgress(percent);
                });
                if (!ok) {
                    Serial.println("[Main] Update failed - continuing on current firmware");
                    // Falls through to normal boot below on the existing version.
                }
                // On success, performOTAUpdate() reboots the device itself
                // and none of the code below this point ever runs.
            }
        }

        // Boot has fully finished painting the panel now (welcome text,
        // "Powered by KML", loading bar) - safe to init the first screen
        // so its border actually gets drawn and stays drawn.
        screenManager->initCurrentScreen();

        Serial.println("[Main] Ready - connected and pre-loaded.");
        Serial.println(WiFi.localIP());
    } else {
        // Either no saved credentials, or the saved ones failed - start
        // the setup captive portal.
        Serial.println("[Main] Starting captive portal for setup...");
        WiFi.softAPdisconnect(false);  // Keep AP on
        WiFi.mode(WIFI_AP_STA);
        portal.beginAP();
        portalRunning = true;
        wifiScreen.showStep1();
    }

    Serial.println("FlugVel Initialization Complete!\n");
}

// ============ LOOP ============
void loop() {
    // ---- Handle the web server ----
    // Driven off the portal's own state rather than portalRunning, because
    // there are two things that can raise it now: first-run setup (which
    // portalRunning tracks) and Settings > Configure on phone, which can
    // start it long after setup is done.
    if (portal.isRunning()) {
        portal.handle();
    }

    // NOT nested inside isRunning() above. The save handler raises these
    // flags AND asks the device to close the portal; SettingsScreen sees
    // that request and stops the server on the same tick, so by the time
    // control got back here the gate was already false and every setting
    // was silently dropped. The flags outlive the server on purpose.
    {
        // Applied out here rather than inside the route handler: the
        // handler runs mid-loop, potentially from SettingsScreen::update(),
        // and re-initing a screen from there would mean a screen changing
        // under the one that is currently updating.
        if (portal.consumeConfigChanged()) {
            // Everything that can be applied without a restart, applied.
            // Brightness is immediate; the header/legend changes land via
            // the invalidate() inside applyConfigChange(); per-screen values
            // are read at draw time, so they need nothing here.
            Backlight::set(ConfigStore::get().brightness);

            // Clock format and units are read through Units, not straight
            // from the config, so they need pushing across before anything
            // redraws with them.
            Units::begin(ConfigStore::get().dateTimeFormat,
                         ConfigStore::get().imperial != 0);
            header.invalidate();

            bool nowLeft = ConfigStore::get().leftHanded != 0;
            bool flipped = (nowLeft != orientationApplied);

            // Idle text and facing live in the portal's EEPROM block, not
            // the config blob, so they have to be handed back to the plane
            // screen explicitly rather than being read at draw time.
            if (planeScreen) {
                planeScreen->setBounceText(portal.getBounceText());
                planeScreen->setDisplayBearing(portal.getFacingBearingDeg());
            }

            if (screenManager) {
                screenManager->applyConfigChange();
                screenManager->notifyConfigChanged();
            }

            if (flipped) {
                // Rotating the panel invalidates every pixel on it, not just
                // the content, so this is one of the few places a full
                // fillScreen is correct - forceRepaint() below invalidates
                // the header and legend and re-inits the screen, so nothing
                // is left blank. Screen *drawing* code must still never do
                // this (it would wipe the header strip and leave it blank
                // until the next minute tick).
                orientationApplied = nowLeft;
                applyOrientation();
                tft.fillScreen(ThemeManager::current().bg);
                if (screenManager) screenManager->forceRepaint();
            }

            Serial.println("[Main] Config changed from the web page - applied");
        }

        // A new location invalidates everything that was fetched for the old
        // one. Handled separately from the config block above because it is
        // the only change that has to go back out to the network: the plane
        // search is centred on these coordinates and the forecast belongs to
        // them, so both are thrown away and re-fetched rather than left
        // showing somewhere the device no longer is.
        if (portal.consumeLocationChanged()) {
            loadUserLocation();

            // Fills in whatever was left blank - geocodes a typed city, or
            // falls back to an IP estimate. Blocking, but only for a second
            // or two, and only right after the user pressed Save.
            ensureLocationResolved();

            header.setTimezone(portal.getTimezone());

            if (dashboardScreen) {
                dashboardScreen->setTimezone(portal.getTimezone());
                dashboardScreen->setLocation(portal.getCity(), portal.getCountry());
                dashboardScreen->setLocationCoords(userLat, userLon);  // refetches
            }

            // Drop the aircraft that was overhead somewhere else, and let the
            // next loop pass fetch for the new position instead of waiting
            // out the rest of the interval.
            if (planeScreen) planeScreen->clearFlight();
            currentFlight = Flight();
            lastFlightCheck = 0;

            Serial.printf("[Main] Location changed to %s (%.4f, %.4f) - refetching\n",
                          portal.getCity().c_str(), userLat, userLon);
        }
    }

    // ---- WiFi Connection Status ----
    if (!wifiConnected && WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        portalRunning = false;
        setupScreenShown = false;  // Reset flag for next time

        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);

        portal.loadLocationEEPROM();
        loadUserLocation();
        ensureLocationResolved(); // needs internet - only possible now

        String tz = portal.getTimezone();

        Serial.println("WiFi Connected!");
        Serial.println(WiFi.localIP());

        // No visible "Connected to WiFi!" + IP screen here any more - it
        // was on screen for a flat 2s no matter what, telling the person
        // nothing they need (they just watched the QR/status screen
        // confirm the join). screenManager->initCurrentScreen() below
        // paints the real first screen immediately instead.
        header.begin(tz);

        // Update dashboard with location
        dashboardScreen->setTimezone(tz);
        dashboardScreen->setLocation(portal.getCity(), portal.getCountry());
        dashboardScreen->setLocationCoords(userLat, userLon);

        // First-time setup path: the plane screen was constructed in setup()
        // BEFORE the captive portal wrote these values to EEPROM, so apply
        // the just-entered idle text / screen-facing now instead of only
        // picking them up on the next reboot.
        if (planeScreen) {
            planeScreen->setBounceText(portal.getBounceText());
            planeScreen->setDisplayBearing(portal.getFacingBearingDeg());
        }

        // Populate flight + quote up front, same as the saved-Wi-Fi boot
        // path's prefetchOnConnect() does - otherwise the quote strip stays
        // blank (the background task only refreshes it at local midnight)
        // and the plane screen shows nothing until the next 30s flight poll.
        doFlightCheck();
        doQuoteFetch();

        // Same reasoning as the boot path above: the "Connected to WiFi!"
        // message just painted over everything, so (re)init the current
        // screen now that the panel is settled.
        if (screenManager) screenManager->initCurrentScreen();
    }

    // ---- Flight Detection & Quote of the Day (non-blocking) ----
    // The actual network fetches happen on the background task; these just
    // pick up whatever result is ready, which is fast enough to call every
    // single loop iteration without any interval check needed here.
    applyFlightResult();
    applyQuoteResult();

    // ---- Screen Manager Update & Draw ----
    if (screenManager && wifiConnected) {
        // Only show screens after WiFi is connected
        screenManager->update();
        screenManager->draw();
    } else if (!wifiConnected && portalRunning) {
        // Show WiFi setup screen with QR code (only show once)
        if (!setupScreenShown) {
            setupScreenShown = true;
            wifiScreen.showStep1();  // Shows QR code and WiFi info
        }
    }

    // ---- Handle Input (ScreenManager handles debouncing) ----
    if (screenManager && wifiConnected) {
        screenManager->handleEncoderInput();     // push = next screen
        screenManager->handleEncoderRotation();  // turn = up/down within screen
        screenManager->handleButtonInput();
    }

    // ---- Factory Reset ----
    bool koPressed = digitalRead(KO_BUTTON) == LOW;
    bool encoderPressed = digitalRead(ENCODER_BTN) == LOW;

    if (koPressed && encoderPressed) {
        if (resetStartTime == 0) {
            resetStartTime = millis();
        }

        if (!resetTriggered && millis() - resetStartTime > RESET_HOLD_TIME) {
            resetTriggered = true;

            tft.fillScreen(MY_BLACK);
            tft.setTextColor(MY_RED);
            tft.setTextSize(2);

            tft.setCursor(40, 120);
            tft.println("Factory Reset...");

            delay(2000);

            factoryReset();
        }
    }
    else {
        resetStartTime = 0;
        resetTriggered = false;
    }

    delay(50);  // Main loop delay
}