#include "UpdatePromptScreen.h"
#include "../ui/Theme.h"

UpdatePromptScreen::UpdatePromptScreen(TFT_eSPI* display, int koPin, int encoderBtn)
    : tft(display), koButton(koPin), encoderBtnPin(encoderBtn)
{
}

// Layout mirrors the Settings screen's own update page (drawUpdateSettings()/
// runOTAUpdate() in SettingsScreen.cpp) - same theme roles, same "Do not
// power off" wording, same progress bar treatment - so this reads as part
// of the same app instead of a bolted-on dialog. This screen used to draw
// with hardcoded colors from the pre-theme MyColors.h palette, which is
// why it looked wrong on any theme other than whichever one those colors
// happened to have been picked for - every other screen goes through
// ThemeManager::current() and this one now does too.
bool UpdatePromptScreen::show(const UpdateInfo& info) {
    const Theme &theme = ThemeManager::current();

    tft->fillScreen(theme.bg);
    tft->setTextDatum(MC_DATUM);

    // theme.accent is the same role SettingsScreen's own update page uses
    // for "Available: vX" - this prompt IS that moment, just full-screen.
    tft->setTextSize(2);
    tft->setTextColor(theme.accent);
    tft->drawString("Update Available", 160, 35);

    tft->setTextSize(1);
    tft->setTextColor(theme.fg);
    char versionLabel[32];
    snprintf(versionLabel, sizeof(versionLabel), "Version %s", info.version.c_str());
    tft->drawString(versionLabel, 160, 65);

    // Notes can run long - wrap crudely at a fixed character count rather
    // than pulling in a text-wrapping library for a handful of lines.
    tft->setTextColor(theme.fgDim);
    String notes = info.notes;
    const int maxCharsPerLine = 42;
    int y = 90;
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

    tft->drawFastHLine(30, 210, 260, theme.rule);

    tft->setTextSize(2);
    tft->setTextColor(theme.fg);
    tft->drawString("[Press: Download]", 160, 235);
    tft->setTextColor(theme.fgDim);
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
    const Theme &theme = ThemeManager::current();

    tft->fillScreen(theme.bg);
    tft->setTextDatum(MC_DATUM);

    tft->setTextSize(2);
    tft->setTextColor(theme.fg);
    tft->drawString("Downloading Update", 160, 80);

    tft->setTextSize(1);
    tft->setTextColor(theme.fgDim);
    tft->drawString("Do not power off the device", 160, 110);

    tft->drawRect(40, 140, 240, 20, theme.fgDim);

    tft->setTextDatum(TL_DATUM);
}

void UpdatePromptScreen::updateProgress(int percent) {
    const Theme &theme = ThemeManager::current();

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    int fillWidth = (240 - 4) * percent / 100;
    tft->fillRect(42, 142, 240 - 4, 16, theme.bg); // clear previous fill only, not the outline
    // accent2 is this app's "positive/in-progress-and-going-well" role -
    // same one SettingsScreen's own OTA progress bar (settingsOtaProgress()) uses.
    if (fillWidth > 0) tft->fillRect(42, 142, fillWidth, 16, theme.accent2);

    char pctLabel[8];
    snprintf(pctLabel, sizeof(pctLabel), "%d%%", percent);
    tft->setTextDatum(MC_DATUM);
    tft->setTextSize(1);
    tft->setTextColor(theme.fg);
    tft->fillRect(130, 165, 60, 12, theme.bg);
    tft->drawString(pctLabel, 160, 171);
    tft->setTextDatum(TL_DATUM);
}
