#pragma once
#include <Arduino.h>

struct QuoteData {
    String text;    // the quote itself, e.g. "The only way to do great work..."
    String author;  // e.g. "Steve Jobs"

    QuoteData() : text(""), author("") {}
};

// Fetches a quote to display. Primary source is ZenQuotes' daily endpoint
// (https://zenquotes.io/api/today - free, no key); if that fails every
// retry, it falls back to a random quote from dummyjson.com so a flaky
// TLS/connection hiccup doesn't leave the screen without a quote. Each
// source is retried a couple of times before moving on. See quote_api.cpp.
// Returns true and fills `data` on success; on total failure `data` is
// left untouched so callers keep showing the previous quote.
bool fetchQuoteOfTheDay(QuoteData &data);