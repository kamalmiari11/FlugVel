#pragma once
#include "Screen.h"
#include "../api/notes_api.h"

// A short read-only list pulled from one Notion page (see
// config/NotesSource.h for how it's configured): to-do checkboxes, bullet
// and numbered list items, paragraphs and headings, in the order they sit
// on the page. Turn the encoder to scroll past MAX_ITEMS rows; press the
// button to force an immediate refresh rather than waiting for the next
// scheduled one.
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
    static const int VISIBLE_ROWS = 7;
    static const int ROW_H        = 24;
    static const int LIST_TOP     = 26;
    static const unsigned long REFRESH_MS = 15UL * 60UL * 1000UL;

    NoteItem _items[MAX_ITEMS];
    int  _count;
    int  _sel;
    int  _scroll;

    bool _fetched;
    int  _lastError;      // 0 = ok, else fetchNotionNotes()'s negative code
    unsigned long _lastFetch;
    bool _needsRedraw;

    void doFetch();
    void drawList();
    void emptyState(const char* line1, const char* line2);
};
