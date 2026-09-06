#include "DashboardScreen.h"
#include "../config/Config.h"
#include "../MyColors.h"
#include "../ui/Theme.h"
#include "../ui/UiChrome.h"
#include "../ui/Units.h"
#include <time.h>

DashboardScreen::DashboardScreen(TFT_eSPI* display)
    : Screen(display), _flightsDetectedToday(0),
      _timezone("UTC"), _city("Unknown"), _country("Unknown"),
      _lat(0), _lon(0), _hasCoords(false),
      _hasWeatherData(false), _realWeather(false),
      _mode(VIEW_WEEK), _selectedDay(0),
      _hourlyLoaded(false), _hourlyLoadedForDay(-1),
      _hourlyPending(false), _hourlyFetching(false), _hourlyFailed(false),
      _skelLastTick(0), _scanPos(0), _scanDir(1),
      _refreshGeneration(0), _prefetchRunning(false),
      _needsFullRedraw(true), _lastRefresh(0),
      _highlightedHourIndex(-1), _lastHourCheck(0), _hourScroll(0)
{
    for (int i = 0; i < WeatherData::MAX_DAYS; i++) { _hourlyCacheValid[i] = false; _hourlyCacheDate[i] = ""; }
    _cacheMutex = xSemaphoreCreateMutex();
    loadDummyWeather(); // something sensible on screen before the first real fetch
}

void DashboardScreen::init() {
    Serial.println("[DashboardScreen] Weather screen initialized");
    _mode = VIEW_WEEK;
    _hourScroll = 0;
    _needsFullRedraw = true; // force one clean redraw when entering this screen
}

void DashboardScreen::loadDummyWeather() {
    _weather.currentTempC = 18;
    _weather.currentWeatherCode = 2; // "Partly Cloudy"

    _weather.dayCount = 7;
    _weather.days[0] = { "Today", "", 3,  19, 12 };
    _weather.days[1] = { "Tue",   "", 61, 16, 10 };
    _weather.days[2] = { "Wed",   "", 0,  21, 11 };
    _weather.days[3] = { "Thu",   "", 0,  22, 13 };
    _weather.days[4] = { "Fri",   "", 2,  20, 12 };
    _weather.days[5] = { "Sat",   "", 80, 17, 11 };
    _weather.days[6] = { "Sun",   "", 1,  19, 12 };

    _hasWeatherData = true;
}

void DashboardScreen::setLocationCoords(float lat, float lon) {
    _lat = lat;
    _lon = lon;
    _hasCoords = true;
    _lastRefresh = 0; // force an immediate refresh on the next update()
}

void DashboardScreen::refreshWeatherNow() {
    if (!_hasCoords) return;

    Serial.println("[DashboardScreen] Refreshing weather...");
    WeatherData fetched;
    if (fetchWeather(fetched, _lat, _lon)) {
        WeatherData prev = _weather;   // keep the old summaries for day-by-day comparison
        _weather = fetched;
        _hasWeatherData = true;
        _realWeather = true;
        _needsFullRedraw = true;
        if (_selectedDay >= _weather.dayCount) _selectedDay = 0;

        _refreshGeneration++; // abandon any in-flight prefetch from the old forecast

        // Plan B: a routine refresh does NOT wipe every day's hourly cache.
        // Keep a cached day when the SAME calendar date is still in that
        // slot and that day's summary (weather code / hi / lo) is unchanged.
        // Today (index 0) is always re-pulled - its hourly values move as
        // the day goes on.
        if (xSemaphoreTake(_cacheMutex, portMAX_DELAY) == pdTRUE) {
            for (int i = 0; i < WeatherData::MAX_DAYS; i++) {
                if (i >= _weather.dayCount) { _hourlyCacheValid[i] = false; continue; }

                const String &iso = _weather.days[i].isoDate;
                bool keep = _hourlyCacheValid[i] && iso.length() > 0 && _hourlyCacheDate[i] == iso;

                if (keep && i == 0) keep = false; // re-pull today

                if (keep) {
                    for (int j = 0; j < prev.dayCount; j++) {
                        if (prev.days[j].isoDate == iso) {
                            if (prev.days[j].weatherCode != _weather.days[i].weatherCode ||
                                prev.days[j].highC       != _weather.days[i].highC ||
                                prev.days[j].lowC        != _weather.days[i].lowC)
                                keep = false;
                            break;
                        }
                    }
                }
                _hourlyCacheValid[i] = keep;
            }
            xSemaphoreGive(_cacheMutex);
        }

        // Only force the on-screen detail view to reload if the day it's
        // showing just lost its cache.
        if (_selectedDay < 0 || _selectedDay >= _weather.dayCount ||
            !_hourlyCacheValid[_selectedDay]) {
            _hourlyLoaded = false;
            _hourlyLoadedForDay = -1;
            // If the user is sitting in the detail view for that day, pull it
            // again in the background so the skeleton shows rather than a
            // stale curve or a blocking wait.
            if (_mode == VIEW_DAY_DETAIL) {
                _hourlyFailed = false;
                _hourlyPending = true;
                startOnDemandHourly(_selectedDay);
            }
        }

        Serial.println("[DashboardScreen] Weather updated");
        // Prefetch trades a handful of background requests for a detail
        // view that opens instantly; turning it off keeps the device
        // quieter on the network at the cost of a wait on first open.
        if (ConfigStore::get().weatherPrefetch) {
            startBackgroundPrefetch(); // pulls only the days whose cache was dropped
        }
    } else {
        // Keep showing the last-known-good (or dummy) data on failure -
        // don't blank the screen just because one fetch failed.
        Serial.println("[DashboardScreen] Weather fetch failed, keeping last data");
    }
}

// ================ BACKGROUND HOURLY PREFETCH ================
// Runs on its own FreeRTOS task, pinned to core 0 (the Arduino loop() runs
// on core 1), so network waits never block drawing or input handling.
// Walks every day in the current forecast and fetches its hourly data into
// _hourlyCache. Skipped days (already cached, e.g. one loaded on-demand by
// a user click that beat the prefetch to it) are left alone.
void DashboardScreen::startBackgroundPrefetch() {
    if (_prefetchRunning) return; // previous prefetch still going - let it finish, next refresh will retry anything it missed
    if (!_hasCoords || _weather.dayCount <= 0) return;

    PrefetchParams* params = new PrefetchParams();
    params->self = this;
    params->lat = _lat;
    params->lon = _lon;
    params->dayCount = _weather.dayCount;
    params->generation = _refreshGeneration;
    for (int i = 0; i < _weather.dayCount; i++) {
        params->isoDates[i] = _weather.days[i].isoDate;
    }

    _prefetchRunning = true;
    BaseType_t ok = xTaskCreatePinnedToCore(
        prefetchTaskEntry,
        "wxPrefetch",
        12288,      // HTTPS/TLS needs a fair bit of stack headroom
        params,
        1,          // low priority - don't compete with UI/input tasks
        nullptr,
        0           // pin to core 0
    );

    if (ok != pdPASS) {
        Serial.println("[DashboardScreen] Failed to start prefetch task");
        delete params;
        _prefetchRunning = false;
    }
}

void DashboardScreen::prefetchTaskEntry(void* param) {
    PrefetchParams* params = static_cast<PrefetchParams*>(param);
    params->self->runPrefetch(params);
    delete params;
    vTaskDelete(nullptr);
}

void DashboardScreen::runPrefetch(PrefetchParams* params) {
    // Hold off at boot: the first flight lookup, the quote fetch, the
    // firmware check and the current-conditions fetch all run their own
    // TLS handshakes right after connect, and each wants ~35 KB contiguous.
    // Starting 7 more secure connections into that fragments the heap
    // enough to make handshakes fail ("connection refused" / X509 alloc).
    for (int w = 0; w < 12 && params->generation == _refreshGeneration; w++)
        vTaskDelay(pdMS_TO_TICKS(1000));

    for (int i = 0; i < params->dayCount; i++) {
        if (params->generation != _refreshGeneration) break; // a newer refresh started - abandon this pass

        // Don't add a TLS handshake to an already-strained heap.
        while (ESP.getFreeHeap() < 95000 && params->generation == _refreshGeneration)
            vTaskDelay(pdMS_TO_TICKS(1500));

        bool alreadyCached = false;
        if (xSemaphoreTake(_cacheMutex, portMAX_DELAY) == pdTRUE) {
            alreadyCached = _hourlyCacheValid[i];
            xSemaphoreGive(_cacheMutex);
        }
        if (alreadyCached) continue;
        if (params->isoDates[i].length() == 0) continue;

        WeatherHourly fetched;
        Serial.printf("[DashboardScreen] Prefetching hourly for day %d in background\n", i);
        if (fetchHourlyForecast(fetched, params->lat, params->lon, params->isoDates[i])) {
            if (xSemaphoreTake(_cacheMutex, portMAX_DELAY) == pdTRUE) {
                if (params->generation == _refreshGeneration) { // still fresh
                    _hourlyCache[i] = fetched;
                    _hourlyCacheValid[i] = true;
                    _hourlyCacheDate[i] = params->isoDates[i];
                }
                xSemaphoreGive(_cacheMutex);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1500)); // gentle on the WiFi/TLS stack between requests
    }
    _prefetchRunning = false;
}

// Non-blocking. If the selected day's hourly data is already cached (loaded
// earlier, or pulled by the background prefetch) it's copied in and we're
// done. Otherwise the detail view shows its animated skeleton and a one-shot
// task fetches the day in the background; update() picks the result up.
void DashboardScreen::loadHourlyForSelectedDay() {
    if (_selectedDay < 0 || _selectedDay >= _weather.dayCount) return;

    // Already showing this day - nothing to do.
    if (_hourlyLoaded && _hourlyLoadedForDay == _selectedDay) return;

    bool cached = false;
    if (xSemaphoreTake(_cacheMutex, portMAX_DELAY) == pdTRUE) {
        cached = _hourlyCacheValid[_selectedDay];
        if (cached) _hourly = _hourlyCache[_selectedDay];
        xSemaphoreGive(_cacheMutex);
    }
    if (cached) {
        _hourlyLoaded = true;
        _hourlyLoadedForDay = _selectedDay;
        _hourlyPending = false;
        _hourlyFailed = false;
        _needsFullRedraw = true;
        return;
    }

    const String &iso = (_hasCoords && _selectedDay < _weather.dayCount)
                            ? _weather.days[_selectedDay].isoDate : String();
    if (iso.length() == 0) {
        // Dummy/offline data has no real date to query - nothing to fetch.
        _hourlyLoaded = false;
        _hourlyPending = false;
        _hourlyFailed = true;
        _needsFullRedraw = true;
        return;
    }

    // Not cached: show the skeleton now, fetch off the UI thread.
    _hourlyLoaded = false;
    _hourlyFailed = false;
    _hourlyPending = true;
    _needsFullRedraw = true;
    startOnDemandHourly(_selectedDay);
}

struct HourlyParams {
    DashboardScreen* self;
    float lat, lon;
    String iso;
    int day;
    uint32_t generation;
};

// Fetch one day's hourly data on its own short-lived FreeRTOS task so the
// UI thread never stalls on the TLS handshake / HTTP round-trip.
void DashboardScreen::startOnDemandHourly(int day) {
    if (_hourlyFetching) return;                 // one at a time
    if (!_hasCoords || day < 0 || day >= _weather.dayCount) return;
    const String &iso = _weather.days[day].isoDate;
    if (iso.length() == 0) return;

    HourlyParams* p = new HourlyParams{ this, _lat, _lon, iso, day, _refreshGeneration };
    _hourlyFetching = true;

    BaseType_t ok = xTaskCreatePinnedToCore(
        hourlyTaskEntry, "wxHourly", 12288, p,
        2,   // above the background prefetch: the user is waiting on this one
        nullptr, 0);

    if (ok != pdPASS) {
        delete p;
        _hourlyFetching = false;
        _hourlyFailed = true;
        _hourlyPending = false;
    }
}

void DashboardScreen::hourlyTaskEntry(void* param) {
    HourlyParams* p = static_cast<HourlyParams*>(param);
    DashboardScreen* self = p->self;

    // Wait out any transient low-heap moment (other tasks mid-handshake)
    // rather than letting our own handshake fail.
    for (int w = 0; w < 25 && ESP.getFreeHeap() < 90000; w++)
        vTaskDelay(pdMS_TO_TICKS(300));

    WeatherHourly fetched;
    bool ok = fetchHourlyForecast(fetched, p->lat, p->lon, p->iso);

    if (ok && xSemaphoreTake(self->_cacheMutex, portMAX_DELAY) == pdTRUE) {
        if (p->generation == self->_refreshGeneration && p->day < WeatherData::MAX_DAYS) {
            self->_hourlyCache[p->day]      = fetched;
            self->_hourlyCacheValid[p->day] = true;
            self->_hourlyCacheDate[p->day]  = p->iso;
        }
        xSemaphoreGive(self->_cacheMutex);
    }
    if (!ok) self->_hourlyFailed = true;

    self->_hourlyFetching = false;
    delete p;
    vTaskDelete(nullptr);
}

unsigned long DashboardScreen::refreshIntervalMs() const {
    uint8_t steps = ConfigStore::get().weatherRefresh10;
    if (steps == 0) steps = 3;                 // never hammer the API
    return (unsigned long)steps * 10UL * 60UL * 1000UL;
}

void DashboardScreen::update() {
    // Move the "current hour" highlight along in the day-detail view without
    // touching anything else on screen. Cheap (just a clock read) so this
    // runs even while _hasCoords is false / no weather yet - it'll just be
    // a no-op via findCurrentHourIndex()'s own guards.
    if (_mode == VIEW_DAY_DETAIL) {
        unsigned long nowMs = millis();
        if (_lastHourCheck == 0 || nowMs - _lastHourCheck > HOUR_CHECK_INTERVAL_MS) {
            _lastHourCheck = nowMs;
            updateHourHighlight(false);
        }
    }

    // A background hourly fetch we're waiting on may have landed.
    if (_mode == VIEW_DAY_DETAIL && _hourlyPending) {
        bool valid = false;
        if (xSemaphoreTake(_cacheMutex, portMAX_DELAY) == pdTRUE) {
            valid = _hourlyCacheValid[_selectedDay];
            if (valid) _hourly = _hourlyCache[_selectedDay];
            xSemaphoreGive(_cacheMutex);
        }
        if (valid) {
            _hourlyLoaded = true;
            _hourlyLoadedForDay = _selectedDay;
            _hourlyPending = false;
            _hourlyFailed = false;
            int maxScroll = max(0, _hourly.hourCount - HOUR_WINDOW);
            int cur = findCurrentHourIndex();
            _hourScroll = (cur >= 0) ? constrain(cur - HOUR_WINDOW / 2, 0, maxScroll) : 0;
            _needsFullRedraw = true;
        } else if (!_hourlyFetching) {
            // Task finished without data - stop the skeleton, show the message.
            _hourlyPending = false;
            _hourlyFailed = true;
            _needsFullRedraw = true;
        }
    }

    // Animate the loading skeleton (only once a skeleton draw has laid down
    // the static parts - _needsFullRedraw still set means it hasn't yet).
    if (!_needsFullRedraw && skeletonActive()) {
        unsigned long ms = millis();
        if (ms - _skelLastTick >= SKEL_TICK_MS) {
            _skelLastTick = ms;
            tickSkeleton();
        }
    }

    if (!_hasCoords) return;

    unsigned long now = millis();
    if (_lastRefresh == 0 || now - _lastRefresh > refreshIntervalMs()) {
        _lastRefresh = now;
        refreshWeatherNow();
    }
}

bool DashboardScreen::skeletonActive() const {
    if (_mode == VIEW_WEEK)  return !_realWeather;
    return !_hourlyLoaded && !_hourlyFailed;   // day detail, still waiting
}

void DashboardScreen::onEncoderUp() {
    if (_mode == VIEW_WEEK) {
        int dayCount = _weather.dayCount > 0 ? _weather.dayCount : 1;
        int previous = _selectedDay;
        _selectedDay = (_selectedDay - 1 + dayCount) % dayCount;
        // Redraw just the two affected rows in place - no screen clear.
        drawForecastRow(previous);
        drawForecastRow(_selectedDay);
    } else {
        scrollHours(-1); // day detail: pan the hour window earlier
    }
}

void DashboardScreen::onEncoderDown() {
    if (_mode == VIEW_WEEK) {
        int dayCount = _weather.dayCount > 0 ? _weather.dayCount : 1;
        int previous = _selectedDay;
        _selectedDay = (_selectedDay + 1) % dayCount;
        drawForecastRow(previous);
        drawForecastRow(_selectedDay);
    } else {
        scrollHours(1); // day detail: pan the hour window later
    }
}

void DashboardScreen::scrollHours(int dir) {
    if (_mode != VIEW_DAY_DETAIL || !_hourlyLoaded) return;

    int maxScroll = max(0, _hourly.hourCount - HOUR_WINDOW);
    int prev = _hourScroll;
    _hourScroll = constrain(_hourScroll + dir, 0, maxScroll);

    if (_hourScroll != prev) {
        drawHourPlot(false);   // just the panning plot area, not the header/readout
        _needsFullRedraw = false;
    }
}

void DashboardScreen::onButtonPress() {
    if (_mode == VIEW_WEEK) {
        _mode = VIEW_DAY_DETAIL;
        _hourlyFailed = false;
        if (_hourlyLoadedForDay != _selectedDay) {
            _hourlyLoaded = false;
            loadHourlyForSelectedDay();   // non-blocking: cache copy, or skeleton + bg fetch
        }
        // Centre the window on the current hour (or the start of the day for
        // any day that isn't today). When the data is still loading this is
        // recomputed in update() once it arrives.
        int maxScroll = max(0, _hourly.hourCount - HOUR_WINDOW);
        int cur = _hourlyLoaded ? findCurrentHourIndex() : -1;
        _hourScroll = (cur >= 0) ? constrain(cur - HOUR_WINDOW / 2, 0, maxScroll) : 0;
    } else {
        _mode = VIEW_WEEK;
    }
    _needsFullRedraw = true;
}

void DashboardScreen::draw() {
    // Only repaint when something actually changed - the top status bar
    // (date/time) is handled separately by ScreenManager/Header every loop,
    // so this screen doesn't need to redraw on every tick.
    if (!_needsFullRedraw) return;
    _needsFullRedraw = false;

    // Clear the body only - never the top 20px, which belongs to the
    // persistent Header (drawn by ScreenManager; it won't repaint itself
    // after a wipe until its own content changes). A full fillScreen() here
    // made the header vanish when switching week <-> day views.
    tft->fillRect(0, 20, tft->width(), tft->height() - 20, ThemeManager::current().bg);

    if (_mode == VIEW_WEEK) {
        if (!_realWeather) {
            drawWeekSkeleton();
        } else {
            drawCurrentConditions();
            drawForecast();
        }
    } else {
        // The day/date/hi-lo strip and the temperature curve are only fully
        // redrawn here (entering the view or switching day). After that,
        // panning the hour window and the "now" marker rolling over each do
        // a targeted plot-only redraw, never a full page repaint.
        drawDayDetailHeader();
        if (!_hourlyLoaded) {
            if (_hourlyFailed) drawHourPlot(true);   // shows the "unavailable" message
            else               drawDaySkeleton();
        } else {
            _highlightedHourIndex = findCurrentHourIndex();
            drawHourPlot(true);
            _lastHourCheck = millis();
        }
    }
}

// ================ LOADING SKELETON ================
// Shown instead of blocking the UI while weather data is still in flight.
// A bright accent scan line sweeps up and down the band where the real
// temperature figures will land; everything else is drawn dim.

void DashboardScreen::drawWeekSkeleton() {
    const Theme &t = ThemeManager::current();
    const int W = tft->width();

    // Current-conditions placeholders (big temp block + two text lines).
    tft->fillRoundRect(10, 24, 92, 22, 3, t.rule);
    tft->fillRect(104, 27, 120, 6, t.rule);
    tft->fillRect(104, 44, 150, 6, t.rule);

    // Table chrome - identical to drawForecast()'s header.
    tft->setTextSize(1);
    tft->setTextColor(t.fgDim);
    tft->setCursor(TABLE_COL_DAY,  TABLE_TOP); tft->print("Day");
    tft->setCursor(TABLE_COL_COND, TABLE_TOP); tft->print("Condition");
    tft->setCursor(TABLE_COL_HI,   TABLE_TOP); tft->print("Hi");
    tft->setCursor(TABLE_COL_LO,   TABLE_TOP); tft->print("Lo");
    tft->drawFastHLine(TABLE_COL_DAY, TABLE_TOP + 11, W - TABLE_COL_DAY - 10, t.rule);

    int rows = _weather.dayCount > 0 ? _weather.dayCount : 7;
    for (int i = 0; i < rows; i++) {
        int y = TABLE_TOP + 16 + i * TABLE_ROW_H;
        tft->fillRect(TABLE_COL_DAY,  y, 32, 6, t.rule);
        tft->fillRect(TABLE_COL_COND, y, 90, 6, t.rule);
        tft->fillRect(TABLE_COL_HI,   y, 22, 6, t.rule);
        tft->fillRect(TABLE_COL_LO,   y, 22, 6, t.rule);
    }

    _scanPos = TABLE_TOP + 14;
    _scanDir = 1;
    _skelLastTick = millis();
}

void DashboardScreen::drawDaySkeleton() {
    const Theme &t = ThemeManager::current();
    const int W = tft->width();
    const int right = W - 10;
    const int mid = (PLOT_TOP + PLOT_BOTTOM) / 2;

    tft->fillRect(0, 50, W, tft->height() - 50, t.bg);

    // Dashed baseline where the temperature curve will be.
    for (int x = PLOT_LEFT; x < right; x += 6)
        tft->drawFastHLine(x, mid, 3, t.rule);

    // Placeholder blocks for the hour labels under the plot.
    int slots = HOUR_WINDOW;
    int stepX = (slots > 1) ? (right - PLOT_LEFT) / (slots - 1) : 0;
    for (int i = 0; i < slots; i++) {
        int x = PLOT_LEFT + i * stepX;
        tft->fillRect(x - 6, PLOT_HOURLBL_Y, 12, 6, t.rule);
    }

    // Scrollbar track (chrome) + readout placeholder.
    tft->drawRect(PLOT_LEFT, PLOT_SCROLLBAR_Y, (right - PLOT_LEFT), 5, t.rule);
    tft->setTextSize(1);
    tft->setTextColor(t.fgDim, t.bg);
    tft->setCursor(10, PLOT_READOUT_Y);
    tft->print("Loading hourly forecast");

    _scanPos = PLOT_TOP;
    _scanDir = 1;
    _skelLastTick = millis();
}

// Partial redraw: erase the old scan line (restoring the dim baseline it may
// have crossed), step it, draw it at the new position.
void DashboardScreen::tickSkeleton() {
    const Theme &t = ThemeManager::current();
    const int W = tft->width();
    int x0, x1, top, bot, mid;

    if (_mode == VIEW_DAY_DETAIL) {
        x0 = PLOT_LEFT; x1 = W - 10;
        top = PLOT_TOP; bot = PLOT_BOTTOM;
        mid = (PLOT_TOP + PLOT_BOTTOM) / 2;
    } else {
        x0 = TABLE_COL_HI - 4; x1 = W - 12;
        top = TABLE_TOP + 14;
        bot = TABLE_TOP + 16 + (_weather.dayCount > 0 ? _weather.dayCount : 7) * TABLE_ROW_H;
        mid = -1;
    }

    tft->drawFastHLine(x0, _scanPos, x1 - x0, t.bg);          // erase old line
    if (mid >= 0)                                             // restore baseline
        for (int x = x0; x < x1; x += 6) tft->drawFastHLine(x, mid, 3, t.rule);

    _scanPos += _scanDir * 3;
    if (_scanPos >= bot) { _scanPos = bot; _scanDir = -1; }
    if (_scanPos <= top) { _scanPos = top; _scanDir =  1; }

    tft->drawFastHLine(x0, _scanPos, x1 - x0, t.accent);      // draw new line
}

// Compact single strip: big temp on the left, condition + location on the
// right, so the rest of the (landscape) screen is free for the day table.
void DashboardScreen::drawCurrentConditions() {
    const Theme &theme = ThemeManager::current();

    tft->setTextSize(3);
    tft->setTextColor(theme.fg);
    tft->setCursor(10, 24);
    tft->print(Units::temp(_weather.currentTempC));
    tft->print(" ");
    tft->println(Units::tempUnit());

    tft->setTextSize(1);
    tft->setTextColor(theme.fgDim);
    tft->setCursor(100, 26);
    tft->println(weatherCodeToText(_weather.currentWeatherCode));

    tft->setTextColor(theme.fgDim);
    tft->setCursor(100, 44);
    tft->print(_city);
    if (_country.length() > 0) {
        tft->print(", ");
        tft->print(_country);
    }
}

// 7-day forecast as a compact table (one row per day). Turn the encoder to
// move the highlighted row, press to open that day's hourly detail.
void DashboardScreen::drawForecast() {
    const Theme &theme = ThemeManager::current();

    tft->setTextSize(1);
    tft->setTextColor(theme.fg);
    tft->setCursor(TABLE_COL_DAY, TABLE_TOP);
    tft->print("Day");
    tft->setCursor(TABLE_COL_COND, TABLE_TOP);
    tft->print("Condition");
    tft->setCursor(TABLE_COL_HI, TABLE_TOP);
    tft->print("Hi");
    tft->setCursor(TABLE_COL_LO, TABLE_TOP);
    tft->print("Lo");

    tft->drawFastHLine(TABLE_COL_DAY, TABLE_TOP + 11, tft->width() - TABLE_COL_DAY - 10, theme.rule);

    // The API may hand back more days than the user wants listed; the
    // extra days stay cached (the detail view can still reach them), they
    // just don't take up rows in the week table.
    int shown = _weather.dayCount;
    int wanted = ConfigStore::get().weatherDays;
    if (wanted > 0 && wanted < shown) shown = wanted;

    for (int i = 0; i < shown; i++) {
        drawForecastRow(i);
    }
}

// Redraws a single day's row in place - background + text only, no
// fillScreen. Safe to call on its own (e.g. when just the selection moves)
// without touching the rest of the screen.
void DashboardScreen::drawForecastRow(int index) {
    if (index < 0 || index >= _weather.dayCount) return;

    const Theme &theme = ThemeManager::current();
    const WeatherDay &day = _weather.days[index];
    int y = TABLE_TOP + 16 + index * TABLE_ROW_H;
    bool selected = (index == _selectedDay);

    // Row background - selected row is inverted (fill + light text), same
    // shared helper every list screen uses.
    UiChrome::drawPanelRow(tft, TABLE_COL_DAY - 4, y - 3, tft->width() - TABLE_COL_DAY - 4,
                            TABLE_ROW_H - 2, selected, theme.bg, theme.selectBg);

    tft->setTextSize(1);
    tft->setTextColor(selected ? theme.selectFg : (index == 0 ? theme.accent : theme.fgDim));
    tft->setCursor(TABLE_COL_DAY, y);
    tft->print(day.dayLabel);

    tft->setTextColor(selected ? theme.selectFg : theme.fgDim);
    if (ConfigStore::get().weatherIcons) {
        // Same glyph the day-detail view draws, shrunk into the gap before
        // the condition text rather than replacing it - the word still has
        // to be readable, the icon is just faster to scan down the column.
        drawWeatherGlyph(TABLE_COL_COND + 4, y + 3, day.weatherCode,
                         selected ? theme.selectFg : theme.fgDim);
        tft->setCursor(TABLE_COL_COND + 14, y);
    } else {
        tft->setCursor(TABLE_COL_COND, y);
    }
    tft->print(weatherCodeToText(day.weatherCode));

    tft->setTextColor(selected ? theme.selectFg : theme.fg);
    tft->setCursor(TABLE_COL_HI, y);
    tft->print(Units::temp(day.highC));
    tft->print(Units::tempUnit());

    tft->setTextColor(selected ? theme.selectFg : theme.fgDim);
    tft->setCursor(TABLE_COL_LO, y);
    tft->print(Units::temp(day.lowC));
    tft->print(Units::tempUnit());
}

// Static header strip for the day-detail view: day label, date, hi/lo,
// condition. Only changes when the selected day changes, so it's only
// drawn from draw() (full page entry), never from the per-tick highlight
// update.
void DashboardScreen::drawDayDetailHeader() {
    if (_selectedDay < 0 || _selectedDay >= _weather.dayCount) return;
    const WeatherDay &day = _weather.days[_selectedDay];

    const Theme &theme = ThemeManager::current();

    // Clear just this strip before redrawing - new text may be shorter
    // than what was here before (e.g. "Rain" -> "Fog", or a shorter date),
    // so without this, leftover characters from the old day bleed through.
    tft->fillRect(0, 22, tft->width(), 26, theme.bg);

    tft->setTextSize(1);
    tft->setTextColor(theme.accent);
    tft->setCursor(10, 26);
    tft->print(day.dayLabel);
    if (day.isoDate.length() > 0) {
        tft->print("  ");
        tft->print(day.isoDate);
    }

    tft->setTextColor(theme.fgDim);
    tft->setCursor(10, 40);
    tft->print("Hi ");
    tft->print(Units::temp(day.highC));
    tft->print(Units::tempUnit());
    tft->print(" / Lo ");
    tft->print(Units::temp(day.lowC));
    tft->print(Units::tempUnit());
    tft->print("  -  ");
    tft->print(weatherCodeToText(day.weatherCode));
}

// Tiny weather-condition icon (~14x14) drawn from primitives, since the
// build only compiles the GLCD text font. Maps WMO weather codes (as
// Open-Meteo returns them) to one of: sun / partly / cloud / fog / rain /
// snow. `k` is the single ink colour to use.
void DashboardScreen::drawWeatherGlyph(int cx, int cy, int code, uint16_t k) {
    auto sun = [&](int scx, int scy, int r) {
        tft->drawCircle(scx, scy, r, k);
        tft->drawFastVLine(scx, scy - r - 3, 2, k);
        tft->drawFastVLine(scx, scy + r + 2, 2, k);
        tft->drawFastHLine(scx - r - 3, scy, 2, k);
        tft->drawFastHLine(scx + r + 2, scy, 2, k);
        tft->drawPixel(scx - r - 1, scy - r - 1, k);
        tft->drawPixel(scx + r + 1, scy - r - 1, k);
        tft->drawPixel(scx - r - 1, scy + r + 1, k);
        tft->drawPixel(scx + r + 1, scy + r + 1, k);
    };
    auto cloud = [&](int ccx, int by) {           // solid cloud, base at by
        tft->fillCircle(ccx - 3, by - 1, 3, k);
        tft->fillCircle(ccx + 2, by - 2, 3, k);
        tft->fillCircle(ccx + 5, by - 1, 2, k);
        tft->fillRect(ccx - 3, by - 1, 9, 3, k);
    };

    bool fog    = (code == 45 || code == 48);
    bool snow   = (code >= 71 && code <= 77) || code == 85 || code == 86;

    if (fog) {
        tft->drawFastHLine(cx - 5, cy - 3, 11, k);
        tft->drawFastHLine(cx - 6, cy,     12, k);
        tft->drawFastHLine(cx - 4, cy + 3,  9, k);
        return;
    }
    if (code <= 1) { sun(cx, cy, 3); return; }          // clear / mainly clear
    if (code == 2) { sun(cx + 3, cy - 3, 2); cloud(cx - 1, cy + 4); return; } // partly
    if (code == 3) { cloud(cx, cy + 2); return; }       // overcast

    // Anything else falls through to precipitation (drizzle 51-57, rain
    // 61-67, showers 80-82, thunder 95-99, and snow via the flag above).
    cloud(cx, cy);
    if (snow) {
        tft->drawPixel(cx - 3, cy + 5, k); tft->drawPixel(cx - 4, cy + 5, k); tft->drawPixel(cx - 2, cy + 5, k); tft->drawPixel(cx - 3, cy + 4, k); tft->drawPixel(cx - 3, cy + 6, k);
        tft->drawPixel(cx + 1, cy + 6, k); tft->drawPixel(cx,     cy + 6, k); tft->drawPixel(cx + 2, cy + 6, k); tft->drawPixel(cx + 1, cy + 5, k); tft->drawPixel(cx + 1, cy + 7, k);
    } else {
        tft->drawLine(cx - 3, cy + 3, cx - 4, cy + 6, k);
        tft->drawLine(cx,     cy + 3, cx - 1, cy + 6, k);
        tft->drawLine(cx + 3, cy + 3, cx + 2, cy + 6, k);
    }
}

// Scrollable 24-hour temperature curve. HOUR_WINDOW hours are visible at a
// time; the encoder pans the window (_hourScroll). With `full`, the whole
// area below the header is cleared and the fixed chrome (readout line) is
// drawn too; otherwise only the panning plot band is cleared/redrawn, so a
// knob turn or an hourly "now" tick never repaints the header/readout.
void DashboardScreen::drawHourPlot(bool full) {
    const Theme &t = ThemeManager::current();
    const int W = tft->width();
    const int right = W - 10;

    if (full) {
        tft->fillRect(0, 50, W, tft->height() - 50, t.bg);
    } else {
        // Just the plot band: temp labels above the curve, the curve, the
        // hour numbers, and the scrollbar. The readout line below is left
        // alone.
        tft->fillRect(0, PLOT_TOP - 16, W, (PLOT_SCROLLBAR_Y + 7) - (PLOT_TOP - 16), t.bg);
    }

    if (!_hourlyLoaded) {
        tft->setTextSize(1);
        tft->setTextColor(t.fgDim);
        tft->setCursor(10, 78);
        tft->println("Hourly forecast unavailable");
        tft->setCursor(10, 94);
        tft->println("(no data for this day yet)");
        return;
    }

    const int n = _hourly.hourCount;
    if (n <= 0) return;

    // Whole-day min/max so panning just slides the same curve rather than
    // rescaling the y axis every step.
    int tmin = _hourly.hours[0].tempC, tmax = tmin;
    int maxIdx = 0, minIdx = 0;
    for (int i = 1; i < n; i++) {
        int v = _hourly.hours[i].tempC;
        if (v < tmin) { tmin = v; minIdx = i; }
        if (v > tmax) { tmax = v; maxIdx = i; }
    }
    if (tmax == tmin) tmax = tmin + 1;

    const int window = min(HOUR_WINDOW, n);
    const int stepX = (window > 1) ? (right - PLOT_LEFT) / (window - 1) : 0;
    const int last = min(_hourScroll + window, n);
    const int curIdx = _highlightedHourIndex;

    tft->setTextSize(1);
    tft->setTextDatum(MC_DATUM);

    int px = 0, py = 0;
    for (int i = _hourScroll; i < last; i++) {
        const HourPoint &h = _hourly.hours[i];
        int x = PLOT_LEFT + (i - _hourScroll) * stepX;
        int y = map(h.tempC, tmin, tmax, PLOT_BOTTOM, PLOT_TOP);
        bool isNow = (i == curIdx);

        if (i > _hourScroll) {
            tft->drawLine(px, py, x, y, t.fg);
            tft->drawLine(px, py + 1, x, y + 1, t.fg);
        }

        uint16_t mark = isNow ? t.accent : t.fg;
        tft->fillCircle(x, y, 2, mark);

        char tb[8]; snprintf(tb, sizeof(tb), "%d\xF8", Units::temp(h.tempC));
        tft->setTextColor(mark, t.bg);
        tft->drawString(tb, x, y - 11);

        drawWeatherGlyph(x, PLOT_GLYPH_Y, h.weatherCode, isNow ? t.accent : t.fgDim);

        char hb[4]; snprintf(hb, sizeof(hb), "%02d", h.hour);
        tft->setTextColor(isNow ? t.accent : t.fgDim, t.bg);
        tft->drawString(hb, x, PLOT_HOURLBL_Y);

        px = x;
        py = y;   // remember this point so the next iteration draws a segment to it
    }

    // "Now" marker - a dashed vertical line if the current hour is in view.
    if (curIdx >= _hourScroll && curIdx < last) {
        int nx = PLOT_LEFT + (curIdx - _hourScroll) * stepX;
        for (int yy = PLOT_TOP - 3; yy < PLOT_BOTTOM + 3; yy += 5)
            tft->drawFastVLine(nx, yy, 2, t.accent);
    }

    // Scrollbar: full 0..n span, filled slice = the visible window.
    const int trackW = right - PLOT_LEFT;
    tft->drawRect(PLOT_LEFT, PLOT_SCROLLBAR_Y, trackW, 5, t.rule);
    int fx = PLOT_LEFT + (int)((long)trackW * _hourScroll / n);
    int fw = (int)((long)trackW * window / n);
    if (fw < 4) fw = 4;
    tft->fillRect(fx + 1, PLOT_SCROLLBAR_Y + 1, fw, 3, t.accent);

    // Fixed readout line (only on a full redraw - these are whole-day stats).
    if (full) {
        tft->setTextDatum(TL_DATUM);
        char rb[72];
        const char u = Units::tempUnit();
        if (curIdx >= 0)
            snprintf(rb, sizeof(rb), "NOW %d\xF8%c %s   MAX %d\xF8@%02d   MIN %d\xF8@%02d",
                     Units::temp(_hourly.hours[curIdx].tempC), u,
                     weatherCodeToText(_hourly.hours[curIdx].weatherCode).c_str(),
                     Units::temp(_hourly.hours[maxIdx].tempC), _hourly.hours[maxIdx].hour,
                     Units::temp(_hourly.hours[minIdx].tempC), _hourly.hours[minIdx].hour);
        else
            snprintf(rb, sizeof(rb), "MAX %d\xF8%c@%02d   MIN %d\xF8%c@%02d",
                     Units::temp(_hourly.hours[maxIdx].tempC), u, _hourly.hours[maxIdx].hour,
                     Units::temp(_hourly.hours[minIdx].tempC), u, _hourly.hours[minIdx].hour);
        tft->setTextColor(t.fgDim, t.bg);
        tft->setCursor(10, PLOT_READOUT_Y);
        tft->print(rb);
    }

    tft->setTextDatum(TL_DATUM);
}

// Finds which row of _hourly corresponds to the real current hour. Only
// meaningful when looking at today's forecast (day index 0) - any other
// day has no "now" to highlight.
int DashboardScreen::findCurrentHourIndex() {
    if (_selectedDay != 0 || !_hourlyLoaded) return -1;

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 5)) return -1; // clock not synced yet - skip highlighting rather than guess

    for (int i = 0; i < _hourly.hourCount; i++) {
        if (_hourly.hours[i].hour == timeinfo.tm_hour) return i;
    }
    return -1;
}

// Kept for source compatibility - the day-switch-in-detail path no longer
// exists (the encoder pans hours there now), but harmless if ever called:
// a full in-place redraw of the header + curve, no screen clear.
void DashboardScreen::refreshDayDetailInPlace() {
    drawDayDetailHeader();
    _highlightedHourIndex = findCurrentHourIndex();
    drawHourPlot(true);
    _lastHourCheck = millis();
}

// Called periodically from update(). When the real current hour rolls over,
// redraw the curve so its "now" marker moves - just the plot band, never
// the header or readout line.
void DashboardScreen::updateHourHighlight(bool force) {
    if (_mode != VIEW_DAY_DETAIL || !_hourlyLoaded) return;

    int newIndex = findCurrentHourIndex();
    if (!force && newIndex == _highlightedHourIndex) return;

    _highlightedHourIndex = newIndex;
    drawHourPlot(false);
}

void DashboardScreen::getActionLegend(String &line1, String &line2) const {
    if (_mode == VIEW_WEEK) {
        line1 = "^v SCROLL DAY";
        line2 = "o OPEN DAY DETAIL";
    } else {
        line1 = "^v SCROLL HOURS";
        line2 = "o BACK TO WEEK";
    }
}

void DashboardScreen::recordFlight() {
    _flightsDetectedToday++;
}

void DashboardScreen::setTimezone(const String& tz) {
    _timezone = tz;
}

void DashboardScreen::setLocation(const String& city, const String& country) {
    _city = city;
    _country = country;
    _needsFullRedraw = true;
}