#include "NotesScreen.h"
#include "../config/NotesSource.h"
#include "../ui/Theme.h"
#include <WiFi.h>

NotesScreen::NotesScreen(TFT_eSPI* display)
    : Screen(display), _count(0), _sel(0), _scroll(0),
      _fetched(false), _lastError(0), _lastFetch(0), _needsRedraw(true) {}

void NotesScreen::init() {
    _needsRedraw = true;
}

// ---- data ----
void NotesScreen::doFetch() {
    const Theme &t = ThemeManager::current();
    const int W = tft->width();

    // The Notion fetch is blocking (a second or so) - show a hint first,
    // same as the calendar screen's own doFetch().
    tft->fillRect(0, 20, W, tft->height() - 50, t.bg);
    tft->setTextDatum(MC_DATUM);
    tft->setTextSize(1);
    tft->setTextColor(t.fgDim);
    tft->drawString("Loading notes...", W / 2, 110);
    tft->setTextDatum(TL_DATUM);

    int got = fetchNotionNotes(_items, MAX_ITEMS, NotesSource::token(),
                                NotesSource::pageId(), NotesSource::showChecked());
    if (got >= 0) { _count = got; _lastError = 0; }
    else          { _count = 0;   _lastError = got; }

    _fetched   = true;
    _lastFetch = millis();
    if (_sel >= _count) _sel = _count > 0 ? _count - 1 : 0;
    _scroll = 0;
    _needsRedraw = true;
}

void NotesScreen::onConfigChanged() {
    _fetched   = false;   // update() refetches on the next tick
    _lastFetch = 0;
    _sel = 0; _scroll = 0;
    _needsRedraw = true;
}

void NotesScreen::update() {
    if (!NotesSource::usable()) return;   // nothing configured to fetch
    if (WiFi.status() != WL_CONNECTED) return;

    bool stale = !_fetched
               || (_lastError != 0 && millis() - _lastFetch > 120000UL) // retry sooner after a failure
               || (millis() - _lastFetch > REFRESH_MS);
    if (stale) doFetch();
}

// ---- drawing ----
void NotesScreen::emptyState(const char* l1, const char* l2) {
    const Theme &t = ThemeManager::current();
    tft->setTextDatum(MC_DATUM);
    tft->setTextSize(2);
    tft->setTextColor(t.fg, t.bg);
    tft->drawString(l1, tft->width() / 2, 100);
    tft->setTextSize(1);
    tft->setTextColor(t.fgDim, t.bg);
    tft->drawString(l2, tft->width() / 2, 124);
    tft->setTextDatum(TL_DATUM);
}

void NotesScreen::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;

    const Theme &t = ThemeManager::current();
    const int W = tft->width();
    tft->fillRect(0, 20, W, tft->height() - 50, t.bg);   // body only; header + legend are drawn elsewhere

    if (!NotesSource::usable()) {
        emptyState("No notes linked", "add a Notion page during setup");
        return;
    }
    if (!_fetched) return;   // update() will fetch + redraw

    if (_lastError == -2) { emptyState("Can't access page", "check the token & sharing"); return; }
    if (_lastError == -3) { emptyState("Page not found", "check the page link"); return; }
    if (_lastError != 0 && _count == 0) { emptyState("Couldn't load notes", "check the connection"); return; }
    if (_count == 0) { emptyState("Nothing here yet", "add some items to the page"); return; }

    drawList();
}

void NotesScreen::drawList() {
    const Theme &t = ThemeManager::current();
    const int W = tft->width();
    const int rowX = 6, rowW = W - 12;

    tft->setTextDatum(TL_DATUM);

    for (int i = 0; i < VISIBLE_ROWS; i++) {
        int idx = _scroll + i;
        if (idx >= _count) break;

        const NoteItem &n = _items[idx];
        int y = LIST_TOP + i * ROW_H;
        bool sel = (idx == _sel);

        uint16_t bg = sel ? t.selectBg : t.bg;
        uint16_t fg = sel ? t.selectFg : t.fg;

        tft->fillRect(rowX, y, rowW, ROW_H - 2, bg);
        if (sel) tft->fillRect(rowX, y, 4, ROW_H - 2, t.accent);

        // marker: a checkbox for a to-do (ticked if already done), a
        // small dot for every other block type this screen shows.
        int markX = rowX + 12, markY = y + (ROW_H - 2) / 2;
        if (n.isTodo) {
            uint16_t boxCol = n.checked ? t.fgDim : fg;
            tft->drawRect(markX - 5, markY - 5, 10, 10, boxCol);
            if (n.checked) {
                tft->drawLine(markX - 3, markY,     markX - 1, markY + 3, t.fgDim);
                tft->drawLine(markX - 1, markY + 3, markX + 4, markY - 4, t.fgDim);
            }
        } else {
            tft->fillCircle(markX, markY, 2, t.accent);
        }

        const int textX = rowX + 26;
        const int maxW  = rowW - 26 - 4;
        tft->setTextSize(2);
        uint16_t textCol = (n.isTodo && n.checked) ? t.fgDim : fg;
        tft->setTextColor(textCol, bg);
        String text = n.text[0] ? n.text : "(empty)";
        while (text.length() > 1 && tft->textWidth(text) > maxW)
            text.remove(text.length() - 1);
        tft->setCursor(textX, y + (ROW_H - 2 - 16) / 2);
        tft->print(text);
    }

    // scroll indicator
    if (_count > VISIBLE_ROWS) {
        int trackH = VISIBLE_ROWS * ROW_H - 6;
        int th = max(8, trackH * VISIBLE_ROWS / _count);
        int ty = LIST_TOP + (trackH - th) * _scroll / (_count - VISIBLE_ROWS);
        tft->fillRect(W - 3, LIST_TOP, 2, trackH, t.bg);
        tft->fillRect(W - 3, ty, 2, th, t.rule);
    }
}

// ---- input ----
void NotesScreen::onEncoderUp() {
    if (_count == 0) return;
    if (_sel > 0) _sel--;
    if (_sel < _scroll) _scroll = _sel;
    _needsRedraw = true;
}

void NotesScreen::onEncoderDown() {
    if (_count == 0) return;
    if (_sel < _count - 1) _sel++;
    if (_sel >= _scroll + VISIBLE_ROWS) _scroll = _sel - VISIBLE_ROWS + 1;
    _needsRedraw = true;
}

// No detail view to toggle into (a note line is already the whole content)
// - the button instead forces an immediate refetch rather than waiting out
// REFRESH_MS, the same way pulling to refresh works elsewhere.
void NotesScreen::onButtonPress() {
    if (!NotesSource::usable()) return;
    _fetched = false;
    _needsRedraw = true;
}

void NotesScreen::getActionLegend(String &line1, String &line2) const {
    line1 = (_count > VISIBLE_ROWS) ? "^v SCROLL NOTES" : "";
    line2 = "o REFRESH";
}
