#include "BootScreen.h"
#include "../MyColors.h"
#include "../ui/Theme.h"

BootScreen::BootScreen(TFT_eSPI* display) {
    tft = display;
}

void BootScreen::show(unsigned long connectTimeout, bool &wifiConnected,
                       bool hasSavedWifi, const String &ssid, const String &password,
                       std::function<void(BootProgressFn)> onConnectedEarly)
{
    const Theme &theme = ThemeManager::current();

    // Fraction of the bar reserved for the post-connect prefetch phase:
    // connecting fills up to here, prefetch drives it the rest of the way.
    const float PREFETCH_BAR_START = 0.60f;

    // --- Already connected somehow (e.g. previous soft reset) ---
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        if (onConnectedEarly) onConnectedEarly([](float, const String &) {});
        return;
    }

    // --- Phase 1: big device-name typing effect, same layout as before
    // (large centered letters, typed in one at a time) just in the new
    // theme's colors, plus a blinking cursor after the last-typed letter ---
    tft->fillScreen(theme.bg);
    tft->setTextFont(1);
    tft->setTextSize(4);
    tft->setTextColor(theme.fg);

    const char* deviceName = "[ FLUGVEL ]";
    int letterSpacing = 24;
    int nameLen = strlen(deviceName);
    int startX = (320 - nameLen * letterSpacing) / 2;
    int y = 100;
    int charW = 20; // approx glyph width at size 4 for font 1 - cursor block width
    int charH = 32; // approx glyph height at size 4 for font 1 - cursor block height

    for (int i = 0; i < nameLen; i++) {
        tft->drawChar(deviceName[i], startX + i * letterSpacing, y, 1);

        // Blink the cursor once in the gap right after this letter, before
        // moving on to the next one.
        int cursorX = startX + (i + 1) * letterSpacing;
        for (int blink = 0; blink < 2; blink++) {
            tft->fillRect(cursorX, y, charW / 3, charH,
                          (blink % 2 == 0) ? theme.fg : theme.bg);
            delay(90);
        }
        delay(40);
    }
    // Leave the cursor off (background color) once typing has finished.
    tft->fillRect(startX + nameLen * letterSpacing, y, charW / 3, charH, theme.bg);
    delay(250);

    // --- Phase 2: separate welcome/loading screen ---
    tft->fillScreen(theme.bg);
    tft->setTextFont(1);
    tft->setTextDatum(MC_DATUM);
    tft->setTextSize(3);
    tft->setTextColor(theme.fg);
    tft->drawString("[ Welcome ]", 160, 70);

    // --- Loading bar (outlined, segmented fill) ---
    const int barWidth = 200;
    const int barHeight = 15;
    const int barX = 60;
    const int barY = 150;
    const int segments = 16;
    const int segGap = 2;
    const int segWidth = (barWidth - (segments - 1) * segGap) / segments;

    tft->drawRect(barX, barY, barWidth, barHeight, theme.fg);

    // --- "Powered by KML", under the bar ---
    tft->setTextSize(2);
    tft->setTextColor(theme.fgDim);
    tft->drawString("Powered by KML", 160, barY + barHeight + 30);

    // --- Status line above the bar, left-aligned to where the bar starts,
    // describing what's happening right now ---
    int statusY = barY - 20;
    String lastStatus = "";
    auto setStatus = [&](const String &msg) {
        if (msg == lastStatus) return; // skip redundant redraws
        tft->setTextDatum(TL_DATUM);
        tft->setTextSize(1);
        tft->setTextColor(theme.bg); // erase previous line (background color)
        tft->drawString(lastStatus, barX, statusY);
        tft->setTextColor(theme.fgDim);
        tft->drawString(msg, barX, statusY);
        lastStatus = msg;
        tft->setTextDatum(MC_DATUM);
    };

    auto drawSegmentedFill = [&](float progress) {
        if (progress < 0) progress = 0;
        if (progress > 1) progress = 1;
        int filledSegments = (int)(progress * segments + 0.001f);
        for (int i = 0; i < filledSegments; i++) {
            int sx = barX + 2 + i * (segWidth + segGap);
            tft->fillRect(sx, barY + 2, segWidth, barHeight - 4, theme.fg);
        }
    };

    if (!hasSavedWifi) {
        // No saved credentials to try - don't waste the user's time
        // pretending to connect. Fill the bar quickly (just so it doesn't
        // look broken/frozen) and hand back control so the caller can go
        // straight to the setup page.
        Serial.println("[BootScreen] No saved WiFi - skipping to setup");
        setStatus("No saved Wi-Fi found");
        const int quickSteps = 20;
        for (int i = 0; i <= quickSteps; i++) {
            drawSegmentedFill(float(i) / quickSteps);
        }
        setStatus("Starting setup...");
        wifiConnected = false;
        return;
    }

    // We do have saved credentials - actually try to connect while the
    // bar runs, using the real credentials (not a cached NVS guess).
    Serial.println("[BootScreen] Connecting with saved WiFi credentials...");
    setStatus("Connecting to Wi-Fi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    int steps = 100;
    bool ranCallback = false;

    for (int i = 0; i <= steps; i++) {
        if (WiFi.status() == WL_CONNECTED) {
            wifiConnected = true;

            if (!ranCallback && onConnectedEarly) {
                ranCallback = true;

                // Quick catch-up sweep to the prefetch start mark, so the
                // handoff from "connecting" to "loading data" reads as
                // forward progress rather than a stall.
                float from = PREFETCH_BAR_START * float(i) / steps;
                for (float p = from; p < PREFETCH_BAR_START; p += 0.03f) {
                    drawSegmentedFill(p);
                    delay(15);
                }
                drawSegmentedFill(PREFETCH_BAR_START);
                setStatus("Loading data...");

                // Runs synchronously here (location + weather + flight +
                // quote) while the bar is still showing. It calls the
                // reporter between each step so the bar keeps advancing and
                // the status line keeps updating instead of freezing.
                onConnectedEarly([&](float frac, const String &label) {
                    if (frac < 0) frac = 0;
                    if (frac > 1) frac = 1;
                    setStatus(label);
                    drawSegmentedFill(PREFETCH_BAR_START + frac * (1.0f - PREFETCH_BAR_START));
                });
            }

            // Connected (and prefetch done if any) - snap the bar to full
            // and stop, no need to keep animating/delaying further.
            drawSegmentedFill(1.0f);
            setStatus("Ready!");
            break;
        }

        // Connecting only fills up to PREFETCH_BAR_START - the rest is the
        // prefetch phase's to fill.
        drawSegmentedFill(PREFETCH_BAR_START * float(i) / steps);

        delay(connectTimeout / steps);
    }

    if (!wifiConnected) {
        setStatus("Could not connect - starting setup");
    }

    // Ran out of time without connecting - onConnectedEarly() never fires,
    // caller sees wifiConnected == false and can fall back to setup.
}
