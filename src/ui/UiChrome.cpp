#include "UiChrome.h"

namespace UiChrome {

void drawBracketTitle(TFT_eSPI* tft, const String &title, int x, int y, uint16_t color) {
    tft->setTextDatum(TL_DATUM);
    tft->setTextFont(1);
    tft->setTextSize(1);
    tft->setTextColor(color);
    tft->setCursor(x, y);
    tft->print("[ ");
    tft->print(title);
    tft->print(" ]");
}

void drawDotPager(TFT_eSPI* tft, int currentIndex, int count, int rightEdge, int centerY,
                   uint16_t filledColor, uint16_t hollowColor) {
    if (count <= 0) return;

    const int dotRadius = 2;
    const int dotSpacing = 9; // center-to-center

    // Rightmost dot's center sits `dotRadius` in from rightEdge; walk
    // leftward from there so the whole pager's right edge lands exactly
    // at rightEdge regardless of dot count.
    int lastCx = rightEdge - dotRadius;

    for (int i = count - 1; i >= 0; i--) {
        int cx = lastCx - (count - 1 - i) * dotSpacing;
        bool isCurrent = (i == currentIndex);
        if (isCurrent) {
            tft->fillCircle(cx, centerY, dotRadius, filledColor);
        } else {
            tft->drawCircle(cx, centerY, dotRadius, hollowColor);
        }
    }
}

void drawPanelRow(TFT_eSPI* tft, int x, int y, int w, int h, bool selected,
                   uint16_t bg, uint16_t selectBg) {
    tft->fillRect(x, y, w, h, selected ? selectBg : bg);
}

} // namespace UiChrome
