#pragma once
#include <Arduino.h>

// Where the Notes screen reads its list from: a single Notion page, shared
// with a free "internal integration" the user creates once at
// notion.so/my-integrations and pastes the secret token for here, plus the
// id (or full URL - see notes_api.cpp's extractPageId()) of the page to
// read.
//
// WHY NOT IN Config: same reasoning as CalendarFeeds (config/Config.h) -
// the token alone runs 50+ characters, and this is written once and read
// once per refresh, so it costs nothing to keep as its own NVS entry
// instead of growing the fixed-size Config blob and its migration ladder.
namespace NotesSource {

    // Load from NVS. Safe to call more than once.
    void begin();

    const String& token();
    const String& pageId();
    int  maxItems();     // how many rows the screen keeps, clamped 5-15
    bool showChecked();  // include to_do blocks that are already checked off

    bool usable();        // token + page both set

    // Editing. Doesn't write to flash; call save() when done.
    void set(const String &token, const String &pageId, int maxItems, bool showChecked);
    void save();

} // namespace NotesSource
