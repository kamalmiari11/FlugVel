#pragma once
#include <Arduino.h>

// One line pulled from a Notion page's block children.
struct NoteItem {
    static const int TEXT_LEN = 80;
    char text[TEXT_LEN];
    bool isTodo;    // true for a to_do block - drawn with a checkbox
    bool checked;   // to_do only

    NoteItem() : isTodo(false), checked(false) { text[0] = 0; }
};

// Reads the top-level blocks of a Notion page through a free "internal
// integration" token (create one at notion.so/my-integrations, then share
// the page with it from the page's ... menu > Connections - skip that step
// and every request comes back 404, since the integration otherwise can't
// see the page at all). `pageRef` can be the bare 32-character page id or
// the full notion.so URL/share link - the last run of 32 hex characters
// found in it is used either way, so pasting straight from the browser
// address bar works.
//
// Only to_do / bulleted_list_item / numbered_list_item / paragraph /
// heading_1-3 blocks that actually have text are kept; empty spacer
// blocks, dividers, images, embeds etc. are skipped rather than shown as
// blank rows. Several rich-text runs in one block (e.g. a line that's part
// bold) are concatenated into one line.
//
// Returns the number of items written (>= 0, capped at maxItems), or a
// negative code the caller should turn into an explanatory empty state:
//  -1  network / transport error (no connection, timeout, bad response)
//  -2  401/403 - bad token, or the page hasn't been shared with the
//      integration yet
//  -3  404 - page id not found
//  -4  response wasn't the JSON shape expected
int fetchNotionNotes(NoteItem *out, int maxItems, const String &token,
                      const String &pageRef, bool includeChecked);
