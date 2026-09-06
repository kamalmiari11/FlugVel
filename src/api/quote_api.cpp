#include "quote_api.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ============================================================================
// QUOTE OF THE DAY - with retries and a fallback source
// ============================================================================
// "connection refused" (HTTPClient code -1) from here is almost always a
// transient TLS/TCP connect failure to zenquotes.io: an occasional dropped
// connection, a brief throttle, or an mbedTLS handshake that couldn't get
// enough contiguous heap right after the flight lookup parsed its big JSON
// on the same background task. None of those mean "there is no quote", so:
//
//   * each source is retried a couple of times with a short pause, giving
//     the network / heap a moment to recover;
//   * a fresh WiFiClientSecure is used per attempt (setInsecure - these are
//     public read-only endpoints, no cert pinning) and always closed;
//   * if zenquotes still won't answer, a second provider (dummyjson) is
//     tried before giving up.
//
// Only when EVERY source fails every retry does this return false, and the
// caller then just keeps whatever quote was already showing.
// ============================================================================

static bool parseZenQuotes(const String &payload, QuoteData &data) {
    // [{"q":"...","a":"...","h":"<html>"}]
    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.printf("[Quote] zenquotes JSON error: %s\n", err.c_str());
        return false;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull() || arr.size() == 0) return false;

    const char *q = arr[0]["q"];
    const char *a = arr[0]["a"];
    if (!q || strlen(q) == 0) return false;

    // zenquotes returns this exact string as the "quote" when the caller is
    // being rate-limited - treat it as a failure so the retry / fallback
    // kicks in instead of showing it.
    if (strstr(q, "Too many requests") != nullptr) {
        Serial.println("[Quote] zenquotes rate-limited");
        return false;
    }

    data.text = String(q);
    data.author = (a && strlen(a) > 0) ? String(a) : String("Unknown");
    return true;
}

static bool parseDummyJson(const String &payload, QuoteData &data) {
    // {"id":1,"quote":"...","author":"..."}
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.printf("[Quote] dummyjson JSON error: %s\n", err.c_str());
        return false;
    }

    const char *q = doc["quote"];
    const char *a = doc["author"];
    if (!q || strlen(q) == 0) return false;

    data.text = String(q);
    data.author = (a && strlen(a) > 0) ? String(a) : String("Unknown");
    return true;
}

struct QuoteSource {
    const char *name;
    const char *url;
    bool (*parse)(const String &, QuoteData &);
};

static const QuoteSource kSources[] = {
    { "zenquotes", "https://zenquotes.io/api/today",     parseZenQuotes },
    { "dummyjson", "https://dummyjson.com/quotes/random", parseDummyJson },
};
static const int kSourceCount = sizeof(kSources) / sizeof(kSources[0]);

static const int   ATTEMPTS_PER_SOURCE = 2;
static const int   RETRY_DELAY_MS      = 700;

// One HTTP GET against one source. Returns true only if the body parsed
// into a usable quote.
static bool fetchFromSource(const QuoteSource &src, QuoteData &data) {
    WiFiClientSecure client;
    client.setInsecure();                 // public, read-only endpoint
    client.setHandshakeTimeout(15);       // seconds

    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(10000);
    http.setReuse(false);

    if (!http.begin(client, src.url)) {
        Serial.printf("[Quote] %s: begin() failed\n", src.name);
        return false;
    }
    http.addHeader("Accept", "application/json");
    http.addHeader("User-Agent", "FlugVel/1.0 (+ESP32)");

    int code = http.GET();

    if (code < 0) {
        Serial.printf("[Quote] %s: %s (free heap %u)\n", src.name,
                      http.errorToString(code).c_str(), (unsigned)ESP.getFreeHeap());
        http.end();
        return false;
    }
    if (code != HTTP_CODE_OK) {
        Serial.printf("[Quote] %s: HTTP %d\n", src.name, code);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    if (payload.length() == 0) {
        Serial.printf("[Quote] %s: empty body\n", src.name);
        return false;
    }

    return src.parse(payload, data);
}

bool fetchQuoteOfTheDay(QuoteData &data) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Quote] WiFi not connected");
        return false;
    }

    Serial.println("[Quote] Fetching quote of the day...");

    for (int s = 0; s < kSourceCount; s++) {
        const QuoteSource &src = kSources[s];

        for (int attempt = 1; attempt <= ATTEMPTS_PER_SOURCE; attempt++) {
            if (fetchFromSource(src, data)) {
                Serial.printf("[Quote] Got via %s: \"%s\" - %s\n",
                              src.name, data.text.c_str(), data.author.c_str());
                return true;
            }
            if (attempt < ATTEMPTS_PER_SOURCE) {
                Serial.printf("[Quote] %s: retry %d/%d in %dms\n",
                              src.name, attempt + 1, ATTEMPTS_PER_SOURCE, RETRY_DELAY_MS);
                delay(RETRY_DELAY_MS);
            }
        }
        Serial.printf("[Quote] %s: giving up, trying next source\n", src.name);
    }

    Serial.println("[Quote] All quote sources failed - keeping current quote");
    return false;
}
