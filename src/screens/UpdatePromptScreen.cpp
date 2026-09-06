#include "UpdatePromptScreen.h"
#include "../MyColors.h"

UpdatePromptScreen::UpdatePromptScreen(TFT_eSPI* display, int koPin, int encoderBtn)
    : tft(display), koButton(koPin), encoderBtnPin(encoderBtn)
{
}

// Layout mirrors the Settings screen's API/Home Screen pages - title up
// top, the current value in a rounded highlighted box, then supporting
// text, then hints at the bottom - so this prompt reads as part of the
// same app instead of a bolted-on dialog.
bool UpdatePromptScreen::show(const UpdateInfo& info) {
    tft->fillScreen(MY_BLACK);
    tft->setTextDatum(MC_DATUM);

    tft->setTextSize(2);
    tft->setTextColor(MY_CYAN);
    tft->drawString("Update Available", 160, 35);

    tft->fillRoundRect(20, 60, 280, 40, 4, MY_DARK_GRAY);
    char versionLabel[32];
    snprintf(versionLabel, sizeof(versionLabel), "Version %s", info.version.c_str());
    tft->setTextColor(MY_YELLOW);
    tft->drawString(versionLabel, 160, 80);

    // Notes can run long - wrap crudely at a fixed character count rather
    // than pulling in a text-wrapping library for a handful of lines.
    tft->setTextSize(1);
    tft->setTextColor(MY_GRAY);
    String notes = info.notes;
    const int maxCharsPerLine = 42;
    int y = 120;
    while (notes.length() > 0 && y < 195) {
        String line;
        if ((int)notes.length() <= maxCharsPerLine) {
            line = notes;
            notes = "";
        } else {
            int breakAt = notes.lastIndexOf(' ', maxCharsPerLine);
            if (breakAt <= 0) breakAt = maxCharsPerLine;
            line = notes.substring(0, breakAt);
            notes = notes.substring(breakAt + 1);
        }
        tft->drawString(line, 160, y);
        y += 14;
    }

    tft->drawFastHLine(30, 210, 260, MY_DARK_GRAY);

    tft->setTextSize(2);
    tft->setTextColor(MY_WHITE);
    tft->drawString("[Press: Download]", 160, 235);
    tft->setTextColor(MY_GRAY);
    tft->drawString("[Turn: Later]", 160, 262);

    tft->setTextDatum(TL_DATUM); // restore default for whatever draws next

    // Wait here for a decision - this runs before ScreenManager's normal
    // input loop even starts, so there's no conflict polling the pins
    // directly (same approach WiFiSetupScreen::nextPressed() uses).
    while (true) {
        if (digitalRead(koButton) == LOW) {
            delay(30); // crude debounce, matches this codebase's other standalone-screen input handling
            return true;
        }
        if (digitalRead(encoderBtnPin) == LOW) {
            delay(30);
            return false;
        }
        delay(10);
    }
}

void UpdatePromptScreen::showUpdatingScreen() {
    tft->fillScreen(MY_BLACK);
    tft->setTextDatum(MC_DATUM);

    tft->setTextSize(2);
    tft->setTextColor(MY_CYAN);
    tft->drawString("Downloading Update", 160, 80);

    tft->setTextSize(1);
    tft->setTextColor(MY_GRAY);
    tft->drawString("Do not power off the device", 160, 110);

    tft->drawRect(40, 140, 240, 20, MY_GRAY);

    tft->setTextDatum(TL_DATUM);
}

void UpdatePromptScreen::updateProgress(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    int fillWidth = (240 - 4) * percent / 100;
    tft->fillRect(42, 142, 240 - 4, 16, MY_BLACK); // clear previous fill only, not the outline
    if (fillWidth > 0) tft->fillRect(42, 142, fillWidth, 16, MY_GREEN);

    char pctLabel[8];
    snprintf(pctLabel, sizeof(pctLabel), "%d%%", percent);
    tft->setTextDatum(MC_DATUM);
    tft->setTextSize(1);
    tft->setTextColor(MY_WHITE);
    tft->fillRect(130, 165, 60, 12, MY_BLACK);
    tft->drawString(pctLabel, 160, 171);
    tft->setTextDatum(TL_DATUM);
}