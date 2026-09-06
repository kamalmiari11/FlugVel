#pragma once
#include <Arduino.h>

// Reports this device's live status to the FlugVel dashboard (see
// web/functions/api/devices/checkin.js) so it shows up online with no
// manual pairing. Blocking (does its own HTTP POST, same as the other
// api/ files) - call it from the background network task (core 0), on a
// timer, never from the main loop() directly. A failed check-in just logs
// and returns; the next scheduled call picks it back up automatically.
//
// `location` is whatever the device's own Wi-Fi/Location setup resolved
// (CaptivePortal::getCity() + ", " + getCountry()) - the dashboard shows
// this automatically now, no manual typing needed on the admin side.
void sendCheckin(const String& location);
