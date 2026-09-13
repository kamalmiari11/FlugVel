#pragma once
#include "Screen.h"
#include "../api/stock_api.h"
#include "../config/StockSource.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// A short list of ticker quotes (symbol, price, day change %) from the
// symbols configured in StockSource. Turn the encoder to scroll past
// VISIBLE_ROWS, press to force an immediate refresh - same "don't wait for
// the interval" behavior NotesScreen's button gives.
class StockScreen : public Screen {
public:
    StockScreen(TFT_eSPI* display);

    void init() override;
    void update() override;
    void draw() override;
    void onEncoderUp() override;
    void onEncoderDown() override;
    void onButtonPress() override;
    const char* getName() const override { return "Stocks"; }
    ScreenId id() const override { return ScreenId::Stock; }
    void getActionLegend(String &line1, String &line2) const override;

    // The key, symbol list or refresh interval may have just changed -
    // stale until the next fetch either way.
    void onConfigChanged() override;

private:
    static const int VISIBLE_ROWS = 5;
    static const int ROW_H        = 36;
    static const int LIST_TOP     = 26;

    StockQuote _quotes[StockSource::MAX_SYMBOLS];
    int  _count;
    int  _sel;
    int  _scroll;

    bool _fetched;
    bool _fetchFailed;   // every configured symbol failed
    unsigned long _lastFetch;

    bool _needsRedraw;

    // Fetch runs on its own short-lived FreeRTOS task (same pattern as
    // CalendarScreen/NotesScreen - see CalendarScreen.h for the rationale)
    // so a slow/dead API key never blocks loop()/input. It writes into
    // _pending* below; only update() (UI thread, see applyPending()) ever
    // writes _quotes/_count, since draw() reads those unlocked.
    SemaphoreHandle_t _fetchMutex = nullptr;
    volatile bool _fetching = false;
    volatile bool _pendingReady = false;
    StockQuote _pendingQuotes[StockSource::MAX_SYMBOLS];
    int  _pendingCount;
    bool _pendingAnyOk;
    bool _pendingDone;   // true once every configured symbol has been tried -
                          // see fetchTaskEntry(): results publish one at a
                          // time as they land, so the screen fills in row by
                          // row instead of waiting for the whole batch, and
                          // pacing a beat between each fetch (rather than
                          // firing all of them back-to-back) leaves other
                          // background tasks' own TLS handshakes room to
                          // finish instead of every attempt racing for the
                          // same limited heap at once.
    void startFetch();
    static void fetchTaskEntry(void *param);
    void applyPending();

    void drawList();
    void emptyState(const char* line1, const char* line2);

    // ---- loading skeleton ----
    // Rows for symbols still waiting on their own fetch (idx >= _count but
    // < the configured symbol count) draw as a static dim placeholder
    // instead of just stopping the list short.
    void drawSkeletonRow(int y);
};
