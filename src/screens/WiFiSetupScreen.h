#pragma once
#include <TFT_eSPI.h>
#include <Arduino.h>
#include "../MyColors.h"
#include "../external/qrcodegen.hpp"

// First-run setup instructions shown while the captive portal is running:
// a QR to join the FlugVel setup Wi-Fi plus the address of the config page.
// Styled to match the rest of the app (bracket-title header strip, bordered
// content box, theme colours, bottom hint strip). The QR itself is kept
// pure black-on-white so it scans reliably under every theme.
class WiFiSetupScreen {
public:
    WiFiSetupScreen(TFT_eSPI* display, int koPin);

    void showStep1();
    bool nextPressed();

    // Provide access to credentials
    const char* getSSID() const { return ssid; }
    const char* getPassword() const { return password; }

private:
    TFT_eSPI* tft;
    int koButton;

    // Wi-Fi credentials (owned by this screen)
    const char* ssid = "FlugVel";
    const char* password = "MySecretPass";

    void drawQRCode(const char* qrText);
};
