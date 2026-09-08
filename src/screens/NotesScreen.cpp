#include "NotesScreen.h"
#include "../config/NotesSource.h"
#include "../ui/Theme.h"
#include <WiFi.h>
#include <string.h>

NotesScreen::NotesScreen(TFT_eSPI* display)
    : Screen(display), _count(0), _visibleCount(0), _sel(0), _scroll(0),
      _todoTotal(0), _todoChecked(0),
      _fetched(false), _lastError(0), _lastFetch(0), _needsRedraw(true),
      _marqueeTick(0), _marqueeNextTick(0) {
    memset(_open, 0, sizeof(_open));
}

void NotesScreen::init() {
    _needsRedraw = true;
}

// How many rows fit below listTop() before running into the action-legend
// strip - computed rather than a fixed constant because listTop() itself
// moves down when the progress row is showing, and this has to stay in
// step with it or the last row would draw underneath the legend text.
int NotesScreen::visibleRows() const {
    int bodyBottom = 20 + (tft->height() - 50);   // matches the fillRect() below
    int rows = (bodyBottom - listTop()) / ROW_H;
    return rows > 0 ? rows : 1;
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
                                NotesSource::pageId(), NotesSource::showChecked(),
                                &_todoTotal, &_todoChecked);
    if (got >= 0) { _count = got; _lastError = 0; }
    else          { _count = 0;   _lastError = got; }

    // Fresh fetch, fresh accordion state - everything closed, then (if
    // the setting's on) the first day on the page opens itself.
    memset(_open, 0, sizeof(_open));
    if (NotesSource::groupByDay()) openFirstDay();
    rebuildVisible();

    _fetched   = true;
    _lastFetch = millis();
    _sel    = 0;
    _scroll = 0;
    _marqueeTick = 0;
    _needsRedraw = true;
}

// Builds _visible[]/_visibleCount from _items[] + _open[]: everything
// above the first heading, every heading itself, and a heading's children
// only while that heading is open (or always, when day-grouping is off -
// same list, same order, as the old flat screen had).
void NotesScreen::rebuildVisible() {
    _visibleCount = 0;
    bool open = true;   // items above the first heading are always shown
    for (int i = 0; i < _count && _visibleCount < MAX_ITEMS; i++) {
        const NoteItem &it = _items[i];
        if (it.isHeading) {
            _visible[_visibleCount++] = i;
            open = !NotesSource::groupByDay() || _open[i];
            continue;
        }
        if (open && _visibleCount < MAX_ITEMS) _visible[_visibleCount++] = i;
    }
}

// Opens the very first heading on the page, if there is one - the
// simplest, most predictable starting state (day-name matching against
// "today" turned out to be more surprising than helpful: which day ends
// up open depends on the exact wording of your headings and the device's
// clock being synced, and whichever day happens to be both "today" and
// last on the page just looks like a bug). Leaves everything closed if
// the page has no headings at all.
void NotesScreen::openFirstDay() {
    for (int i = 0; i < _count; i++) {
        if (_items[i].isHeading) { _open[i] = true; return; }
    }
}

// Counts the to_do children directly under one heading (up to the next
// heading or the end of the list) - used for the "(2/3)" done-count shown
// on a closed day. Independent of _open[]/_visible[] on purpose: a
// closed day's count still needs to reflect ALL of its to-dos, not just
// what would currently be drawn if it were open.
void NotesScreen::dayTally(int headingIdx, int &total, int &checked) const {
    total = 0; checked = 0;
    for (int i = headingIdx + 1; i < _count; i++) {
        const NoteItem &it = _items[i];
        if (it.isHeading) break;
        if (it.isTodo) { total++; if (it.checked) checked++; }
    }
}

int NotesScreen::selectedItemIndex() const {
    if (_sel < 0 || _sel >= _visibleCount) return -1;
    return _visible[_sel];
}

// Opens the given heading and closes every other one (accordion: only one
// day open at a time), or just closes it if it was already open. Keeps
// the selection on that same heading row afterward, scrolling just enough
// to keep it on screen.
void NotesScreen::toggleDay(int headingIdx) {
    bool wasOpen = _open[headingIdx];
    for (int i = 0; i < _count; i++) if (_items[i].isHeading) _open[i] = false;
    _open[headingIdx] = !wasOpen;

    rebuildVisible();

    for (int i = 0; i < _visibleCount; i++) {
        if (_visible[i] == headingIdx) { _sel = i; break; }
    }
    int rows = visibleRows();
    if (_sel < _scroll) _scroll = _sel;
    if (_sel >= _scroll + rows) _scroll = _sel - rows + 1;
    if (_scroll < 0) _scroll = 0;
    _marqueeTick = 0;
    _needsRedraw = true;
}

// Flips one to-do's checked state. Optimistic: the box and text update
// immediately (via a direct draw() call, same "show it, then do the slow
// part" order doFetch() already uses for its own loading message) and
// only the Notion sync happens in the background of that - if it fails,
// the change is quietly reverted rather than surfaced as an error state.
void NotesScreen::toggleCheck(int idx) {
    NoteItem &n = _items[idx];
    bool newChecked = !n.checked;

    n.checked = newChecked;
    if (newChecked) _todoChecked++;
    else if (_todoChecked > 0) _todoChecked--;
    _needsRedraw = true;
    draw();

    bool ok = setNotionTodoChecked(NotesSource::token(), n.id, newChecked);
    if (!ok) {
        Serial.println("[Notes] checkbox sync failed, reverting");
        n.checked = !newChecked;
        if (newChecked) { if (_todoChecked > 0) _todoChecked--; }
        else _todoChecked++;
        _needsRedraw = true;
    }
}

void NotesScreen::onConfigChanged() {
    _fetched   = false;   // update() refetches on the next tick
    _lastFetch = 0;
    _sel = 0; _scroll = 0;
    _marqueeTick = 0;
    _needsRedraw = true;
}

void NotesScreen::update() {
    if (NotesSource::usable() && WiFi.status() == WL_CONNECTED) {
        bool stale = !_fetched
                   || (_lastError != 0 && millis() - _lastFetch > 120000UL) // retry sooner after a failure
                   || (millis() - _lastFetch > REFRESH_MS);
        if (stale) doFetch();
    }

    // Paces the selected row's text scroll independently of any fetch -
    // only runs once there's actually a list on screen to scroll. Redraws
    // just that one row (tickMarqueeRow(), not _needsRedraw/draw()) so a
    // scrolling row doesn't repaint the whole list several times a second.
    if (_fetched && _visibleCount > 0) {
        unsigned long now = millis();
        if (now >= _marqueeNextTick) {
            _marqueeNextTick = now + MARQUEE_TICK_MS;
            _marqueeTick++;
            tickMarqueeRow();
        }
    }
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

    if (_todoTotal > 0) drawProgress();
    drawList();
}

// A single "N/M done" row pinned under the header, only when the page
// actually has to-dos on it - a page of plain notes gets no progress row
// at all, rather than an empty "0/0" that means nothing.
void NotesScreen::drawProgress() {
    const Theme &t = ThemeManager::current();
    const int W = tft->width();
    const int y = 24;

    tft->setTextSize(1);
    tft->setTextColor(t.fgDim, t.bg);
    tft->setCursor(6, y);
    tft->print("TO-DO");

    char counter[16];
    snprintf(counter, sizeof(counter), "%d/%d done", _todoChecked, _todoTotal);
    tft->setTextDatum(TR_DATUM);
    tft->setTextColor(_todoChecked == _todoTotal ? t.accent2 : t.fgDim, t.bg);
    tft->drawString(counter, W - 6, y);
    tft->setTextDatum(TL_DATUM);

    // A thin filled bar underneath does the same job as the text but reads
    // at a glance - proportional width, floor of a couple pixels so a
    // freshly-started list (0 done) still shows a sliver rather than
    // nothing at all.
    const int barX = 6, barY = y + 12, barW = W - 12, barH = 3;
    tft->fillRect(barX, barY, barW, barH, t.rule);
    int fillW = _todoTotal > 0 ? (barW * _todoChecked) / _todoTotal : 0;
    if (fillW > 0) tft->fillRect(barX, barY, fillW, barH, t.accent2);
}

// Text to actually print for one row, given the full string and the pixel
// width it has to fit in - see the header's doc comment for the animated
// case's design. Shared by both the heading and the plain-row branches of
// drawList() below.
String NotesScreen::layoutRowText(const String &full, int maxW, bool animate) const {
    if (tft->textWidth(full) <= maxW) return full;

    if (!animate) {
        String t = full;
        while (t.length() > 1 && tft->textWidth(t) > maxW) t.remove(t.length() - 1);
        return t;
    }

    // Find how far the window can slide before the tail end of the string
    // fills maxW exactly (right-anchored) - that's the far end of the
    // scroll. Trimming from the front of a probe copy is the same
    // technique the non-animated branch uses trimming from the back.
    String probe = full;
    int maxStart = 0;
    while (probe.length() > 1 && tft->textWidth(probe) > maxW) {
        probe.remove(0, 1);
        maxStart++;
    }

    int cycle = maxStart + MARQUEE_PAUSE_TICKS * 2;
    int phase = (cycle > 0) ? (_marqueeTick % cycle) : 0;

    int startChar;
    if (phase < MARQUEE_PAUSE_TICKS) {
        startChar = 0;                                   // pause at the beginning
    } else if (phase < MARQUEE_PAUSE_TICKS + maxStart) {
        startChar = phase - MARQUEE_PAUSE_TICKS;          // scrolling
    } else {
        startChar = maxStart;                             // pause at the end
    }

    String t = full.substring(startChar);
    while (t.length() > 1 && tft->textWidth(t) > maxW) t.remove(t.length() - 1);
    return t;
}

// Redraws just the selected row's text - see the header's doc comment.
// Mirrors drawList()'s own per-row layout exactly (same maxW math, same
// text) but touches only the text region of that one row, so it never
// disturbs the marker/checkbox, the done-count, or the divider rule line
// next to it - those don't change between ticks, only the scrolled text
// does.
void NotesScreen::tickMarqueeRow() {
    int rowOnScreen = _sel - _scroll;
    if (rowOnScreen < 0 || rowOnScreen >= visibleRows()) return;
    int idx = selectedItemIndex();
    if (idx < 0) return;

    const Theme &t = ThemeManager::current();
    const int W = tft->width();
    const int rowX = 6, rowW = W - 12;
    const int y = listTop() + rowOnScreen * ROW_H;
    const bool grouped = NotesSource::groupByDay();
    const NoteItem &n = _items[idx];

    tft->setTextDatum(TL_DATUM);
    tft->setTextSize(2);

    if (n.isHeading) {
        int markW = 0;
        if (grouped) {
            char mark[12] = "";
            int total = 0, checked = 0;
            dayTally(idx, total, checked);
            if (total > 0) {
                snprintf(mark, sizeof(mark), "(%d/%d)", checked, total);
                tft->setTextSize(1);
                markW = tft->textWidth(mark) + 6;
                tft->setTextSize(2);
            }
        }
        const int textX = rowX + 8;
        const int maxW  = rowW - 8 - 4 - markW;
        String text = n.text[0] ? n.text : "(empty)";
        if (grouped) text = String(_open[idx] ? "v " : "> ") + text;
        if (tft->textWidth(text) <= maxW) return;   // fits - nothing to animate

        text = layoutRowText(text, maxW, true);
        tft->fillRect(textX, y, maxW, ROW_H - 3, t.selectBg);   // stop short of the divider rule
        tft->setTextColor(t.selectFg, t.selectBg);
        tft->setCursor(textX, y + (ROW_H - 2 - 16) / 2);
        tft->print(text);
        return;
    }

    const int textX = rowX + 26;
    const int maxW  = rowW - 26 - 4;
    String text = n.text[0] ? n.text : "(empty)";
    if (tft->textWidth(text) <= maxW) return;   // fits - nothing to animate

    text = layoutRowText(text, maxW, true);
    uint16_t textCol = (n.isTodo && n.checked) ? t.fgDim : t.selectFg;
    tft->fillRect(textX, y, maxW, ROW_H - 2, t.selectBg);
    tft->setTextColor(textCol, t.selectBg);
    tft->setCursor(textX, y + (ROW_H - 2 - 16) / 2);
    tft->print(text);
}

void NotesScreen::drawList() {
    const Theme &t = ThemeManager::current();
    const int W = tft->width();
    const int rowX = 6, rowW = W - 12;
    const int top  = listTop();
    const int rows = visibleRows();
    const bool grouped = NotesSource::groupByDay();

    tft->setTextDatum(TL_DATUM);

    for (int i = 0; i < rows; i++) {
        int visIdx = _scroll + i;
        if (visIdx >= _visibleCount) break;
        int idx = _visible[visIdx];

        const NoteItem &n = _items[idx];
        int y = top + i * ROW_H;
        bool sel = (visIdx == _sel);

        uint16_t bg = sel ? t.selectBg : t.bg;
        uint16_t fg = sel ? t.selectFg : t.fg;

        tft->fillRect(rowX, y, rowW, ROW_H - 2, bg);
        if (sel) tft->fillRect(rowX, y, 4, ROW_H - 2, t.accent);

        if (n.isHeading) {
            // A closed day's own done-count ("(2/3)"), right-aligned,
            // drawn first so its width can be reserved from the heading
            // text's own truncation budget below. Only shown at all when
            // day-grouping is on and that day actually has to-dos.
            char mark[12] = "";
            bool markDone = false;
            int markW = 0;
            if (grouped) {
                int total = 0, checked = 0;
                dayTally(idx, total, checked);
                if (total > 0) {
                    snprintf(mark, sizeof(mark), "(%d/%d)", checked, total);
                    markDone = (checked == total);
                }
            }
            if (mark[0]) {
                tft->setTextSize(1);
                markW = tft->textWidth(mark) + 6;
                tft->setTextDatum(TR_DATUM);
                tft->setTextColor(sel ? t.selectFg : (markDone ? t.accent2 : t.fgDim), bg);
                tft->drawString(mark, rowX + rowW - 6, y + (ROW_H - 2 - 8) / 2);
                tft->setTextDatum(TL_DATUM);
            }

            // Section divider: a leading caret when day-grouping is on
            // (">" closed, "v" open - same ASCII convention as the
            // "^v SCROLL" legend hint), accent-colored text, with a thin
            // rule along the bottom of the row so it reads as a break in
            // the list rather than just another item.
            const int textX = rowX + 8;
            const int maxW  = rowW - 8 - 4 - markW;
            tft->setTextSize(2);
            tft->setTextColor(sel ? t.selectFg : t.accent, bg);
            String text = n.text[0] ? n.text : "(empty)";
            if (grouped) text = String(_open[idx] ? "v " : "> ") + text;
            text = layoutRowText(text, maxW, sel);
            tft->setCursor(textX, y + (ROW_H - 2 - 16) / 2);
            tft->print(text);
            tft->fillRect(rowX, y + ROW_H - 3, rowW, 1, sel ? t.selectFg : t.rule);
            continue;
        }

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
        text = layoutRowText(text, maxW, sel);
        tft->setCursor(textX, y + (ROW_H - 2 - 16) / 2);
        tft->print(text);
    }

    // scroll indicator
    if (_visibleCount > rows) {
        int trackH = rows * ROW_H - 6;
        int th = max(8, trackH * rows / _visibleCount);
        int ty = top + (trackH - th) * _scroll / (_visibleCount - rows);
        tft->fillRect(W - 3, top, 2, trackH, t.bg);
        tft->fillRect(W - 3, ty, 2, th, t.rule);
    }
}

// ---- input ----
void NotesScreen::onEncoderUp() {
    if (_visibleCount == 0) return;
    if (_sel > 0) _sel--;
    if (_sel < _scroll) _scroll = _sel;
    _marqueeTick = 0;
    _needsRedraw = true;
}

void NotesScreen::onEncoderDown() {
    if (_visibleCount == 0) return;
    int rows = visibleRows();
    if (_sel < _visibleCount - 1) _sel++;
    if (_sel >= _scroll + rows) _scroll = _sel - rows + 1;
    _marqueeTick = 0;
    _needsRedraw = true;
}

// Context-sensitive: KO opens/closes a day when a heading is selected
// (only meaningful with day-grouping on), ticks/unticks a to-do when one
// of those is selected, and otherwise falls back to forcing an immediate
// refetch - the same "don't wait for REFRESH_MS" behavior this always had.
void NotesScreen::onButtonPress() {
    if (!NotesSource::usable()) return;

    int idx = selectedItemIndex();
    if (idx >= 0) {
        const NoteItem &n = _items[idx];
        if (NotesSource::groupByDay() && n.isHeading) { toggleDay(idx); return; }
        if (n.isTodo) { toggleCheck(idx); return; }
    }

    _fetched = false;
    _needsRedraw = true;
}

void NotesScreen::getActionLegend(String &line1, String &line2) const {
    line1 = (_visibleCount > visibleRows()) ? "^v SCROLL NOTES" : "";

    int idx = selectedItemIndex();
    if (idx >= 0) {
        const NoteItem &n = _items[idx];
        if (NotesSource::groupByDay() && n.isHeading) {
            line2 = _open[idx] ? "o CLOSE DAY" : "o OPEN DAY";
            return;
        }
        if (n.isTodo) {
            line2 = n.checked ? "o UNTICK" : "o TICK";
            return;
        }
    }
    line2 = "o REFRESH";
}
