#include "notes_api.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ctype.h>
#include <string.h>

namespace {

// Notion page URLs (https://www.notion.so/Workspace/My-Page-<32 hex>?pvs=4)
// and share links all end in the bare 32-character id, so rather than
// parsing the URL shape, just keep the last 32 hex characters seen
// anywhere in the string once every hyphen is stripped out. A bare id
// pasted on its own (with or without its usual dashes) reduces to the same
// thing.
String extractPageId(const String &raw) {
    String s = raw;
    s.trim();
    int q = s.indexOf('?');
    if (q >= 0) s = s.substring(0, q);

    String hex;
    hex.reserve(s.length());
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (isxdigit((unsigned char)c)) hex += c;
    }
    if (hex.length() < 32) return "";
    return hex.substring(hex.length() - 32);
}

// Every block type this screen understands keeps its text under
// <type>.rich_text[].plain_text. Concatenates every run into `out` - a
// no-heap fixed-buffer append, same reasoning as calendar_api.cpp's
// unescapeInto().
void collectText(JsonObject block, const char *type, char *out, size_t cap) {
    JsonArray runs = block[type]["rich_text"];
    size_t w = 0;
    for (JsonObject run : runs) {
        const char *t = run["plain_text"] | "";
        for (size_t i = 0; t[i] && w + 1 < cap; i++) out[w++] = t[i];
    }
    out[w] = 0;
}

} // namespace

int fetchNotionNotes(NoteItem *out, int maxItems, const String &token,
                      const String &pageRef, bool includeChecked,
                      int *todoTotal, int *todoChecked) {
    if (todoTotal)   *todoTotal   = 0;
    if (todoChecked) *todoChecked = 0;
    if (token.length() == 0 || WiFi.status() != WL_CONNECTED) return -1;

    String pageId = extractPageId(pageRef);
    if (pageId.length() != 32) return -1;

    // Ask for a bit more than maxItems since checked-off to-dos may get
    // filtered back out below, and Notion's own default (page_size unset)
    // is only 100 - never a real limit here, just keeps the reply small.
    int wantRaw = maxItems * 2;
    if (wantRaw > 90) wantRaw = 90;
    if (wantRaw < 20) wantRaw = 20;

    String url = "https://api.notion.com/v1/blocks/" + pageId +
                 "/children?page_size=" + String(wantRaw);

    Serial.printf("[Notes] Fetching page %s\n", pageId.c_str());

    HTTPClient http;
    http.setTimeout(10000);
    http.setConnectTimeout(8000);
    if (!http.begin(url)) return -1;   // https:// - HTTPClient auto-uses a secure client, same as the rest of this project
    http.addHeader("Authorization", "Bearer " + token);
    http.addHeader("Notion-Version", "2022-06-28");
    http.addHeader("Accept", "application/json");

    int code = http.GET();
    if (code == 401 || code == 403) { http.end(); return -2; }
    if (code == 404)                { http.end(); return -3; }
    if (code != HTTP_CODE_OK) {
        Serial.printf("[Notes] HTTP %d\n", code);
        http.end();
        return -1;
    }

    String body = http.getString();
    http.end();

    // Only the fields these block types actually use - the rest of a real
    // block (id, created/last_edited time+by, has_children, archived...)
    // would otherwise roughly triple what's parsed for nothing this screen
    // shows.
    JsonDocument filter;
    filter["results"][0]["type"]                                            = true;
    filter["results"][0]["to_do"]["checked"]                                = true;
    filter["results"][0]["to_do"]["rich_text"][0]["plain_text"]             = true;
    filter["results"][0]["paragraph"]["rich_text"][0]["plain_text"]         = true;
    filter["results"][0]["bulleted_list_item"]["rich_text"][0]["plain_text"] = true;
    filter["results"][0]["numbered_list_item"]["rich_text"][0]["plain_text"] = true;
    filter["results"][0]["heading_1"]["rich_text"][0]["plain_text"]         = true;
    filter["results"][0]["heading_2"]["rich_text"][0]["plain_text"]         = true;
    filter["results"][0]["heading_3"]["rich_text"][0]["plain_text"]         = true;
    filter["results"][0]["id"]                                              = true;

    DynamicJsonDocument doc(12288);
    DeserializationError err =
        deserializeJson(doc, body, DeserializationOption::Filter(filter));
    if (err) {
        Serial.printf("[Notes] JSON error: %s\n", err.c_str());
        return -4;
    }

    JsonArray results = doc["results"];
    if (results.isNull()) return -4;

    int n = 0;
    for (JsonObject block : results) {
        if (n >= maxItems) break;
        const char *type = block["type"] | "";
        NoteItem item;
        const char *bid = block["id"] | "";
        strncpy(item.id, bid, sizeof(item.id) - 1);
        item.id[sizeof(item.id) - 1] = 0;

        if (strcmp(type, "to_do") == 0) {
            item.isTodo  = true;
            item.checked = block["to_do"]["checked"] | false;
            // Tallied before the includeChecked filter below, so a
            // finished item still counts toward the total even when it's
            // about to be dropped from the visible list.
            if (todoTotal)                    (*todoTotal)++;
            if (item.checked && todoChecked)  (*todoChecked)++;
            if (item.checked && !includeChecked) continue;
            collectText(block, "to_do", item.text, sizeof(item.text));
        } else if (strcmp(type, "paragraph") == 0) {
            collectText(block, "paragraph", item.text, sizeof(item.text));
        } else if (strcmp(type, "bulleted_list_item") == 0) {
            collectText(block, "bulleted_list_item", item.text, sizeof(item.text));
        } else if (strcmp(type, "numbered_list_item") == 0) {
            collectText(block, "numbered_list_item", item.text, sizeof(item.text));
        } else if (strcmp(type, "heading_1") == 0) {
            item.isHeading = true;
            collectText(block, "heading_1", item.text, sizeof(item.text));
        } else if (strcmp(type, "heading_2") == 0) {
            item.isHeading = true;
            collectText(block, "heading_2", item.text, sizeof(item.text));
        } else if (strcmp(type, "heading_3") == 0) {
            item.isHeading = true;
            collectText(block, "heading_3", item.text, sizeof(item.text));
        } else {
            continue;   // divider, image, table, toggle, etc. - nothing to show
        }

        if (item.text[0] == 0) continue;   // empty spacer block
        out[n++] = item;
    }

    Serial.printf("[Notes] %d items (of %d blocks returned)\n", n, (int)results.size());
    return n;
}

bool setNotionTodoChecked(const String &token, const String &blockId, bool checked) {
    if (token.length() == 0 || blockId.length() == 0) return false;
    if (WiFi.status() != WL_CONNECTED) return false;

    String url = "https://api.notion.com/v1/blocks/" + blockId;

    HTTPClient http;
    http.setTimeout(10000);
    http.setConnectTimeout(8000);
    if (!http.begin(url)) return false;
    http.addHeader("Authorization", "Bearer " + token);
    http.addHeader("Notion-Version", "2022-06-28");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");

    String body = String("{\"to_do\":{\"checked\":") + (checked ? "true" : "false") + "}}";
    // sendRequest() rather than the PATCH() convenience wrapper - it's the
    // one HTTPClient entry point guaranteed present across core versions.
    int code = http.sendRequest("PATCH", body);
    http.end();

    if (code != HTTP_CODE_OK) {
        Serial.printf("[Notes] checkbox PATCH failed, HTTP %d\n", code);
        return false;
    }
    return true;
}
