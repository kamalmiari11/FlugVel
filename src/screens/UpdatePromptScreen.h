#pragma once
#include <TFT_eSPI.h>
#include "../api/update_check.h"

// Shown once at boot, only when checkForUpdate() found something newer -
// NOT part of ScreenManager's screen cycle (same pattern as
// WiFiSetupScreen), since it's a one-off prompt rather than something you
// navigate back to. Reuses KO_BUTTON for "Update Now" (matches its role as
// the general confirm/select button everywhere else in the app) and
// ENCODER_BTN for "Later" (matches its role as "move on" from
// ScreenManager's screen-cycling).
class UpdatePromptScreen {
public:
    UpdatePromptScreen(TFT_eSPI* display, int koPin, int encoderBtnPin);

    // Draws the prompt and blocks until the user picks Update Now or
    // Later. Returns true if they chose to update (the caller is expected
    // to then call performOTAUpdate() - this class doesn't perform the
    // update itself, only the choice).
    bool show(const UpdateInfo& info);

    // Draws a simple progress bar + percentage, for use as
    // performOTAUpdate()'s progress callback target. Call
    // showUpdatingScreen() once first to paint the static parts.
    void showUpdatingScreen();
    void updateProgress(int percent);

private:
    TFT_eSPI* tft;
    int koButton;
    int encoderBtnPin;
};