#pragma once
#include <Arduino.h>

// Where the Stock screen gets its API key and ticker list - a free account
// at finnhub.io gives an API key, pasted here alongside a comma-separated
// list of symbols (e.g. "AAPL,TSLA,MSFT").
//
// WHY NOT IN Config: same reasoning as NotesSource/CalendarFeeds - the key
// and symbol list are variable-length text written once and read once per
// refresh, so this is its own NVS entry rather than growing the fixed-size
// Config blob.
namespace StockSource {

    // 10 is a cap for sanity, not a technical limit - at 800ms between each
    // symbol's fetch (see StockScreen::fetchTaskEntry()), 10 symbols is an
    // ~8s fetch cycle, still comfortably inside Finnhub's free-tier rate
    // limit (60 calls/min) even refreshed every 5 minutes.
    static const int MAX_SYMBOLS = 10;

    // Load from NVS. Safe to call more than once.
    void begin();

    const String& apiKey();
    const String& symbolsCsv();     // raw "AAPL,TSLA,MSFT" as the user typed it
    int  symbolCount();             // parsed, capped at MAX_SYMBOLS
    String symbolAt(int index);     // trimmed, uppercased; "" if out of range
    int  refreshMinutes();          // how often the screen re-fetches

    bool usable();                  // key + at least one symbol

    // Editing. Doesn't write to flash; call save() when done.
    void set(const String &apiKey, const String &symbolsCsv, int refreshMinutes);
    void save();

} // namespace StockSource
