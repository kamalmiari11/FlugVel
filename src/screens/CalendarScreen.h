#pragma once
#include "Screen.h"
#include "../api/calendar_api.h"

class CaptivePortal;

// Agenda screen: the next ~60 days of events from the iCal feed configured
// in setup, or added later from the config page. Several feeds can be
// configured (see config/CalendarFeeds.h); they are fetched in turn and
// merged into one agenda. Turn the encoder to move
// through the list, press to open/close a detail view for one event.
class CalendarScreen : public Screen {
public:
    CalendarScreen(TFT_eSPI* display, CaptivePortal* portal);

    void init() override;
    void update() override;
    void draw() override;
    void onEncoderUp() override;
    void onEncoderDown() override;
    void onButtonPress() override;
    const char* getName() const override { return "Calendar"; }
    ScreenId id() const override { return ScreenId::Calendar; }
    void getActionLegend(String &line1, String &line2) const override;

    // Feeds, look-ahead and the filters all decide what a fetch returns, so
    // a change means the events currently held are stale.
    void onConfigChanged() override;

private:
    static const int MAX_EVENTS  = 20;
    static const int WINDOW_DAYS  = 60;
    static const int VISIBLE_ROWS = 6;
    static const int ROW_H        = 30;
    static const int LIST_TOP     = 26;
    static const unsigned long REFRESH_MS = 30UL * 60UL * 1000UL;

    CaptivePortal* _portal;

    // Collapses the same event arriving from more than one feed into one
    // row, and puts the merged list back in start order (each feed arrives
    // sorted, but concatenating them does not stay sorted). Both return /
    // operate on the new count.
    int  mergeDuplicates(int count);
    void sortByStart(int count);

    CalEvent _events[MAX_EVENTS];
    int  _count;
    int  _sel;
    int  _scroll;
    bool _detail;

    bool _fetched;
    bool _fetchFailed;
    bool _fetchBadUrl;   // URL returned a web page, not an iCal feed
    unsigned long _lastFetch;

    bool _needsRedraw;

    void doFetch();
    void drawList();
    void drawDetail();
    void emptyState(const char* line1, const char* line2);
    String relLabel(const CalEvent &e) const;   // "TODAY" / "TMRW" / "WED 12"
    String timeLabel(const CalEvent &e) const;  // "09:30" or "" for all-day
};
