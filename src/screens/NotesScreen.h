#pragma once
#include "Screen.h"
#include "../api/notes_api.h"

// A short list pulled from one Notion page (see config/NotesSource.h for
// how it's configured): to-do checkboxes, bullet and numbered list items,
// paragraphs and headings, in the order they sit on the page. Turn the
// encoder to scroll past MAX_ITEMS rows.
//
// The KO button is context-sensitive, matching whatever's selected (see
// getActionLegend()): on a to-do it ticks/unticks it - optimistically, and
// written back to Notion in the background (setNotionTodoChecked()); with
// NotesSource::groupByDay() on, a heading is a collapsible day section and
// KO opens/closes it (see rebuildVisible()/toggleDay()), with only one day
// open at a time and today's opened automatically on each fetch
// (autoOpenToday()); anything else falls back to forcing an immediate
// refresh rather than waiting for the next scheduled one.
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

    int listTop() const { return _todoTotal > 0 ? LIST_TOP_PROGRESS : LIST_TOP; }
    int visibleRows() const;   // depends on listTop() and the panel's actual height

    void doFetch();
    void drawProgress();
    void drawList();
    void emptyState(const char* line1, const char* line2);

    // Day-accordion + checkbox support. All operate on _items[] (which
    // always holds everything the last fetch returned, open or not) and
    // keep _visible[]/_sel/_scroll in sync with whatever they change.
    void rebuildVisible();
    void autoOpenToday();                                   // sets one _open[] bit
    void dayTally(int headingIdx, int &total, int &checked) const;
    void toggleDay(int headingIdx);
    void toggleCheck(int idx);
    int  selectedItemIndex() const;   // _visible[_sel], or -1 if nothing's selected
};
