// MyColors.h
#pragma once
#include <TFT_eSPI.h>

// ==== Custom color mapping for your screen ====
#define MY_BLACK    0xFFFF   // black shows as white
#define MY_WHITE    0x0000   // white shows as black
#define MY_RED      0xFFE0   // red shows as yellow
#define MY_GREEN    0x8010   // green shows as purple
#define MY_BLUE     0x07FF   // blue shows as light blue
#define MY_CYAN     0x001F   // cyan shows as dark blue
#define MY_MAGENTA  0x07E0   // magenta shows as lime green
#define MY_YELLOW   0xF800   // yellow shows as red
#define MY_ORANGE   0xFD20   // orange shows as orange
#define MY_PINK     0x07E0   // pink shows as lime green
#define MY_PURPLE   0x07E0   // purple shows as gradian green
#define MY_BROWN    0xFFE0   // brown shows as yellow gradian
#define MY_NAVY     0x07FF   // navy shows as sky blue
#define MY_SILVER   0x0000   // silver shows as black
#define MY_MAROON   0xC618   // maroon shows as tan/beige
#define MY_AQUA     0xF81F   // aqua shows as pink
#define MY_DEEP_PINK 0xFC18  // deep pink shows as dark green
#define MY_SEA_GREEN 0xAFE5  // sea green shows as dark pink
#define MY_GRAY       0x8410   // gray
#define MY_DARK_GRAY  0x4208   // dark gray

// ==== Theme colours ====
// This panel (a no-name ST7789 clone) INVERTS the value and SWAPS red and
// blue: it shows swapRB(~sent). Every exact primary in the table above fits
// that rule (black, white, red, blue, cyan, magenta, yellow); the rest of
// the table (green, orange, pink...) are hand-picked approximations, not a
// second rule. PANEL_RGB() applies the inverse - pass the real colour you
// want to see (e.g. PANEL_HEX(0xc8d0b8)) and it yields the value to send.
// An earlier version only inverted, which left red and blue swapped.
#define RGB565_OF(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))
#define PANEL_RGB(r, g, b) ((uint16_t)(~RGB565_OF((b), (g), (r)) & 0xFFFF))
#define PANEL_HEX(h)       PANEL_RGB(((h) >> 16) & 0xFF, ((h) >> 8) & 0xFF, (h) & 0xFF)

// Pin the rule to the values verified on the real panel - if a different
// screen ever needs a different rule, these fail the build instead of
// silently recolouring every theme.
static_assert(PANEL_HEX(0x000000) == MY_BLACK,   "panel rule: black");
static_assert(PANEL_HEX(0xFFFFFF) == MY_WHITE,   "panel rule: white");
static_assert(PANEL_HEX(0xFF0000) == MY_RED,     "panel rule: red");
static_assert(PANEL_HEX(0x0000FF) == MY_BLUE,    "panel rule: blue");
static_assert(PANEL_HEX(0x00FFFF) == MY_CYAN,    "panel rule: cyan");
static_assert(PANEL_HEX(0xFF00FF) == MY_MAGENTA, "panel rule: magenta");
static_assert(PANEL_HEX(0xFFFF00) == MY_YELLOW,  "panel rule: yellow");
