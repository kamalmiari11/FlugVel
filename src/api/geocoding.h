#pragma once
#include <Arduino.h>

// Resolves a typed city name (optionally narrowed down by a country name/
// code) to precise coordinates + timezone, using Open-Meteo's free
// geocoding API (no key required). Needs internet access.
//
// This is preferred over IP-based geolocation whenever the user has typed
// a city, since IP geolocation is only accurate to "somewhere in your ISP's
// service area" (can easily be off by several towns), while this resolves
// the actual place name.
//
// `resolvedCity`/`resolvedCountry` come back from the API (useful for
// confirming what was matched) but callers are free to keep the user's own
// typed spelling instead if they prefer.
bool geocodeCity(const String &city, const String &countryHint,
                  float &lat, float &lon,
                  String &resolvedCity, String &resolvedCountry,
                  String &timezone);