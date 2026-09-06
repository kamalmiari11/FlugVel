#pragma once
#include <TFT_eSPI.h>
#include <Arduino.h>

// Small, stateless drawing helpers shared by every screen so borders/
// spacing/row-styling aren't reimplemented per screen. All colors are
// passed in explicitly by the caller (normally Theme::current()'s fields)
// - nothing in here hardcodes a palette.
namespace UiChrome {

    // "[ TITLE ]" - literal brackets, left-aligned, TFT_eSPI's built-in
    // font at size 1. Draws at (x, y) using the datum/cursor convention
    // (top-left of the text sits at x,y).
    void drawBracketTitle(TFT_eSPI* tft, const String &title, int x, int y, uint16_t color);

    // One small dot per top-level screen, right-aligned so the pager's
    // right edge lands at `rightEdge`. currentIndex's dot is filled solid;
    // the rest are hollow/dim. No numeric "x/N" label - dots only.
    void drawDotPager(TFT_eSPI* tft, int currentIndex, int count, int rightEdge, int centerY,
                       uint16_t filledColor, uint16_t hollowColor);

    // Bordered/shaded table/list row: fills the row's background (inverted
    // fill + text color when selected, theme background otherwise) and
    // leaves the caller to draw its own text on top at the same y. Every
    // list-style screen (forecast days, hourly grid, settings options,
    // game list) should route its row backgrounds through this instead of
    // reimplementing selection highlighting individually.
    void drawPanelRow(TFT_eSPI* tft, int x, int y, int w, int h, bool selected,
                       uint16_t bg, uint16_t selectBg);

} // namespace UiChrome
