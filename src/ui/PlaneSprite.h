#pragma once
#include <TFT_eSPI.h>

// The one airplane silhouette used everywhere in the firmware - the plane
// screen's flyover and loading indicator, the screen switcher's Plane icon,
// and all three games that fly something around. Before this, each of those
// drew its own idea of a plane (a real bitmap here, a bare triangle there,
// a plain square in Flappy and Paddle Catch) and they all pointed whichever
// way that one caller happened to need.
//
// Two sizes, same silhouette:
//   LARGE (24x24) - all eight compass directions, so a real flight heading
//                   can be drawn exactly. This is the original set the
//                   plane screen has always used.
//   SMALL (16x16) - the four cardinals only, for the game sprites, which
//                   are far too small for the large set to read at. See
//                   small() for what a diagonal does here.
//
// Both sets are drawn with TFT_eSPI::drawBitmap, so only the set bits are
// painted: passing the background color erases exactly the silhouette and
// nothing around it, which is how the movers erase their previous frame.
namespace PlaneSprite {

// Compass direction the plane is pointing, in the same order and with the
// same meaning as the 8-way buckets the plane screen has always used.
enum Dir { N, NE, E, SE, S, SW, W, NW };

static const int LARGE_W = 24;
static const int LARGE_H = 24;
static const int SMALL_W = 16;
static const int SMALL_H = 16;

// Real-world heading in degrees (0 = north, clockwise) to the nearest of
// the eight directions. Wraps and normalizes, so any value is safe.
Dir fromHeading(float headingDeg);

// Raw bitmaps, if a caller needs to hand them to drawBitmap itself.
const uint8_t* large(Dir d);

// The small set only has the four cardinals - a 16px diagonal silhouette
// is a smudge, and nothing needs one: every caller of the small set flies
// its sprite along a fixed axis. A diagonal passed here snaps to the
// nearest cardinal (NE -> N, and so on round the compass) rather than
// failing, so this stays safe to call with a live heading.
const uint8_t* small(Dir d);

// x/y are the top-left corner of the sprite box; the Centered variants
// take the middle of it instead, which is what most callers actually have
// (a lane center, a tile center, an object's position).
void drawLarge(TFT_eSPI* tft, int x, int y, Dir d, uint16_t color);
void drawSmall(TFT_eSPI* tft, int x, int y, Dir d, uint16_t color);
void drawLargeCentered(TFT_eSPI* tft, int cx, int cy, Dir d, uint16_t color);
void drawSmallCentered(TFT_eSPI* tft, int cx, int cy, Dir d, uint16_t color);

} // namespace PlaneSprite
