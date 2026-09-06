#pragma once
#include "Screen.h"
#include "../api/flight_api.h"
#include "../api/weather_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// Weather + forecast screen, backed by the Open-Meteo API (see weather_api.h).
// Turn the encoder to move the highlighted day, press to open that day's
// hourly detail view (turn there to switch days, press again to go back).
class DashboardScreen : public Screen {
public:
    DashboardScreen(TFT_eSPI* display);

    void init() override;
    void update() override;
    void draw() override;
    void onEncoderUp() override;
    void onEncoderDown() override;
    void onButtonPress() override;
    const char* getName() const override { return "Weather"; }
    ScreenId id() const override { return ScreenId::Weather; }
    void getActionLegend(String &line1, String &line2) const override;

    // Kept for compatibility with main.cpp / flight tracking code
    void recordFlight();
    void setTimezone(const String& tz);
    void setLocation(const String& city, const String& country);

    // Coordinates to fetch weather for. Call once WiFi + location are known;
    // triggers an immediate refresh, then refreshes periodically after that.
    void setLocationCoords(float lat, float lon);

private:
    enum ViewMode { VIEW_WEEK, VIEW_DAY_DETAIL };

    String _timezone;
    String _city;
    String _country;
    int _flightsDetectedToday;

    float _lat, _lon;
    bool _hasCoords;

    WeatherData _weather;
    bool _hasWeatherData;
    bool _realWeather;   // false until the first live fetchWeather() succeeds - week view shows a skeleton until then

    ViewMode _mode;
    int _selectedDay;

    WeatherHourly _hourly;
    bool _hourlyLoaded;
    int _hourlyLoadedForDay;   // which day index _hourly currently holds

    // Opening a day no longer blocks on the network: if that day's hourly
    // data isn't cached yet, the detail view shows an animated skeleton and
    // a one-shot background task fetches it. _hourlyPending = waiting on that
    // task; _hourlyFetching = the task is actually in flight; _hourlyFailed =
    // the task finished with nothing, so show the "unavailable" message
    // instead of an endless skeleton.
    volatile bool _hourlyPending;
    volatile bool _hourlyFetching;
    volatile bool _hourlyFailed;
    static void hourlyTaskEntry(void* param);
    void startOnDemandHourly(int day);

    // Animated loading skeleton (week view before the first fetch, day view
    // before that day's hourly data arrives). A bright scan line sweeps up
    // and down the band where the temperature figures will appear.
    unsigned long _skelLastTick;
    int _scanPos;
    int _scanDir;
    static const unsigned long SKEL_TICK_MS = 60;
    bool skeletonActive() const;
    void drawWeekSkeleton();
    void drawDaySkeleton();
    void tickSkeleton();

    // Per-day cache so re-visiting a day already seen this refresh cycle
    // (scrolling back and forth in detail view) doesn't fire a new request.
    // Cleared whenever refreshWeatherNow() pulls fresh daily data.
    // Filled in two ways: on-demand in loadHourlyForSelectedDay(), and
    // proactively in the background by the prefetch task below - so by the
    // time the user clicks into a day, it's usually already sitting here.
    WeatherHourly _hourlyCache[WeatherData::MAX_DAYS];
    bool _hourlyCacheValid[WeatherData::MAX_DAYS];
    String _hourlyCacheDate[WeatherData::MAX_DAYS]; // isoDate each cache slot holds, so slots realign after a midnight day-roll
    SemaphoreHandle_t _cacheMutex;      // guards _hourlyCache[] / _hourlyCacheValid[] across tasks

    // Background prefetch: a low-priority FreeRTOS task (pinned to the core
    // the main loop() isn't running on) walks the week and fetches each
    // day's hourly data into the cache above, so the UI thread never blocks
    // waiting on the network unless the user gets there before it finishes.
    volatile uint32_t _refreshGeneration; // bumped on every successful daily refresh
    volatile bool _prefetchRunning;

    struct PrefetchParams {
        DashboardScreen* self;
        float lat, lon;
        String isoDates[WeatherData::MAX_DAYS];
        int dayCount;
        uint32_t generation;
    };
    static void prefetchTaskEntry(void* param);
    void runPrefetch(PrefetchParams* params);
    void startBackgroundPrefetch();

    bool _needsFullRedraw;
    unsigned long _lastRefresh;
    // How long between forecast refreshes. Read from the saved config
    // (stored in 10-minute steps) rather than fixed, so it can be traded
    // off against how chatty the device is on the network.
    unsigned long refreshIntervalMs() const;

    // Which hour cell is currently highlighted as "now" in the day-detail
    // view, and the last time we checked whether that should move on.
    // Checked periodically from update() and redrawn as a targeted 1-2 row
    // update - the header and the rest of the hour grid are NOT touched,
    // since neither changes just because a minute ticked over.
    int _highlightedHourIndex;
    unsigned long _lastHourCheck;
    static const unsigned long HOUR_CHECK_INTERVAL_MS = 15UL * 1000UL; // 15 sec

    // Day-detail is a scrollable 24h temperature curve: HOUR_WINDOW hours
    // are on screen at once and the encoder pans the window left/right
    // (_hourScroll = index of the leftmost visible hour).
    static const int HOUR_WINDOW = 8;
    int _hourScroll;

    void loadDummyWeather();   // fallback shown until the first real fetch succeeds
    void refreshWeatherNow();  // blocking HTTP fetch, mirrors flight_api's pattern
    void loadHourlyForSelectedDay();

    void drawCurrentConditions();
    void drawForecast();       // week view - table of days (full draw)
    void drawForecastRow(int index); // redraw a single row in place (no screen clear)

    void drawDayDetailHeader();   // day/date/hi-lo/condition strip - drawn once per day, not per tick
    void drawHourPlot(bool full); // the scrollable 24h temperature curve; full=clear+draw everything, else just the panning plot
    void drawWeatherGlyph(int cx, int cy, int weatherCode, uint16_t color); // tiny sun/cloud/rain/fog/snow icon
    void scrollHours(int dir);    // pan the visible hour window by dir (+/-1), redraw if it moved
    void refreshDayDetailInPlace(); // day switch while already in detail view - no fillScreen
    int findCurrentHourIndex();   // index into _hourly.hours matching the real current hour, or -1
    void updateHourHighlight(bool force); // redraws the plot's "now" marker when the current hour rolls over

    // Day-detail temperature-curve layout (content sits between the 20px
    // header and the ~30px action-legend bar).
    static const int PLOT_LEFT   = 16;
    static const int PLOT_TOP    = 66;
    static const int PLOT_BOTTOM = 150;
    static const int PLOT_GLYPH_Y     = 160; // weather-condition glyph row (centre y)
    static const int PLOT_HOURLBL_Y   = 172; // hour numbers under the glyphs
    static const int PLOT_SCROLLBAR_Y = 186;
    static const int PLOT_READOUT_Y   = 196; // NOW / MAX / MIN summary line

    // Table layout, shared between the full-table draw and single-row
    // redraws. Same reasoning as above.
    static const int TABLE_COL_DAY  = 10;
    static const int TABLE_COL_COND = 65;
    static const int TABLE_COL_HI   = 240;
    static const int TABLE_COL_LO   = 280;
    static const int TABLE_TOP      = 58;
    static const int TABLE_ROW_H    = 19;
};