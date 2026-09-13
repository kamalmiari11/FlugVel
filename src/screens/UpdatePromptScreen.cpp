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
    const int W = tft->width();
    const int H = tft->height();
    const int cx = W / 2;

    // Bottom-anchored off the real panel height, not a literal that assumed
    // a taller screen - the two action lines and the divider above them used
    // to sit at fixed y's (235/262) that ran past a 240px-tall panel
    // entirely, so "[Turn: Later]" was drawn off the bottom edge.
    const int turnY   = H - 10;
    const int pressY  = turnY - 27;
    const int hlineY  = pressY - 25;
    const int notesEndY = hlineY - 15; // leave a gap before the divider

    tft->fillScreen(theme.bg);
    tft->setTextDatum(MC_DATUM);

    // theme.accent is the same role SettingsScreen's own update page uses
    // for "Available: vX" - this prompt IS that moment, just full-screen.
    tft->setTextSize(2);
    tft->setTextColor(theme.accent);
    tft->drawString("Update Available", cx, 30);

    tft->setTextSize(1);
    tft->setTextColor(theme.fg);
    char versionLabel[32];
    snprintf(versionLabel, sizeof(versionLabel), "Version %s", info.version.c_str());
    tft->drawString(versionLabel, cx, 55);

    // Notes can run long - wrap crudely at a fixed character count rather
    // than pulling in a text-wrapping library for a handful of lines.
    tft->setTextColor(theme.fgDim);
    String notes = info.notes;
    const int maxCharsPerLine = 42;
    int y = 78;
    while (notes.length() > 0 && y < notesEndY) {
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
        tft->drawString(line, cx, y);
        y += 14;
    }

    tft->drawFastHLine(30, hlineY, W - 60, theme.rule);

    tft->setTextSize(2);
    tft->setTextColor(theme.fg);
    tft->drawString("[Press: Download]", cx, pressY);
    tft->setTextColor(theme.fgDim);
    tft->drawString("[Turn: Later]", cx, turnY);

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
    const int cx = tft->width() / 2;

    tft->fillScreen(theme.bg);
    tft->setTextDatum(MC_DATUM);

    tft->setTextSize(2);
    tft->setTextColor(theme.fg);
    tft->drawString("Downloading Update", cx, 80);

    tft->setTextSize(1);
    tft->setTextColor(theme.fgDim);
    tft->drawString("Do not power off the device", cx, 110);

    tft->drawRect(cx - 120, 140, 240, 20, theme.fgDim);

    tft->setTextDatum(TL_DATUM);
}

void UpdatePromptScreen::showFailed() {
    const Theme &theme = ThemeManager::current();
    const int W = tft->width();
    const int H = tft->height();
    const int cx = W / 2;

    tft->fillScreen(theme.bg);
    tft->setTextDatum(MC_DATUM);

    tft->setTextSize(2);
    tft->setTextColor(theme.danger);
    tft->drawString("Update Failed", cx, H / 2 - 50);

    tft->setTextSize(1);
    tft->setTextColor(theme.fg);
    tft->drawString("Please try again later.", cx, H / 2 - 15);
    tft->drawString("If the problem persists,", cx, H / 2 + 3);
    tft->drawString("please contact support.", cx, H / 2 + 21);

    tft->setTextSize(2);
    tft->setTextColor(theme.fgDim);
    tft->drawString("[Press to continue]", cx, H - 20);

    tft->setTextDatum(TL_DATUM);

    // Only one option here - either button just means "go on with the
    // current version", same debounce/poll approach as show() above.
    while (true) {
        if (digitalRead(koButton) == LOW || digitalRead(encoderBtnPin) == LOW) {
            delay(30);
            return;
        }
        delay(10);
    }
}

void UpdatePromptScreen::updateProgress(int percent) {
    const Theme &theme = ThemeManager::current();
    const int cx = tft->width() / 2;
    const int barX = cx - 120;

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    int fillWidth = (240 - 4) * percent / 100;
    tft->fillRect(barX + 2, 142, 240 - 4, 16, theme.bg); // clear previous fill only, not the outline
    // accent2 is this app's "positive/in-progress-and-going-well" role -
    // same one SettingsScreen's own OTA progress bar (settingsOtaProgress()) uses.
    if (fillWidth > 0) tft->fillRect(barX + 2, 142, fillWidth, 16, theme.accent2);

    char pctLabel[8];
    snprintf(pctLabel, sizeof(pctLabel), "%d%%", percent);
    tft->setTextDatum(MC_DATUM);
    tft->setTextSize(1);
    tft->setTextColor(theme.fg);
    tft->fillRect(cx - 30, 165, 60, 12, theme.bg);
    tft->drawString(pctLabel, cx, 171);
    tft->setTextDatum(TL_DATUM);
}
