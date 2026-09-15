#include "StockScreen.h"
#include "../config/StockSource.h"
#include "../ui/Theme.h"
#include <WiFi.h>

StockScreen::StockScreen(TFT_eSPI* display)
    : Screen(display), _count(0), _sel(0), _scroll(0),
      _fetched(false), _fetchFailed(false), _lastFetch(0), _needsRedraw(true),
      _pendingCount(0), _pendingAnyOk(false), _pendingDone(false) {
    _fetchMutex = xSemaphoreCreateMutex();
}

void StockScreen::init() {
    _needsRedraw = true;
}

// ---- data ----
// Kicks off the fetch on its own short-lived FreeRTOS task (see
// CalendarScreen::startFetch() for the rationale) - one HTTP round-trip per
// configured symbol, which used to run right here on the UI thread and
// would otherwise block loop()/input for as long as every symbol took to
// answer or time out.
void StockScreen::startFetch() {
    if (_fetching) return; // one at a time
    _fetching = true;
    _needsRedraw = true; // so draw() can show the loading hint
    xTaskCreatePinnedToCore(fetchTaskEntry, "stockFetch", 12288, this, 1, nullptr, 0);
}

void StockScreen::fetchTaskEntry(void *param) {
    StockScreen *self = static_cast<StockScreen*>(param);
    int n = StockSource::symbolCount();
    if (n > StockSource::MAX_SYMBOLS) n = StockSource::MAX_SYMBOLS;
    bool anyOk = false;
    StockQuote results[StockSource::MAX_SYMBOLS];

    for (int i = 0; i < n; i++) {
        String sym = StockSource::symbolAt(i);
        anyOk |= fetchStockQuote(sym, StockSource::apiKey(), results[i]);

        // A beat between symbols, not just back-to-back: this is what
        // actually fixed the "SSL - Memory allocation failed" errors -
        // firing five HTTPS handshakes in a tight loop left no room for
        // other background tasks' own TLS connections (flight/quote/
        // weather/etc. all do this periodically too) to finish releasing
        // their heap before this one grabbed for more.
        if (i < n - 1) vTaskDelay(pdMS_TO_TICKS(800));
    }

    // Publish the whole batch in one go, like CalendarScreen - the list
    // swaps from skeletons (first load) or the previous prices (refresh)
    // to the new set at once, instead of rows popping in one by one.
    // Runs for n == 0 too, since it's what clears _fetching.
    if (xSemaphoreTake(self->_fetchMutex, portMAX_DELAY) == pdTRUE) {
        for (int i = 0; i < n; i++) self->_pendingQuotes[i] = results[i];
        self->_pendingCount = n;
        self->_pendingAnyOk = anyOk;
        self->_pendingDone  = true;
        self->_pendingReady = n > 0;
        self->_fetching     = false;
        xSemaphoreGive(self->_fetchMutex);
    }

    vTaskDelete(nullptr);
}

// Runs on the UI thread (called from update()) once the background task has
// a result waiting - see CalendarScreen::applyPending() for why this copy
// has to happen here and not in the task itself. Called every loop tick
// while a fetch is in progress, not just once at the end, so each symbol's
// row updates the moment its own result lands (see fetchTaskEntry()).
void StockScreen::applyPending() {
    if (xSemaphoreTake(_fetchMutex, 0) != pdTRUE) return; // task mid-write, try again next loop
    if (!_pendingReady) { xSemaphoreGive(_fetchMutex); return; }

    for (int i = 0; i < _pendingCount; i++) _quotes[i] = _pendingQuotes[i];
    _count = _pendingCount;
    // Only a real "every symbol failed" verdict once the whole batch has
    // actually been tried - otherwise the first symbol failing alone would
    // flash the "Couldn't load stocks" empty state before the rest even had
    // a turn.
    _fetchFailed = _pendingDone && !_pendingAnyOk && _pendingCount > 0;

    _pendingReady = false;
    xSemaphoreGive(_fetchMutex);

    _fetched   = true;
    _lastFetch = millis();
    if (_sel >= _count) _sel = _count > 0 ? _count - 1 : 0;
    if (_scroll > _count) _scroll = 0;
    _needsRedraw = true;
}

void StockScreen::onConfigChanged() {
    _fetched   = false;   // update() refetches on the next tick
    _lastFetch = 0;
    _count = 0;           // old symbols' prices would sit under new tickers until the batch lands
    _sel = 0; _scroll = 0;
    _needsRedraw = true;
}

void StockScreen::update() {
    if (_pendingReady) applyPending();

    if (!StockSource::usable()) return;   // nothing to fetch
    if (WiFi.status() != WL_CONNECTED) return;

    unsigned long refreshMs = (unsigned long)StockSource::refreshMinutes() * 60UL * 1000UL;
    bool stale = !_fetched
               || (_fetchFailed && millis() - _lastFetch > 120000UL) // retry sooner after a failure
               || (millis() - _lastFetch > refreshMs);
    if (stale) startFetch();
}

// ---- drawing ----
void StockScreen::emptyState(const char* l1, const char* l2) {
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

void StockScreen::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;

    const Theme &t = ThemeManager::current();
    const int W = tft->width();
    tft->fillRect(0, 20, W, tft->height() - 50, t.bg);   // body only; header + legend are drawn elsewhere

    if (!StockSource::usable()) {
        emptyState("No stocks linked", "add a ticker during setup");
        return;
    }
    if (_fetchFailed) {
        emptyState("Couldn't load stocks", "check your API key");
        return;
    }

    // Every row from here on is either a real quote (arrived) or a
    // skeleton placeholder (still on its way) - see drawList(), which never
    // needs a separate "still loading" empty state now that it fills the
    // gap itself.
    drawList();
}

void StockScreen::drawList() {
    const Theme &t = ThemeManager::current();
    const int W = tft->width();
    const int rowX = 6, rowW = W - 12;
    const int total = StockSource::symbolCount(); // configured, not yet-arrived

    tft->setTextDatum(TL_DATUM);

    for (int i = 0; i < VISIBLE_ROWS; i++) {
        int idx = _scroll + i;
        if (idx >= total) break;

        if (idx >= _count) {
            // Configured but its own fetch hasn't landed yet - skeleton
            // instead of leaving the row blank (tickSkeleton() animates the
            // scan line across whichever of these are on screen).
            drawSkeletonRow(LIST_TOP + i * ROW_H);
            continue;
        }

        const StockQuote &q = _quotes[idx];
        int y = LIST_TOP + i * ROW_H;
        bool sel = (idx == _sel);

        tft->fillRect(rowX, y, rowW, ROW_H - 4, sel ? t.selectBg : t.bg);
        if (sel) tft->fillRect(rowX, y, 4, ROW_H - 4, t.selectMark);

        uint16_t fg = sel ? t.selectFg : t.fg;
        uint16_t fgDim = sel ? t.selectFg : t.fgDim;

        // Symbol, top-left.
        tft->setTextSize(2);
        tft->setTextColor(fg, sel ? t.selectBg : t.bg);
        tft->setCursor(rowX + 10, y + 4);
        tft->print(q.symbol);

        if (!q.ok) {
            // "not found" is Finnhub confirming the ticker itself doesn't
            // exist (a typo, most likely) - "unavailable" is everything
            // else (network hiccup, bad key, timeout), which might clear up
            // on its own next refresh, so the two aren't worded the same.
            tft->setTextSize(1);
            tft->setTextColor(fgDim, sel ? t.selectBg : t.bg);
            tft->setTextDatum(TR_DATUM);
            tft->drawString(q.notFound ? "not found" : "unavailable", rowX + rowW - 8, y + 12);
            tft->setTextDatum(TL_DATUM);
            continue;
        }

        // Price, top-right.
        char priceBuf[16];
        snprintf(priceBuf, sizeof(priceBuf), "$%.2f", q.price);
        tft->setTextSize(2);
        tft->setTextColor(fg, sel ? t.selectBg : t.bg);
        tft->setTextDatum(TR_DATUM);
        tft->drawString(priceBuf, rowX + rowW - 8, y + 4);
        tft->setTextDatum(TL_DATUM);

        // % change, bottom-right - green/up or red/down, same read-at-a-
        // glance convention as every finance app. accent2/danger are this
        // app's existing "good"/"bad" roles (see Theme.h), not new colors.
        char pctBuf[16];
        snprintf(pctBuf, sizeof(pctBuf), "%s%.2f%%", q.changePercent >= 0 ? "+" : "", q.changePercent);
        uint16_t pctColor = sel ? t.selectFg : (q.changePercent >= 0 ? t.accent2 : t.danger);
        tft->setTextSize(1);
        tft->setTextColor(pctColor, sel ? t.selectBg : t.bg);
        tft->setTextDatum(TR_DATUM);
        tft->drawString(pctBuf, rowX + rowW - 8, y + 24);
        tft->setTextDatum(TL_DATUM);
    }

    // scroll indicator - sized against every configured symbol, not just
    // the ones that have answered yet, so it doesn't shrink and grow again
    // as results land mid-fetch.
    if (total > VISIBLE_ROWS) {
        int trackH = VISIBLE_ROWS * ROW_H - 6;
        int th = max(8, trackH * VISIBLE_ROWS / total);
        int ty = LIST_TOP + (trackH - th) * _scroll / (total - VISIBLE_ROWS);
        tft->fillRect(W - 3, LIST_TOP, 2, trackH, t.bg);
        tft->fillRect(W - 3, ty, 2, th, t.rule);
    }
}

// ================ LOADING SKELETON ================
// A row whose own symbol hasn't answered yet - dim placeholder bars where
// the symbol/price/change would land - no animation, just a static hint
// that a row is spoken for but not back yet.
void StockScreen::drawSkeletonRow(int y) {
    const Theme &t = ThemeManager::current();
    const int W = tft->width();
    const int rowX = 6, rowW = W - 12;

    tft->fillRect(rowX, y, rowW, ROW_H - 4, ThemeManager::current().bg);
    tft->fillRoundRect(rowX + 10, y + 6, 52, 12, 2, t.rule);           // symbol placeholder
    tft->fillRoundRect(rowX + rowW - 68, y + 6, 60, 12, 2, t.rule);    // price placeholder
    tft->fillRoundRect(rowX + rowW - 44, y + 24, 36, 8, 2, t.rule);    // %change placeholder
}

// ---- input ----
void StockScreen::onEncoderUp() {
    if (_count == 0) return;
    if (_sel > 0) _sel--;
    if (_sel < _scroll) _scroll = _sel;
    _needsRedraw = true;
}

void StockScreen::onEncoderDown() {
    if (_count == 0) return;
    if (_sel < _count - 1) _sel++;
    if (_sel >= _scroll + VISIBLE_ROWS) _scroll = _sel - VISIBLE_ROWS + 1;
    _needsRedraw = true;
}

// No detail view to open - KO just forces an immediate refresh, same
// "don't wait for the interval" behavior NotesScreen's button gives when
// nothing more specific is selected.
void StockScreen::onButtonPress() {
    if (!StockSource::usable()) return;
    _fetched = false;
    _needsRedraw = true;
}

void StockScreen::getActionLegend(String &line1, String &line2) const {
    line1 = (_count > VISIBLE_ROWS) ? "^v SCROLL" : "";
    line2 = "o REFRESH";
}
