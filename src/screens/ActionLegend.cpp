#include "ActionLegend.h"
#include "../ui/Theme.h"

ActionLegend::ActionLegend(TFT_eSPI* display) {
    tft = display;
}

void ActionLegend::draw(const String &line1, const String &line2) {
    // Skip repainting if nothing changed - see class comment.
    if (_drawnOnce && line1 == _lastLine1 && line2 == _lastLine2) return;
    _drawnOnce = true;
    _lastLine1 = line1;
    _lastLine2 = line2;

    const Theme &theme = ThemeManager::current();

    int barY = tft->height() - legendHeight;

    tft->fillRect(0, barY, tft->width(), legendHeight, theme.bg);
    tft->drawFastHLine(0, barY, tft->width(), theme.rule);

    tft->setTextDatum(TL_DATUM);
    tft->setTextFont(1);
    tft->setTextSize(1);

    int textX = 6;
    int line1Y = barY + 6;
    int line2Y = barY + 6 + theme.smallLineH;

    tft->setTextColor(theme.fgDim, theme.bg);
    tft->setCursor(textX, line1Y);
    tft->print(line1.length() > 0 ? line1 : "-");

    tft->setTextColor(theme.fg, theme.bg);
    tft->setCursor(textX, line2Y);
    tft->print(line2.length() > 0 ? line2 : "-");
}
