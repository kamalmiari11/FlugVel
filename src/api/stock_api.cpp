#include "stock_api.h"
#include "NetGate.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Finnhub's /quote: one HTTP call per symbol, returns just the numbers this
// screen needs (current price, change, % change) with no intraday history -
// exactly what a "price + day change" list needs and nothing more. Its free
// tier covers this comfortably (60 calls/min) for the handful of symbols a
// user is likely to configure, refreshed every several minutes rather than
// continuously. Chosen over Alpha Vantage (free tier cut to 25 calls/DAY as
// of 2024, too tight for even a few symbols on any real refresh interval).
bool fetchStockQuote(const String &symbol, const String &apiKey, StockQuote &out) {
    strncpy(out.symbol, symbol.c_str(), sizeof(out.symbol) - 1);
    out.symbol[sizeof(out.symbol) - 1] = 0;
    out.ok = false;

    if (symbol.length() == 0 || apiKey.length() == 0 || WiFi.status() != WL_CONNECTED) {
        return false;
    }

    // The screen calls this once per configured symbol back-to-back in the
    // same task - without a beat between them, a TLS handshake can start
    // before the previous connection's heap has actually been freed, which
    // fails with "SSL - Memory allocation failed" (mbedTLS needs a sizeable
    // contiguous block per handshake). Same wait DashboardScreen's hourly
    // fetch uses for the same reason.
    for (int w = 0; w < 25 && ESP.getFreeHeap() < 90000; w++)
        vTaskDelay(pdMS_TO_TICKS(300));

    String url = "https://finnhub.io/api/v1/quote?symbol=" + symbol + "&token=" + apiKey;

    NetGate::Lock netLock; // see NetGate.h - only one HTTPS handshake system-wide at a time
    HTTPClient http;
    http.setTimeout(10000);
    http.setConnectTimeout(8000);
    if (!http.begin(url)) return false;

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[Stock] %s: HTTP %d\n", symbol.c_str(), code);
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    JsonDocument filter;
    filter["c"]  = true; // current price
    filter["dp"] = true; // % change
    filter["pc"] = true; // previous close - used only to spot an unknown ticker

    DynamicJsonDocument doc(256);
    DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
    if (err) {
        Serial.printf("[Stock] %s: JSON error %s\n", symbol.c_str(), err.c_str());
        return false;
    }

    float price = doc["c"]  | 0.0f;
    float prevClose = doc["pc"] | 0.0f;
    float changePct = doc["dp"] | 0.0f;

    // Finnhub answers an unknown/delisted ticker with HTTP 200 and every
    // field zeroed rather than an error code - a real stock never has both
    // a zero price and a zero previous close at once, so that combination
    // is the signal to treat as "not found".
    if (price == 0.0f && prevClose == 0.0f) {
        Serial.printf("[Stock] %s: not found\n", symbol.c_str());
        out.notFound = true;
        return false;
    }

    out.price = price;
    out.changePercent = changePct;
    out.ok = true;
    return true;
}
