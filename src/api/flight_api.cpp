#include "flight_api.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ============================================================================
// MULTI-PROVIDER FLIGHT LOOKUP
// ============================================================================
// Primary provider is still OpenSky. If OpenSky answers HTTP 200 the data is
// used exactly as before. If it returns HTTP 429 the provider is marked
// rate-limited (and skipped for a cooldown period), and the next provider is
// tried. Any other failure - transport error, timeout, HTTP 5xx/4xx, empty
// body, invalid JSON - also just falls through to the next provider.
//
// A "no aircraft overhead" result is only reported to the caller when at
// least one provider actually answered cleanly. If every provider failed,
// fetchNearestFlight() sets *outApiFailed = true so the UI keeps showing
// whatever it had instead of flipping to "no flights detected".
//
// Fallback providers (all verified reachable over plain HTTPS from an ESP32,
// no API key, free, currently maintained, documented endpoints):
//
//   * adsb.lol   https://api.adsb.lol/v2/lat/{lat}/lon/{lon}/dist/{nm}
//                docs: https://api.adsb.lol/docs   (github.com/adsblol/api)
//   * adsb.fi    https://opendata.adsb.fi/api/v3/lat/{lat}/lon/{lon}/dist/{nm}
//                docs: github.com/adsbfi/opendata
//
// Both return the readsb / tar1090 "re-api" JSON schema:
//   { "ac": [ { "flight":"BAW123 ", "lat":..,"lon":..,
//               "alt_baro":<ft|"ground">, "gs":<knots>, "track":<deg>,
//               "true_heading":<deg>, "mag_heading":<deg>, ... }, ... ] }
// so one parser (parseReApi) covers both. They carry no origin-country
// field - only the ICAO hex - so when one of them supplies the chosen
// flight, lookupCountryByHex() backfills originCountry from adsbdb.com's
// free aircraft database (hex -> country of registration).
//
// Public API terms: adsb.fi / adsb.lol open data is for personal,
// non-commercial use and asks for ~1 request/second - which this project's
// 30s+ poll interval is comfortably under.
// ============================================================================

static const float KM_PER_NM   = 1.852f;
static const float FT_TO_M     = 0.3048f;
static const float KNOT_TO_MS  = 0.514444f;
static const float SEARCH_RADIUS_KM = 10.0f;   // "overhead" filter, same as before

// How long to stop querying a provider after it answers HTTP 429.
static const unsigned long RATE_LIMIT_COOLDOWN_MS = 10UL * 60UL * 1000UL; // 10 min

// ================ DISTANCE CALCULATION ================
float distanceKm(float lat1, float lon1, float lat2, float lon2)
{
    float dLat = (lat2 - lat1) * 0.0174533;
    float dLon = (lon2 - lon1) * 0.0174533;

    float a =
        sin(dLat / 2) * sin(dLat / 2) +
        cos(lat1 * 0.0174533) * cos(lat2 * 0.0174533) *
            sin(dLon / 2) * sin(dLon / 2);

    float c = 2 * atan2(sqrt(a), sqrt(1 - a));
    return 6371 * c;
}

// ================ URL BUILDERS ================
String buildOpenSkyUrl(float userLat, float userLon, float radiusKm = 10.0)
{
    // Bounding box from center point and radius.
    // 1 degree latitude  ~ 111 km
    // 1 degree longitude ~ 111 km * cos(latitude)
    float latOffset = radiusKm / 111.0;
    float lonOffset = radiusKm / (111.0 * cos(userLat * 0.0174533));

    float minLat = userLat - latOffset;
    float maxLat = userLat + latOffset;
    float minLon = userLon - lonOffset;
    float maxLon = userLon + lonOffset;

    String url = "https://opensky-network.org/api/states/all?";
    url += "lamin=" + String(minLat, 2);
    url += "&lomin=" + String(minLon, 2);
    url += "&lamax=" + String(maxLat, 2);
    url += "&lomax=" + String(maxLon, 2);
    return url;
}

static int nmRadius(float radiusKm)
{
    int d = (int)ceilf(radiusKm / KM_PER_NM);
    return d < 1 ? 1 : d;
}

static String urlOpenSky(float lat, float lon, float radiusKm)
{
    return buildOpenSkyUrl(lat, lon, radiusKm);
}

static String urlAdsbLol(float lat, float lon, float radiusKm)
{
    return "https://api.adsb.lol/v2/lat/" + String(lat, 4) +
           "/lon/" + String(lon, 4) + "/dist/" + String(nmRadius(radiusKm));
}

static String urlAdsbFi(float lat, float lon, float radiusKm)
{
    return "https://opendata.adsb.fi/api/v3/lat/" + String(lat, 4) +
           "/lon/" + String(lon, 4) + "/dist/" + String(nmRadius(radiusKm));
}

// ================ RESPONSE PARSERS ================
// Each parser turns one provider's raw JSON into a Flight (the nearest
// aircraft inside radiusKm). ERROR means the body wasn't usable at all -
// the caller treats that like any other provider failure and falls through.
enum class ParseOutcome : uint8_t { ERROR, NO_FLIGHT, FOUND };

// OpenSky returns full ISO-3166 English country names ("United Kingdom",
// "Russian Federation", "Iran, Islamic Republic of", ...). Many overrun
// the narrow "FROM" cell on the plane screen, so map the long / formal
// ones to a short common form. Matched by substring (case-sensitive, as
// OpenSky sends them) so wording variants between OpenSky versions still
// hit. First match wins - more specific rules come first. Anything not
// listed is passed through unchanged (and the screen still ellipsis-clips
// it as a last resort).
static String shortCountryName(const String &c) {
    struct Rule { const char *needle; const char *shortName; };
    static const Rule rules[] = {
        { "United States of America",           "USA" },
        { "United States",                      "USA" },
        { "United Kingdom",                     "UK" },
        { "United Arab Emirates",               "UAE" },
        { "Russian Federation",                 "Russia" },
        { "Democratic People's Republic of Korea", "North Korea" },
        { "Korea, Democratic People's",         "North Korea" },
        { "Republic of Korea",                  "South Korea" },
        { "Korea, Republic of",                 "South Korea" },
        { "Iran",                               "Iran" },
        { "Syrian Arab Republic",               "Syria" },
        { "Venezuela",                          "Venezuela" },
        { "Bolivia",                            "Bolivia" },
        { "United Republic of Tanzania",        "Tanzania" },
        { "Tanzania",                           "Tanzania" },
        { "Republic of Moldova",                "Moldova" },
        { "Moldova",                            "Moldova" },
        { "Lao People's Democratic Republic",   "Laos" },
        { "Viet Nam",                           "Vietnam" },
        { "Brunei Darussalam",                  "Brunei" },
        { "Democratic Republic of the Congo",   "DR Congo" },
        { "Congo, The Democratic Republic",     "DR Congo" },
        { "Congo (the Democratic Republic",     "DR Congo" },
        { "Congo",                              "Congo" },
        { "Ivoire",                             "Ivory Coast" },
        { "Bosnia and Herzegovina",             "Bosnia" },
        { "North Macedonia",                    "Macedonia" },
        { "Macedonia",                          "Macedonia" },
        { "Trinidad and Tobago",                "Trinidad" },
        { "Antigua and Barbuda",                "Antigua" },
        { "Saint Kitts and Nevis",              "St Kitts" },
        { "Saint Vincent",                      "St Vincent" },
        { "Saint Lucia",                        "St Lucia" },
        { "Saint Pierre and Miquelon",          "St Pierre" },
        { "Saint Barth",                        "St Barthelemy" },
        { "Saint Martin",                       "St Martin" },
        { "Saint Helena",                       "St Helena" },
        { "Papua New Guinea",                   "Papua N.G." },
        { "Equatorial Guinea",                  "Eq. Guinea" },
        { "Guinea-Bissau",                      "Guinea-Bissau" },
        { "Central African Republic",           "C.A.R." },
        { "Dominican Republic",                 "Dominican R." },
        { "Czech",                              "Czechia" },
        { "South Sudan",                        "S. Sudan" },
        { "Solomon Islands",                    "Solomon Is." },
        { "Marshall Islands",                   "Marshall Is." },
        { "Micronesia",                         "Micronesia" },
        { "Bonaire, Sint Eustatius and Saba",   "Caribbean NL" },
        { "Netherlands Antilles",               "Neth.Antilles" },
        { "Netherlands",                        "Netherlands" },  // also catches "Kingdom of the Netherlands"
        { "Northern Mariana Islands",           "N.Mariana Is" },
        { "Falkland Islands",                   "Falklands" },
        { "Turks and Caicos Islands",           "Turks/Caicos" },
        { "British Indian Ocean Territory",     "BIOT" },
        { "British Virgin Islands",             "BVI" },
        { "United States Virgin Islands",       "US Virgin Is" },
        { "Virgin Islands (U.S.)",              "US Virgin Is" },
        { "Wallis and Futuna",                  "Wallis & F." },
        { "French Polynesia",                   "Fr.Polynesia" },
        { "French Guiana",                      "Fr. Guiana" },
        { "French Southern Territories",        "Fr.S.Terr." },
        { "New Caledonia",                      "N.Caledonia" },
        { "Faroe Islands",                      "Faroe Is." },
        { "Cayman Islands",                     "Cayman Is." },
        { "Cook Islands",                       "Cook Is." },
        { "Christmas Island",                   "Christmas I." },
        { "Norfolk Island",                     "Norfolk I." },
        { "Cocos (Keeling) Islands",            "Cocos Is." },
        { "Western Sahara",                     "W. Sahara" },
        { "South Georgia",                      "S. Georgia" },
        { "Heard Island",                       "Heard I." },
        { "Taiwan",                             "Taiwan" },
        { "Cabo Verde",                         "Cape Verde" },
        { "Timor-Leste",                        "East Timor" },
        { "State of Palestine",                 "Palestine" },
        { "Palestine",                          "Palestine" },
        { "Holy See",                           "Vatican" },
        { "Vatican",                            "Vatican" },
        { "Macao",                              "Macau" },
        { "Cura",                               "Curacao" },  // Curacao / Curaçao
        { "Sint Maarten",                       "Sint Maarten" },
    };
    for (const auto &r : rules) {
        if (c.indexOf(r.needle) >= 0) return String(r.shortName);
    }

    // Not in the table - fall back to generic reductions that shorten most
    // long/formal names automatically (so new or reworded entries from
    // OpenSky still get trimmed without needing a table row each time).
    String s = c;
    int p;
    if ((p = s.indexOf(" (")) > 0) s = s.substring(0, p);   // "X (Bolivarian Republic of)" -> "X"
    if ((p = s.indexOf(", "))  > 0) s = s.substring(0, p);   // "X, Islamic Republic of"     -> "X"

    static const char *prefixes[] = {
        "Republic of ", "Kingdom of ", "State of ", "Union of ",
        "Federation of ", "Federal Republic of ", "Commonwealth of ",
        "Principality of ", "Sultanate of ", "The "
    };
    for (const char *pre : prefixes) {
        if (s.startsWith(pre)) { s = s.substring(strlen(pre)); break; }
    }

    static const char *suffixes[] = { " Republic", " Federation" };
    for (const char *suf : suffixes) {
        if (s.endsWith(suf)) { s = s.substring(0, s.length() - strlen(suf)); break; }
    }

    s.replace(" and ", " & ");
    s.replace("Islands", "Is.");
    s.trim();

    return s.length() ? s : c;
}

static ParseOutcome parseOpenSky(const String &payload, float userLat, float userLon,
                                 float radiusKm, Flight &out)
{
    DynamicJsonDocument doc(65536);
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.printf("[API] OpenSky JSON error: %s\n", err.c_str());
        return ParseOutcome::ERROR;
    }

    JsonArray states = doc["states"];
    if (states.isNull()) return ParseOutcome::NO_FLIGHT;

    float bestDist = 1e9f;
    bool found = false;

    for (JsonArray s : states) {
        const char *icao24   = s[0];
        const char *callsign = s[1];
        const char *country  = s[2];
        float lon      = s[5];
        float lat      = s[6];
        float altitude = s[7];   // meters (OpenSky already SI)
        float heading  = s[10];

        if (!callsign || isnan(lat) || isnan(lon)) continue;

        float dist = distanceKm(userLat, userLon, lat, lon);
        if (dist > radiusKm) continue;
        if (found && dist >= bestDist) continue;

        bestDist = dist;
        found = true;

        out.callsign      = String(callsign);
        out.callsign.trim();
        out.originCountry  = country ? shortCountryName(String(country)) : "Unknown";
        out.icaoHex        = icao24 ? String(icao24) : String("");
        out.speed          = s[9].isNull() ? 0.0f : (float)s[9];  // m/s
        out.altitude       = altitude;
        out.heading        = heading;
        out.latitude       = lat;
        out.longitude      = lon;
    }

    return found ? ParseOutcome::FOUND : ParseOutcome::NO_FLIGHT;
}

// readsb / tar1090 "re-api" schema, shared by adsb.lol and adsb.fi.
static ParseOutcome parseReApi(const String &payload, float userLat, float userLon,
                               float radiusKm, Flight &out)
{
    // Only pull the handful of fields we need - these responses carry ~40
    // keys per aircraft, so filtering keeps the parse well within a small
    // heap buffer even for a wide radius.
    JsonDocument filter;
    filter["ac"][0]["hex"]          = true;
    filter["ac"][0]["flight"]       = true;
    filter["ac"][0]["lat"]          = true;
    filter["ac"][0]["lon"]          = true;
    filter["ac"][0]["alt_baro"]     = true;
    filter["ac"][0]["gs"]           = true;
    filter["ac"][0]["track"]        = true;
    filter["ac"][0]["true_heading"] = true;
    filter["ac"][0]["mag_heading"]  = true;

    DynamicJsonDocument doc(24576);
    DeserializationError err =
        deserializeJson(doc, payload, DeserializationOption::Filter(filter));
    if (err) {
        Serial.printf("[API] re-api JSON error: %s\n", err.c_str());
        return ParseOutcome::ERROR;
    }

    JsonArray ac = doc["ac"];
    if (ac.isNull()) return ParseOutcome::NO_FLIGHT;

    float bestDist = 1e9f;
    bool found = false;

    for (JsonObject a : ac) {
        const char *cs = a["flight"];
        if (!cs) continue;                       // position-only target, no ident
        if (a["lat"].isNull() || a["lon"].isNull()) continue;

        float lat = a["lat"].as<float>();
        float lon = a["lon"].as<float>();
        if (isnan(lat) || isnan(lon)) continue;

        float dist = distanceKm(userLat, userLon, lat, lon);
        if (dist > radiusKm) continue;
        if (found && dist >= bestDist) continue;

        bestDist = dist;
        found = true;

        String call = String(cs);
        call.trim();                             // "BAW123  " -> "BAW123"
        out.callsign     = call.length() ? call : String("Unknown");
        out.originCountry = "Unknown";           // ADS-B feeds omit it - filled
                                                 // in later from icaoHex if possible
        out.icaoHex      = a["hex"] | "";
        out.latitude     = lat;
        out.longitude    = lon;

        JsonVariant altV = a["alt_baro"];
        if (altV.is<const char *>())             // string "ground"
            out.altitude = 0.0f;
        else
            out.altitude = altV.isNull() ? 0.0f : altV.as<float>() * FT_TO_M;

        float gsKnots = a["gs"] | 0.0f;
        out.speed = gsKnots * KNOT_TO_MS;        // knots -> m/s

        // Prefer true track, then true/magnetic heading.
        out.heading = a["track"] | (a["true_heading"] | (a["mag_heading"] | 0.0f));
    }

    return found ? ParseOutcome::FOUND : ParseOutcome::NO_FLIGHT;
}

// ================ PROVIDER TABLE ================
typedef String       (*UrlBuilderFn)(float, float, float);
typedef ParseOutcome (*ParseFn)(const String &, float, float, float, Flight &);

struct Provider {
    const char   *name;
    UrlBuilderFn  buildUrl;
    ParseFn       parse;
    unsigned long rateLimitedUntil;   // millis() timestamp; 0 = available now
};

static Provider s_providers[] = {
    { "OpenSky",  urlOpenSky, parseOpenSky, 0 },
    { "adsb.lol", urlAdsbLol, parseReApi,   0 },
    { "adsb.fi",  urlAdsbFi,  parseReApi,   0 },
};
static const int PROVIDER_COUNT = sizeof(s_providers) / sizeof(s_providers[0]);

// ================ ONE PROVIDER ATTEMPT ================
enum class Attempt : uint8_t { FOUND, NO_FLIGHT, RATE_LIMITED, FAILED };

static Attempt tryProvider(Provider &p, float userLat, float userLon, Flight &out)
{
    String url = p.buildUrl(userLat, userLon, SEARCH_RADIUS_KM);
    Serial.printf("[API] %s: GET %s\n", p.name, url.c_str());

    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(10000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    if (!http.begin(url)) {
        Serial.printf("[API] %s: begin() failed\n", p.name);
        return Attempt::FAILED;
    }
    // setUserAgent(), not addHeader("User-Agent",...): the latter leaves the
    // built-in "ESP32HTTPClient" header in place too, so the request goes
    // out with TWO User-Agent lines and adsb.lol's WAF rejects it with 403.
    http.setUserAgent("Mozilla/5.0 (FlugVel; ESP32)");
    http.addHeader("Accept", "application/json");

    int code = http.GET();

    if (code < 0) {
        Serial.printf("[API] %s: transport error: %s\n", p.name,
                      http.errorToString(code).c_str());
        http.end();
        return Attempt::FAILED;
    }

    Serial.printf("[API] %s: HTTP %d\n", p.name, code);

    if (code == 429) {                 // rate limited
        http.end();
        return Attempt::RATE_LIMITED;
    }
    if (code != HTTP_CODE_OK) {        // 5xx, 403, 404, redirects we didn't follow, ...
        http.end();
        return Attempt::FAILED;
    }

    String payload = http.getString();
    http.end();

    if (payload.length() == 0) {
        Serial.printf("[API] %s: empty response body\n", p.name);
        return Attempt::FAILED;
    }

    ParseOutcome po = p.parse(payload, userLat, userLon, SEARCH_RADIUS_KM, out);
    if (po == ParseOutcome::ERROR)     return Attempt::FAILED;
    if (po == ParseOutcome::NO_FLIGHT) return Attempt::NO_FLIGHT;
    return Attempt::FOUND;
}

// ================ COUNTRY-OF-REGISTRATION ENRICHMENT ================
// The ADS-B feeds (adsb.lol / adsb.fi) don't carry an origin country, only
// the ICAO hex. adsbdb.com's free, no-key aircraft DB maps hex ->
// "registered_owner_country_name". Only called when a chosen flight came
// back with country "Unknown"; a tiny LRU cache means a plane that stays
// overhead for several poll cycles is looked up once.
static bool lookupCountryByHex(const String &hex, String &countryOut) {
    static String cHex[6];
    static String cCountry[6];
    static int    cNext = 0;

    for (int i = 0; i < 6; i++) {
        if (cHex[i] == hex && cCountry[i].length()) { countryOut = cCountry[i]; return true; }
    }

    String url = "https://api.adsbdb.com/v0/aircraft/" + hex;

    HTTPClient http;
    http.setConnectTimeout(6000);
    http.setTimeout(8000);
    if (!http.begin(url)) return false;
    http.setUserAgent("Mozilla/5.0 (FlugVel; ESP32)");
    http.addHeader("Accept", "application/json");

    int code = http.GET();
    if (code != HTTP_CODE_OK) { http.end(); return false; }
    String body = http.getString();
    http.end();
    if (body.length() == 0) return false;

    JsonDocument filter;
    filter["response"]["aircraft"]["registered_owner_country_name"] = true;
    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;

    const char *c = doc["response"]["aircraft"]["registered_owner_country_name"];
    if (!c || !*c) return false;

    countryOut = shortCountryName(String(c));
    cHex[cNext] = hex;
    cCountry[cNext] = countryOut;
    cNext = (cNext + 1) % 6;
    return true;
}

// ================ MAIN FETCH FUNCTION ================
bool fetchNearestFlight(Flight &flight, float userLat, float userLon, bool *outApiFailed,
                        std::function<void(const char *providerName)> onProviderTry)
{
    if (outApiFailed) *outApiFailed = false;

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[API] WiFi not connected");
        if (outApiFailed) *outApiFailed = true;   // not a real "no flights" answer
        return false;
    }

    Serial.println("\n==============================");
    Serial.printf("[API] Fetch cycle: area around %.4f, %.4f\n", userLat, userLon);

    unsigned long now = millis();
    bool gotCleanAnswer = false;   // >=1 provider returned a valid "no aircraft" reply
    int  tried = 0, failed = 0;

    for (int i = 0; i < PROVIDER_COUNT; i++) {
        Provider &p = s_providers[i];

        // Yield between providers so this (core-0-pinned) task doesn't hold
        // the CPU across several back-to-back TLS handshakes and starve the
        // weather-prefetch task / idle task on the same core.
        vTaskDelay(pdMS_TO_TICKS(10));

        // Respect an in-effect 429 cooldown. millis() wrap is handled by the
        // signed-difference comparison.
        if (p.rateLimitedUntil != 0) {
            if ((long)(p.rateLimitedUntil - now) > 0) {
                Serial.printf("[API] %s: skipped, rate-limit cooldown (%lus left)\n",
                              p.name, (unsigned long)((p.rateLimitedUntil - now) / 1000));
                continue;
            }
            p.rateLimitedUntil = 0;   // cooldown elapsed - give it another go
        }

        tried++;
        if (onProviderTry) onProviderTry(p.name);
        Attempt a = tryProvider(p, userLat, userLon, flight);

        if (a == Attempt::FOUND) {
            // Providers that don't send a country: fill it from the hex.
            if ((flight.originCountry.length() == 0 || flight.originCountry == "Unknown") &&
                flight.icaoHex.length() >= 6) {
                vTaskDelay(pdMS_TO_TICKS(10)); // yield before the extra TLS call
                String c;
                if (lookupCountryByHex(flight.icaoHex, c)) flight.originCountry = c;
            }

            Serial.printf("[API] SELECTED %s (%s) via %s (%.1f km)\n",
                          flight.callsign.c_str(), flight.originCountry.c_str(), p.name,
                          distanceKm(userLat, userLon, flight.latitude, flight.longitude));
            Serial.println("==============================\n");
            return true;
        }

        if (a == Attempt::NO_FLIGHT) {
            Serial.printf("[API] %s: no aircraft overhead\n", p.name);
            gotCleanAnswer = true;
            continue;   // another provider may have better low-altitude coverage
        }

        if (a == Attempt::RATE_LIMITED) {
            p.rateLimitedUntil = millis() + RATE_LIMIT_COOLDOWN_MS;
            failed++;
            Serial.printf("[API] %s: HTTP 429 - backing off %lu min, switching provider\n",
                          p.name, RATE_LIMIT_COOLDOWN_MS / 60000UL);
            continue;
        }

        // Attempt::FAILED
        failed++;
        Serial.printf("[API] %s: failed - switching provider\n", p.name);
    }

    Serial.printf("[API] cycle done: %d tried, %d failed, cleanAnswer=%d\n",
                  tried, failed, (int)gotCleanAnswer);
    Serial.println("==============================\n");

    if (gotCleanAnswer) {
        // At least one working provider confirmed there's nothing overhead.
        return false;   // genuine "no flights"; *outApiFailed stays false
    }

    // Nobody gave us a usable answer (all errored / rate-limited / skipped).
    if (outApiFailed) *outApiFailed = true;
    Serial.println("[API] all flight providers unavailable this cycle");
    return false;
}
