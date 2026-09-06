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

// ==== "Mono dot-matrix" theme palette ====
// Derived the same way MY_BLACK/MY_WHITE above already were: MY_BLACK =
// 0xFFFF and MY_WHITE = 0x0000 are exact 16-bit bitwise complements of
// plain black/white, and that's the only relationship in this file that
// can be verified from the existing constants alone (the other MY_* colors
// don't reduce to a single clean rule). So these new tones are generated
// with that same "send the bitwise complement of the RGB565 value you
// actually want" transform, computed from the palette's real hex colors:
//   MY_MONO_BG      <- #c8d0b8 (pale sage)
//   MY_MONO_FG      <- #2a2e22 (dark olive / near-black)
//   MY_MONO_FG_DIM  <- #4a5038 (muted olive-gray)
//   MY_MONO_RULE    <- #8a9078 (soft gray-green hairline)
// NOTE: this hasn't been verified against the physical panel - if any of
// these render inverted/off relative to the rest of the UI, re-derive them
// following the same steps (RGB888 -> RGB565 -> bitwise complement) or
// swap to the not-inverted RGB565 value instead.
#define MY_MONO_BG        0x3168   // pale sage background
#define MY_MONO_FG        0xD69B   // dark olive foreground text/lines
#define MY_MONO_FG_DIM    0xB578   // muted olive-gray secondary text
#define MY_MONO_RULE      0x7370   // soft gray-green hairline rule
#define MY_MONO_SELECT_BG MY_MONO_FG   // selected-row fill: dark olive...
#define MY_MONO_SELECT_FG MY_MONO_BG   // ...with light (inverted) text

// ==== Extra themes ====
// This panel wants the BITWISE COMPLEMENT of the RGB565 value you actually
// want on screen (that's the same transform MY_BLACK/MY_WHITE and the Mono
// palette above already use). PANEL_RGB() bakes that in: pass the real
// 8-bit-per-channel colour you want to see and it yields the value to store.
#define RGB565_OF(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))
#define PANEL_RGB(r, g, b) ((uint16_t)(~RGB565_OF((r), (g), (b)) & 0xFFFF))

// Amber Terminal - warm CRT amber on near-black.
#define MY_AMBER_BG        PANEL_RGB(0x1A, 0x12, 0x00)
#define MY_AMBER_FG        PANEL_RGB(0xFF, 0xB0, 0x00)
#define MY_AMBER_FG_DIM    PANEL_RGB(0x9C, 0x6B, 0x00)
#define MY_AMBER_RULE      PANEL_RGB(0x5A, 0x3F, 0x00)
#define MY_AMBER_ACCENT    PANEL_RGB(0xFF, 0x70, 0x00)
#define MY_AMBER_ACCENT2   PANEL_RGB(0xB8, 0xFF, 0x00)
#define MY_AMBER_DANGER    PANEL_RGB(0xFF, 0x30, 0x30)

// Green Phosphor - classic terminal green.
#define MY_PHOS_BG         PANEL_RGB(0x00, 0x12, 0x00)
#define MY_PHOS_FG         PANEL_RGB(0x33, 0xFF, 0x77)
#define MY_PHOS_FG_DIM     PANEL_RGB(0x18, 0x9C, 0x47)
#define MY_PHOS_RULE       PANEL_RGB(0x0A, 0x5A, 0x28)
#define MY_PHOS_ACCENT     PANEL_RGB(0xFF, 0xD0, 0x00)
#define MY_PHOS_ACCENT2    PANEL_RGB(0x66, 0xFF, 0xCC)
#define MY_PHOS_DANGER     PANEL_RGB(0xFF, 0x55, 0x55)

// Ice Blue - cool pale blue on deep navy.
#define MY_ICE_BG          PANEL_RGB(0x04, 0x12, 0x1E)
#define MY_ICE_FG          PANEL_RGB(0xCF, 0xEA, 0xFF)
#define MY_ICE_FG_DIM      PANEL_RGB(0x6F, 0x9B, 0xC0)
#define MY_ICE_RULE        PANEL_RGB(0x35, 0x56, 0x6E)
#define MY_ICE_ACCENT      PANEL_RGB(0xFF, 0xCA, 0x3A)
#define MY_ICE_ACCENT2     PANEL_RGB(0x6E, 0xE7, 0xFF)
#define MY_ICE_DANGER      PANEL_RGB(0xFF, 0x5C, 0x7C)