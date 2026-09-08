#pragma once
#include "Screen.h"
#include "../api/notes_api.h"

// A short list pulled from one Notion page (see config/NotesSource.h for
// how it's configured): to-do checkboxes, bullet and numbered list items,
// paragraphs and headings, in the order they sit on the page. Turn the
// encoder to scroll past MAX_ITEMS rows; a selected row whose text doesn't
// fit slowly scrolls through the whole thing instead of just sitting cut
// off (see layoutRowText()) - animated by update() on a timer, but via
// tickMarqueeRow() redrawing just that one row, not a full-screen redraw
// several times a second.
//
// The KO button is context-sensitive, matching whatever's selected (see
// getActionLegend()): on a to-do it ticks/unticks it - optimistically, and
// written back to Notion in the background (setNotionTodoChecked()); with
// NotesSource::groupByDay() on, a heading is a collapsible day section and
// KO opens/closes it (see rebuildVisible()/toggleDay()), with the first
// heading on the page opened by default on each fetch (openFirstDay()) and
// only one day open at a time; anything else falls back to forcing an
// immediate refresh rather than waiting for the next scheduled one.
class NotesScreen : public Screen {
public:
    NotesScreen(TFT_eSPI* display);

    void init() override;
    void update() override;
    void draw() override;
    void onEncoderUp() override;
    void onEncoderDown() override;
    void onButtonPress() override;
    const char* getName() const override { return "Notes"; }
    ScreenId id() const override { return ScreenId::Notes; }
    void getActionLegend(String &line1, String &line2) const override;

    // The token, page, item cap or checked-item filter may all have just
    // changed - stale until the next fetch either way.
    void onConfigChanged() override;

private:
    static const int MAX_ITEMS    = 15;
    static const int ROW_H        = 24;
    static const int LIST_TOP     = 26;      // no progress row
    static const int LIST_TOP_PROGRESS = 40; // progress row showing, list starts lower
    static const unsigned long REFRESH_MS = 15UL * 60UL * 1000UL;

    NoteItem _items[MAX_ITEMS];
    int  _count;

    // Which headings are expanded - only meaningful when
    // NotesSource::groupByDay() is on; indexed the same as _items[].
    bool _open[MAX_ITEMS];

    // The rows actually on screen right now: a flattened, filtered view of
    // _items[] built by rebuildVisible() - a closed heading contributes
    // only itself, an open one also contributes everything under it until
    // the next heading. Everything before the first heading is always
    // included. _sel/_scroll are indices into THIS list, not into _items[]
    // directly - selectedItemIndex() converts one to the other. When
    // groupByDay() is off this ends up identical to _items[] in order, so
    // the plain flat-list behavior is unchanged.
    int _visible[MAX_ITEMS];
    int _visibleCount;

    int  _sel;
    int  _scroll;

    // Tallied across every to_do block the last fetch saw, including any
    // dropped from _items by the "show checked items" setting - see
    // notes_api.h. _todoTotal == 0 means no to-dos on the page at all, the
    // signal drawList() uses to skip the progress row entirely.
    int _todoTotal;
    int _todoChecked;

    bool _fetched;
    int  _lastError;      // 0 = ok, else fetchNotionNotes()'s negative code
    unsigned long _lastFetch;
    bool _needsRedraw;

    // Drives the selected-row text scroll in drawList()/layoutRowText() -
    // a free-running counter, not tied to which row is selected, so
    // switching selection needs no special-case bookkeeping beyond
    // resetting it to 0 for a clean start (done wherever _sel changes).
    static const unsigned long MARQUEE_TICK_MS = 300;   // ms per animation step
    static const int MARQUEE_PAUSE_TICKS       = 4;     // pause at each end, in ticks
    int _marqueeTick;
    unsigned long _marqueeNextTick;

    int listTop() const { return _todoTotal > 0 ? LIST_TOP_PROGRESS : LIST_TOP; }
    int visibleRows() const;   // depends on listTop() and the panel's actual height

    void doFetch();
    void drawProgress();
    void drawList();
    void emptyState(const char* line1, const char* line2);

    // Text to actually print for one row, given the full string and the
    // pixel width it has to fit in: `full` unchanged if it already fits;
    // otherwise, for `animate==false`, the plain from-the-end truncation
    // every non-selected row gets (no ellipsis, just cut off); for
    // `animate==true` (the selected row only), a window that starts at the
    // beginning, pauses, scrolls one character at a time until the tail is
    // reached, pauses there too, then jumps back to the start - paced by
    // _marqueeTick, which update() advances on a timer.
    String layoutRowText(const String &full, int maxW, bool animate) const;

    // Redraws just the currently selected row - called from update() every
    // MARQUEE_TICK_MS instead of setting _needsRedraw, so a scrolling row's
    // animation doesn't repaint the whole list (and everything else on it)
    // several times a second. A no-op whenever there's nothing to animate:
    // nothing selected, the selection has scrolled off screen, or that
    // row's text already fits without scrolling.
    void tickMarqueeRow();

    // Day-accordion + checkbox support. All operate on _items[] (which
    // always holds everything the last fetch returned, open or not) and
    // keep _visible[]/_sel/_scroll in sync with whatever they change.
    void rebuildVisible();
    void openFirstDay();                                    // opens the page's first heading, if any
    void dayTally(int headingIdx, int &total, int &checked) const;
    void toggleDay(int headingIdx);
    void toggleCheck(int idx);
    int  selectedItemIndex() const;   // _visible[_sel], or -1 if nothing's selected
};
