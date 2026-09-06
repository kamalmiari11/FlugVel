#pragma once
#include <Arduino.h>
#include <functional>

// Flight data structure - unchanged. Every provider (see flight_api.cpp)
// converts its own JSON into exactly this shape, so nothing outside this
// file needs to know which API the data actually came from.
struct Flight {
    String callsign;
    String originCountry;
    String icaoHex;    // ICAO 24-bit address (lowercase hex). Used only to
                       // look up the country of registration when a provider
                       // doesn't send one (the ADS-B feeds don't) - see
                       // flight_api.cpp. Empty if unknown.
    float speed;        // m/s
    float altitude;     // meters
    float heading;      // degrees 0-360
    float latitude;
    float longitude;

    Flight() : speed(0), altitude(0), heading(0), latitude(0), longitude(0) {}
};

// Fetch the nearest flight, trying multiple providers with automatic
// fallback (OpenSky first, then free ADS-B community APIs). See
// flight_api.cpp for the provider list and fallback rules.
//
// Returns true  -> a flight was found; `flight` is filled in.
// Returns false -> no flight to show. Check `outApiFailed` (optional) to
//                  tell the two false cases apart:
//     *outApiFailed == false : at least one provider answered cleanly and
//                              there genuinely is no aircraft overhead.
//     *outApiFailed == true  : every provider errored / timed out / was
//                              rate-limited - the caller should KEEP
//                              whatever it was already showing and must
//                              NOT report "no flights detected".
// onProviderTry (optional) is called with each provider's name just before
// that provider is queried - used by the boot screen to keep its loading
// bar moving while the fallback chain works through the providers.
bool fetchNearestFlight(Flight &flight, float userLat, float userLon,
                        bool *outApiFailed = nullptr,
                        std::function<void(const char *providerName)> onProviderTry = nullptr);

// Distance calculation in km
float distanceKm(float lat1, float lon1, float lat2, float lon2);

// Build OpenSky URL with bounding box from user location
String buildOpenSkyUrl(float userLat, float userLon, float radiusKm);
