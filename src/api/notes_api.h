#pragma once
#include <Arduino.h>

// One line pulled from a Notion page's block children.
struct NoteItem {
    static const int TEXT_LEN = 80;
    static const int ID_LEN   = 40;   // Notion block ids are 36-char UUIDs
    char text[TEXT_LEN];
    char id[ID_LEN];  // this block's id - needed to PATCH a to_do's checked
                       // state back to Notion; unused for any other type
    bool isTodo;     // true for a to_do block - drawn with a checkbox
    bool checked;    // to_do only
    bool isHeading;  // heading_1/2/3 - drawn as a section divider, no marker

    NoteItem() : isTodo(false), checked(false), isHeading(false) { text[0] = 0; id[0] = 0; }
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
//
// todoTotal/todoChecked, if given, are set to the number of to_do blocks
// seen and how many of those were checked off - counted from every to_do
// block encountered, even one dropped from `out` by includeChecked=false,
// so a "3/7 done" progress line stays accurate whether or not finished
// items are actually being shown in the list. Left untouched (not zeroed)
// when null.
int fetchNotionNotes(NoteItem *out, int maxItems, const String &token,
                      const String &pageRef, bool includeChecked,
                      int *todoTotal = nullptr, int *todoChecked = nullptr);

// Flips a single to_do block's checked state on Notion itself (a PATCH,
// not a fetch) - used when the on-device screen lets you tick something
// off directly. The integration token needs "Insert content"/"Update
// content" capability for this to succeed, not just read access; a token
// created read-only gets a 401/403 same as a bad token would, and the
// caller should treat both the same way (revert the optimistic UI change
// quietly, no fetchNotionNotes() error state is triggered by this).
// Returns false on any failure - no WiFi, bad token/permissions, bad
// blockId, or a non-2xx response.
bool setNotionTodoChecked(const String &token, const String &blockId, bool checked);
