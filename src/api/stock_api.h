#pragma once
#include <Arduino.h>

// One ticker's current quote, from Finnhub's /quote endpoint - the cheapest
// call that gives a usable "price + how the day's going" without needing
// intraday history (see stock_api.cpp for why Finnhub over another
// provider).
struct StockQuote {
    char  symbol[8];
    float price;
    float changePercent;
    bool  ok;        // false = this symbol failed - see notFound for which way
    bool  notFound;  // true = Finnhub confirmed this ticker doesn't exist (a
                      // typo, most likely) - distinct from a transient failure
                      // (network hiccup, bad key, timeout), which the screen
                      // shows as "unavailable" instead since that one might
                      // clear up on its own next refresh.

    StockQuote() : price(0), changePercent(0), ok(false), notFound(false) { symbol[0] = 0; }
};

// Fetches one symbol's current quote. `symbol` is copied into out.symbol
// regardless of success, so a failed row still shows which ticker it was.
// Returns false on any failure - see out.notFound to tell a confirmed-bad
// ticker apart from a transient one (no WiFi, bad/missing key, network
// error, non-200).
bool fetchStockQuote(const String &symbol, const String &apiKey, StockQuote &out);
