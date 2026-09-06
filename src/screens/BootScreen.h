#pragma once
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <functional>

// Boot animation: types the device name character-by-character with a
// blinking cursor, then runs a segmented progress bar while attempting to
// connect (or skipping straight to setup if there's nothing saved to try).
// Uses ThemeManager::current() for all colors - by the time this runs,
// main.cpp has already called ThemeManager::begin() with the persisted
// theme choice.
class BootScreen {
public:
    BootScreen(TFT_eSPI* display);

    // Passed to onConnectedEarly so the prefetch work can keep the loading
    // bar moving instead of letting it sit frozen while each network call
    // blocks. fraction01 is 0..1 across the bar's final ("prefetching")
    // segment; label is the status line to show above the bar.
    using BootProgressFn = std::function<void(float fraction01, const String &label)>;

    // Runs the boot animation + loading bar, and actually attempts to
    // connect using the caller's saved WiFi credentials while it does.
    //
    // - hasSavedWifi == false: nothing to connect to, so the bar fills
    //   quickly (a few hundred ms) instead of pretending to try for the
    //   full duration - the caller should go straight to the setup page.
    //
    // - hasSavedWifi == true: connects with the given ssid/password while
    //   the bar animates (up to connectTimeout). The moment the connection
    //   succeeds, onConnectedEarly() is called once (if provided) - this is
    //   where the caller should kick off background work like fetching
    //   weather/flight data, so by the time this function returns the app
    //   is already fully populated and ready to use, not just connected.
    void show(unsigned long connectTimeout,
              bool &wifiConnected,
              bool hasSavedWifi,
              const String &ssid,
              const String &password,
              std::function<void(BootProgressFn)> onConnectedEarly = nullptr);

private:
    TFT_eSPI* tft;
};