#include "PlaneTrackerScreen.h"
#include "../config/Config.h"
#include "../MyColors.h"
#include "../ui/Theme.h"
#include "../ui/Units.h"
#include <math.h>

// Default display orientation, same idea as the old OLED build's
// DISPLAY_BEARING - overridable per-device via setDisplayBearing().

// ═════════════════════════════════════════════════════
// BITMAPS (24x24, 3 bytes per row, MSB first) - ported as-is from the
// original OLED build. Actual airplane silhouette (nose/fuselage/wings/
// tail), one per 8-way heading.
// ═════════════════════════════════════════════════════
#define PLANE_W 24
#define PLANE_H 24

// Regenerated from a single canonical top-down airliner silhouette (clean
// line-icon style: pointed nose, swept wings, tailplane, notched tail),
// rotated 45 degrees per heading so all 8 are perfectly consistent. See
// scratchpad/genplane.js for the generator.
static const uint8_t planeBitmapN[72] PROGMEM = {
    0x00,0x00,0x00, 0x00,0x10,0x00, 0x00,0x10,0x00, 0x00,0x38,0x00,
    0x00,0x38,0x00, 0x00,0x38,0x00, 0x00,0x38,0x00, 0x00,0x38,0x00,
    0x00,0x7C,0x00, 0x00,0xFE,0x00, 0x01,0xFF,0x00, 0x03,0xFF,0x80,
    0x0F,0xFF,0xE0, 0x1F,0xFF,0xF0, 0x3C,0x7C,0x78, 0x30,0x7C,0x18,
    0x00,0x7C,0x00, 0x00,0x7C,0x00, 0x00,0x7C,0x00, 0x01,0xFF,0x00,
    0x03,0x93,0x80, 0x02,0x10,0x80, 0x00,0x00,0x00, 0x00,0x00,0x00,
};

static const uint8_t planeBitmapNE[72] PROGMEM = {
    0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
    0x00,0x00,0x20, 0x00,0x01,0xC0, 0x00,0x03,0xC0, 0x3F,0x87,0xC0,
    0x3F,0xFF,0x80, 0x0F,0xFF,0x00, 0x01,0xFE,0x00, 0x00,0x7E,0x00,
    0x00,0xFE,0x00, 0x01,0xFE,0x00, 0xFF,0xEF,0x00, 0x3F,0xCF,0x00,
    0x0F,0x87,0x00, 0x0F,0x87,0x00, 0x0F,0x87,0x00, 0x01,0x83,0x00,
    0x01,0x83,0x00, 0x00,0x80,0x00, 0x00,0x80,0x00, 0x00,0x00,0x00,
};

static const uint8_t planeBitmapE[72] PROGMEM = {
    0x00,0x00,0x00, 0x00,0x00,0x00, 0x01,0x80,0x00, 0x01,0xC0,0x00,
    0x00,0xE0,0x00, 0x00,0xE0,0x00, 0x60,0x70,0x00, 0x30,0x78,0x00,
    0x30,0x7C,0x00, 0x1F,0xFE,0x00, 0x1F,0xFF,0xF0, 0x7F,0xFF,0xFC,
    0x1F,0xFF,0xF0, 0x1F,0xFE,0x00, 0x30,0x7C,0x00, 0x30,0x78,0x00,
    0x60,0x70,0x00, 0x00,0xE0,0x00, 0x00,0xE0,0x00, 0x01,0xC0,0x00,
    0x01,0x80,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
};

static const uint8_t planeBitmapSE[72] PROGMEM = {
    0x00,0x80,0x00, 0x00,0x80,0x00, 0x01,0x83,0x00, 0x01,0x83,0x00,
    0x0F,0x87,0x00, 0x0F,0x87,0x00, 0x0F,0x87,0x00, 0x3F,0xCF,0x00,
    0xFF,0xEF,0x00, 0x01,0xFE,0x00, 0x00,0xFE,0x00, 0x00,0x7E,0x00,
    0x01,0xFE,0x00, 0x0F,0xFF,0x00, 0x3F,0xFF,0x80, 0x3F,0x87,0xC0,
    0x00,0x03,0xC0, 0x00,0x01,0xC0, 0x00,0x00,0x20, 0x00,0x00,0x00,
    0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
};

static const uint8_t planeBitmapS[72] PROGMEM = {
    0x00,0x00,0x00, 0x02,0x10,0x80, 0x03,0x93,0x80, 0x01,0xFF,0x00,
    0x00,0x7C,0x00, 0x00,0x7C,0x00, 0x00,0x7C,0x00, 0x30,0x7C,0x18,
    0x3C,0x7C,0x78, 0x1F,0xFF,0xF0, 0x0F,0xFF,0xE0, 0x03,0xFF,0x80,
    0x01,0xFF,0x00, 0x00,0xFE,0x00, 0x00,0x7C,0x00, 0x00,0x38,0x00,
    0x00,0x38,0x00, 0x00,0x38,0x00, 0x00,0x38,0x00, 0x00,0x38,0x00,
    0x00,0x10,0x00, 0x00,0x10,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
};

static const uint8_t planeBitmapSW[72] PROGMEM = {
    0x00,0x02,0x00, 0x00,0x02,0x00, 0x01,0x83,0x00, 0x01,0x83,0x00,
    0x01,0xC3,0xE0, 0x01,0xC3,0xE0, 0x01,0xC3,0xE0, 0x01,0xE7,0xF8,
    0x01,0xEF,0xFE, 0x00,0xFF,0x00, 0x00,0xFE,0x00, 0x00,0xFC,0x00,
    0x00,0xFF,0x00, 0x01,0xFF,0xE0, 0x03,0xFF,0xF8, 0x07,0xC3,0xF8,
    0x07,0x80,0x00, 0x07,0x00,0x00, 0x08,0x00,0x00, 0x00,0x00,0x00,
    0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
};

static const uint8_t planeBitmapW[72] PROGMEM = {
    0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x03,0x00, 0x00,0x07,0x00,
    0x00,0x0E,0x00, 0x00,0x0E,0x00, 0x00,0x1C,0x0C, 0x00,0x3C,0x18,
    0x00,0x7C,0x18, 0x00,0xFF,0xF0, 0x1F,0xFF,0xF0, 0x7F,0xFF,0xFC,
    0x1F,0xFF,0xF0, 0x00,0xFF,0xF0, 0x00,0x7C,0x18, 0x00,0x3C,0x18,
    0x00,0x1C,0x0C, 0x00,0x0E,0x00, 0x00,0x0E,0x00, 0x00,0x07,0x00,
    0x00,0x03,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
};

static const uint8_t planeBitmapNW[72] PROGMEM = {
    0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
    0x08,0x00,0x00, 0x07,0x00,0x00, 0x07,0x80,0x00, 0x07,0xC3,0xF8,
    0x03,0xFF,0xF8, 0x01,0xFF,0xE0, 0x00,0xFF,0x00, 0x00,0xFC,0x00,
    0x00,0xFE,0x00, 0x00,0xFF,0x00, 0x01,0xEF,0xFE, 0x01,0xE7,0xF8,
    0x01,0xC3,0xE0, 0x01,0xC3,0xE0, 0x01,0xC3,0xE0, 0x01,0x83,0x00,
    0x01,0x83,0x00, 0x00,0x02,0x00, 0x00,0x02,0x00, 0x00,0x00,0x00,
};

// ═════════════════════════════════════════════════════
// HEADING → 8 WAY (same buckets as the OLED build)
// ═════════════════════════════════════════════════════
enum PlaneDir { DIR_N, DIR_NE, DIR_E, DIR_SE, DIR_S, DIR_SW, DIR_W, DIR_NW };

static PlaneDir headingToDir(float h) {
    h = fmod(h, 360.0f);
    if (h < 0) h += 360;

    if (h < 22.5 || h >= 337.5) return DIR_N;
    if (h < 67.5)  return DIR_NE;
    if (h < 112.5) return DIR_E;
    if (h < 157.5) return DIR_SE;
    if (h < 202.5) return DIR_S;
    if (h < 247.5) return DIR_SW;
    if (h < 292.5) return DIR_W;
    return DIR_NW;
}

static const uint8_t* getBitmap(PlaneDir d) {
    switch (d) {
        case DIR_N:  return planeBitmapN;
        case DIR_NE: return planeBitmapNE;
        case DIR_E:  return planeBitmapE;
        case DIR_SE: return planeBitmapSE;
        case DIR_S:  return planeBitmapS;
        case DIR_SW: return planeBitmapSW;
        case DIR_W:  return planeBitmapW;
        case DIR_NW: return planeBitmapNW;
    }
    return planeBitmapN;
}

PlaneTrackerScreen::PlaneTrackerScreen(TFT_eSPI* display)
    : Screen(display), _hasFlight(false), _showAnimation(false),
      _animationStartTime(0), _lastFlightTime(0),
      kmlX(50), kmlY(50), kmlDX(2), kmlDY(2), lastKmlUpdate(0),
      _lastDrawnFlight(""), _bounceLabel("KML"),
      _borderX(0), _borderY(0), _borderW(0), _borderH(0), _borderDrawn(false),
      _displayBearing(270.0f)
{
}

void PlaneTrackerScreen::setBounceText(const String& text) {
    if (text.length() == 0) return; // keep default ("KML")
    _bounceLabel = text;
}

void PlaneTrackerScreen::setDisplayBearing(float bearingDeg) {
    if (bearingDeg < 0.0f || bearingDeg >= 360.0f) return; // keep default (270)
    _displayBearing = bearingDeg;
}

void PlaneTrackerScreen::setQuote(const String& text, const String& author) {
    _quoteText = text;
    _quoteAuthor = author;

    // Only actually paint it if this screen has already been init()'d at
    // least once (border geometry, and therefore the quote strip's
    // position, isn't known until then) - init() itself calls drawQuote()
    // afterward, so the stored text/author still gets shown once we do
    // visit this screen.
    if (_borderDrawn) drawQuote();
}

// Draws _quoteText/_quoteAuthor in the strip of space below the border box
// (between the box's bottom edge and the physical screen bottom). This
// area is never touched by clearContentArea() (interior of the box only)
// or by draw()'s per-frame flight/bounce-text redraws, so once painted it
// just sits there undisturbed until the next quote comes in or init() runs.
void PlaneTrackerScreen::drawQuote() {
    if (!ConfigStore::get().planeQuote) return;

    int qy = _borderY + _borderH + 4;
    int qh = tft->height() - qy - 2;
    int qx = _borderX;
    int qw = _borderW;

    if (qh < 10 || qw < 10) return; // not enough room on this panel, skip silently

    const Theme &theme = ThemeManager::current();
    tft->fillRect(qx, qy, qw, qh, theme.bg);

    if (_quoteText.length() == 0) return; // nothing fetched yet - leave it blank

    tft->setTextSize(1);
    tft->setTextColor(theme.fgDim, theme.bg);

    int lineH = tft->fontHeight() + 2;
    int maxLines = qh / lineH;
    if (maxLines < 1) maxLines = 1;

    // Reserve the last line for the "- Author" credit when there's room
    // for more than just the quote itself.
    int maxQuoteLines = (maxLines > 1) ? (maxLines - 1) : maxLines;

    String remaining = "\"" + _quoteText + "\"";
    int cy = qy;
    int linesDrawn = 0;

    while (remaining.length() > 0 && linesDrawn < maxQuoteLines) {
        // Find the longest prefix of what's left that still fits the
        // available width, then back off to the last space so words never
        // get cut in half mid-line.
        int fitLen = remaining.length();
        while (fitLen > 0 && tft->textWidth(remaining.substring(0, fitLen)) > qw - 4) {
            fitLen--;
        }
        if (fitLen < (int)remaining.length() && fitLen > 0) {
            int lastSpace = remaining.substring(0, fitLen).lastIndexOf(' ');
            if (lastSpace > 0) fitLen = lastSpace;
        }
        if (fitLen <= 0) fitLen = remaining.length(); // safety net: one unbreakable "word"

        bool isLastAllowedLine = (linesDrawn == maxQuoteLines - 1) && (fitLen < (int)remaining.length());

        String line = remaining.substring(0, fitLen);
        line.trim();

        if (isLastAllowedLine) {
            // Ran out of vertical space mid-quote - ellipsize so it's
            // obviously truncated rather than looking like the sentence
            // just stops.
            while (line.length() > 3 && tft->textWidth(line + "...") > qw - 4) {
                line.remove(line.length() - 1);
            }
            line += "...";
        }

        tft->setCursor(qx + 2, cy);
        tft->println(line);

        remaining = remaining.substring(fitLen);
        remaining.trim();
        cy += lineH;
        linesDrawn++;

        if (isLastAllowedLine) break;
    }

    // "- Author", right-aligned on its own line under the quote.
    if (_quoteAuthor.length() > 0 && cy + lineH <= qy + qh) {
        String authorLine = "- " + _quoteAuthor;
        int16_t authorW = tft->textWidth(authorLine);
        int ax = qx + qw - authorW - 4;
        if (ax < qx + 2) ax = qx + 2;

        tft->setCursor(ax, cy);
        tft->println(authorLine);
    }
}

void PlaneTrackerScreen::init() {
    Serial.println("[PlaneTrackerScreen] Initialized");
    _showAnimation = false;
    _lastDrawnFlight = "";  // Force redraw on init

    // Compute the border box from the actual panel size and draw it once
    // per visit to this screen. It is never redrawn again after this - see
    // clearContentArea(), which stays strictly inside it.
    _borderX = KML_MARGIN_LEFT - 4;
    _borderY = KML_MARGIN_TOP - 4;
    _borderW = tft->width() - KML_MARGIN_LEFT - KML_MARGIN_RIGHT + 8;
    _borderH = tft->height() - KML_MARGIN_TOP - KML_MARGIN_BOTTOM + 8;

    const Theme &theme = ThemeManager::current();
    tft->fillRect(0, HEADER_HEIGHT, tft->width(), tft->height() - HEADER_HEIGHT, theme.bg);
    drawBorder();
    _borderDrawn = true;

    // The fillRect above just wiped the quote-of-the-day strip along with
    // everything else - repaint whatever quote we already have (if any),
    // so switching away from and back to this screen doesn't lose it.
    drawQuote();
}

void PlaneTrackerScreen::update() {
    // Update bouncing text if no flight
    if (!_hasFlight) {
        unsigned long now = millis();
        if (now - lastKmlUpdate > 50) {  // Update position every 50ms
            updateKMLPosition();
            lastKmlUpdate = now;
        }
    }
}

// Clears only the interior of the border box (inset 2px so the border line
// itself is never touched) - never the header strip, never the border.
void PlaneTrackerScreen::clearContentArea() {
    tft->fillRect(_borderX + 2, _borderY + 2, _borderW - 4, _borderH - 4, ThemeManager::current().bg);
}

void PlaneTrackerScreen::draw() {
    // SMOOTH ANIMATION WITHOUT FLICKERING:
    // Only do a real redraw when flight status changes. The header (top
    // status bar) and the border box are never touched here at all - see
    // clearContentArea() and init(). Everything below only clears/redraws
    // inside the border.

    if (_hasFlight) {
        // Flight mode: Only redraw when flight callsign changes
        if (_lastDrawnFlight != _currentFlight.callsign) {
            _lastDrawnFlight = _currentFlight.callsign;

            clearContentArea();  // wipe whatever was showing before (old info / bounce text)
            if (ConfigStore::get().planeFlyover) {
                animatePlane();      // flyover: enters from outside the box, exits the other side, then is gone
                clearContentArea();  // remove the plane before the info text appears
            }
            drawFlightInfo();
        }
    } else {
        // NO FLIGHT MODE - Smooth bouncing-text animation
        static int lastKmlXDrawn = 50;
        static int lastKmlYDrawn = 50;
        static bool kmlDrawnOnce = false;

        const Theme &theme = ThemeManager::current();

        // On first frame or when transitioning to this state
        if (_lastDrawnFlight != "NO_FLIGHT") {
            _lastDrawnFlight = "NO_FLIGHT";
            kmlDrawnOnce = false;

            clearContentArea(); // wipe whatever flight info / animation trail was showing
        }

        // Compute the EXACT bounding box of the bounce-text glyphs at the
        // text size we draw it at, so the erase rect always fully covers
        // the previously drawn text - this is what fixes the trailing line.
        tft->setTextSize(2);
        int16_t textW = tft->textWidth(_bounceLabel);
        int16_t textH = tft->fontHeight();
        const int margin = 2; // small safety margin against sub-pixel/AA edges

        if (kmlDrawnOnce) {
            tft->fillRect(lastKmlXDrawn - margin, lastKmlYDrawn - margin,
                          textW + margin * 2, textH + margin * 2, theme.bg);
        }

        // Draw new bounce text at new position
        tft->setTextColor(theme.accent);
        tft->setCursor(kmlX, kmlY);
        tft->println(_bounceLabel);

        // Remember where we drew it
        lastKmlXDrawn = kmlX;
        lastKmlYDrawn = kmlY;
        kmlDrawnOnce = true;
    }
}

void PlaneTrackerScreen::setFlight(const Flight& flight) {
    _currentFlight = flight;
    _hasFlight = true;
    _lastFlightTime = millis();
    _showAnimation = true;
}

void PlaneTrackerScreen::clearFlight() {
    _hasFlight = false; // draw() will transition to the bouncing-text state next frame
}

void PlaneTrackerScreen::onButtonPress() {
    if (_hasFlight) {
        Serial.print("[PlaneTrackerScreen] Flight selected: ");
        Serial.println(_currentFlight.callsign);
        // Could open FlightAware link here
    }
}

void PlaneTrackerScreen::getActionLegend(String &line1, String &line2) const {
    // No encoder-rotation behavior on this screen at all.
    line1 = "";
    // onButtonPress() above doesn't do anything user-visible yet (just a
    // serial log, with a "could open a link" TODO) - no button hint to
    // show until that's actually implemented, in either state.
    line2 = "";
}

void PlaneTrackerScreen::triggerAnimation() {
    _showAnimation = true;
    _animationStartTime = millis();
}

// 8-point compass label for a real-world heading in degrees.
static const char* cardinal8(float deg) {
    while (deg < 0) deg += 360.0f;
    while (deg >= 360.0f) deg -= 360.0f;
    static const char* pts[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    return pts[(int)((deg + 22.5f) / 45.0f) % 8];
}

// Option B layout: callsign header + hairline, then the four stats in a
// 2x2 grid so the full width of the border box is used instead of a
// single left-aligned column. All coordinates are relative to the border
// box (_borderX/_borderY) so this stays correct if the box geometry ever
// changes.
void PlaneTrackerScreen::drawFlightInfo() {
    const Theme &theme = ThemeManager::current();
    tft->setTextDatum(TL_DATUM);

    const int left   = _borderX + 18;              // left column x
    const int rightC = _borderX + _borderW / 2;    // right column x (only SPEED + FROM live here)
    const int valMaxW = _borderX + _borderW - 6 - rightC; // clip width for the FROM value
    const int top    = _borderY;

    // Trims a string with a trailing ellipsis until it fits maxW at the
    // current text size - used for country names that would overrun the
    // right column.
    auto fitted = [&](const String& s, int maxW) -> String {
        if (tft->textWidth(s) <= maxW) return s;
        String t = s;
        while (t.length() > 1 && tft->textWidth(t + "..") > maxW)
            t.remove(t.length() - 1);
        return t + "..";
    };

    // ---- Callsign header ----
    tft->setTextSize(2);
    tft->setTextColor(theme.accent2, theme.bg);
    tft->setCursor(left, top + 10);
    tft->print(_currentFlight.callsign.length() ? _currentFlight.callsign : String("--"));

    tft->drawFastHLine(_borderX + 14, top + 34, _borderW - 28, theme.rule);

    // ---- 2x2 stat grid ----
    // label: small + dim, value: size-2 + full-contrast, drawn just below.
    auto cell = [&](int x, int y, const char* label, const String& value, bool clip) {
        tft->setTextSize(1);
        tft->setTextColor(theme.fgDim, theme.bg);
        tft->setCursor(x, y);
        tft->print(label);

        tft->setTextSize(2);
        tft->setTextColor(theme.fg, theme.bg);
        tft->setCursor(x, y + 14);
        tft->print(clip ? fitted(value, valMaxW) : value);
    };

    char buf[20];

    snprintf(buf, sizeof(buf), "%d %s",
             Units::altitude((int)_currentFlight.altitude), Units::altitudeUnit());
    cell(left, top + 48, "ALTITUDE", buf, false);

    snprintf(buf, sizeof(buf), "%d %s",
             Units::speed(_currentFlight.speed), Units::speedUnit());
    cell(rightC, top + 48, "SPEED", buf, false);

    snprintf(buf, sizeof(buf), "%d\xF8 %s",
             (int)_currentFlight.heading, cardinal8(_currentFlight.heading));
    cell(left, top + 92, "HEADING", buf, false);

    cell(rightC, top + 92, "FROM",
         _currentFlight.originCountry.length() ? _currentFlight.originCountry : String("Unknown"),
         true);
}

void PlaneTrackerScreen::updateKMLPosition() {
    int screenWidth = tft->width();
    int screenHeight = tft->height();

    // Fixed max size for up to 3 characters at textSize 2 (see draw()) -
    // works for "KML" or whatever the user configured, since it's always
    // capped at 3 letters.
    const int textW = 36;  // 3 chars * 6px * scale 2
    const int textH = 16;  // 8px * scale 2

    kmlX += kmlDX;
    kmlY += kmlDY;

    // Bounce off edges with margin (top margin also clears the header bar)
    if (kmlX <= KML_MARGIN_LEFT || kmlX + textW >= screenWidth - KML_MARGIN_RIGHT) {
        kmlDX = -kmlDX;
        kmlX = constrain(kmlX, KML_MARGIN_LEFT, screenWidth - KML_MARGIN_RIGHT - textW);
    }

    if (kmlY <= KML_MARGIN_TOP || kmlY + textH >= screenHeight - KML_MARGIN_BOTTOM) {
        kmlDY = -kmlDY;
        kmlY = constrain(kmlY, KML_MARGIN_TOP, screenHeight - KML_MARGIN_BOTTOM - textH);
    }
}

// Draws a box around exactly the area the bounce text moves within (see the
// KML_MARGIN_* constants) - called exactly once per visit to this screen,
// from init(). Never called again after that (see draw()/animatePlane(),
// which only ever clear strictly inside it via clearContentArea()).
void PlaneTrackerScreen::drawBorder() {
    tft->drawRect(_borderX, _borderY, _borderW, _borderH, ThemeManager::current().fg);
}

// Rotates a real-world compass heading to be relative to the way the
// screen itself is facing, same as the old OLED build's getRelativeHeading.
float PlaneTrackerScreen::getRelativeHeading(float h) const {
    float r = h - _displayBearing;
    while (r < 0) r += 360;
    while (r >= 360) r -= 360;
    return r;
}

// Flies the 8-way plane bitmap across the inside of the border box, in the
// direction the real flight is heading (adjusted for _displayBearing).
// Starts fully outside the box on the side the flight is coming FROM,
// crosses all the way through, and ends fully outside the opposite side -
// it never stops mid-box. Only the small area around the bitmap is
// cleared/redrawn each frame (not the whole box), so it's smooth and never
// touches the header or the border line.
void PlaneTrackerScreen::animatePlane() {
    float rel = getRelativeHeading(_currentFlight.heading);
    const uint8_t* bmp = getBitmap(headingToDir(rel));

    float angle = rel * (PI / 180.0f);
    float vx = sin(angle);
    float vy = -cos(angle);

    // Animate within the border box's interior, not the whole screen -
    // this keeps the flight path (and its start/end "off-screen" points)
    // consistent with the box that's actually drawn on screen.
    int boxX = _borderX + 2;
    int boxY = _borderY + 2;
    int boxW = _borderW - 4;
    int boxH = _borderH - 4;
    int cx = boxX + boxW / 2;
    int cy = boxY + boxH / 2;
    int halfW = boxW / 2;
    int halfH = boxH / 2;

    // Distance from center to just outside whichever edge of the box the
    // plane is approaching from.
    float travel = 0;
    if (fabs(vx) > 0.0001f) travel = max(travel, (float)halfW / fabs(vx));
    if (fabs(vy) > 0.0001f) travel = max(travel, (float)halfH / fabs(vy));
    travel += PLANE_W; // clear margin so the bitmap starts/ends fully outside the box

    float startX = cx - vx * travel;
    float startY = cy - vy * travel;
    float endX   = cx + vx * travel;
    float endY   = cy + vy * travel;

    float dist = sqrt((endX - startX) * (endX - startX) + (endY - startY) * (endY - startY));

    // Scale the frame count to the path length so the plane always moves at
    // the same ON-SCREEN speed. A fixed frame count made near-vertical
    // flights (short path across the landscape box) crawl and diagonal
    // ones (long path) zip across, since each covered the same number of
    // steps in the same wall-clock time. Now px-per-frame is constant and
    // the total duration varies instead; clamped so a very short path still
    // animates for a beat and a long one doesn't drag.
    const float PX_PER_FRAME = 4.5f;
    int frames = constrain((int)(dist / PX_PER_FRAME), 28, 130);
    const float step = dist / frames;

    const Theme &theme = ThemeManager::current();
    float px = startX, py = startY;
    int lastPx = -10000, lastPy = -10000;
    const int frameDelay = 30; // ms - slower, more visible flyover (was 12ms)

    // The plane deliberately starts/ends OUTSIDE the box (that's the point -
    // it flies in from off-box and exits the other side). That means its
    // erase/draw rects can overlap the border line, or even go past it
    // entirely. Clip all drawing to strictly inside the box interior so the
    // border itself is never touched, no matter where the bitmap is.
    tft->setViewport(boxX, boxY, boxW, boxH);

    for (int i = 0; i < frames; i++) {
        px += vx * step;
        py += vy * step;

        // Erase only the small area the bitmap previously occupied, not
        // the whole box - this is what makes the motion smooth instead of
        // a full-screen flash every frame. Coordinates below are still in
        // absolute screen space; setViewport() clips them for us.
        if (lastPx > -10000) {
            tft->fillRect(lastPx - PLANE_W / 2 - 1, lastPy - PLANE_H / 2 - 1, PLANE_W + 2, PLANE_H + 2, theme.bg);
        }

        tft->drawBitmap((int)px - PLANE_W / 2, (int)py - PLANE_H / 2, bmp, PLANE_W, PLANE_H, theme.fg);

        lastPx = (int)px;
        lastPy = (int)py;

        // This loop blocks for frames * frameDelay (a couple seconds) -
        // without this, the header's clock would visibly freeze for the
        // whole flyover, since ScreenManager only redraws the header AFTER
        // this whole function returns. The viewport only clips drawing to
        // the box, so it has to be lifted for the header (which lives
        // above the box, in the top 20px strip) and restored after.
        if (_header) {
            tft->resetViewport();
            _header->draw(getName(), _pagerIndex, _pagerCount);
            tft->setViewport(boxX, boxY, boxW, boxH);
        }

        delay(frameDelay);
    }

    // Remove the plane at its final (off-box) position so nothing lingers.
    tft->fillRect(lastPx - PLANE_W / 2 - 1, lastPy - PLANE_H / 2 - 1, PLANE_W + 2, PLANE_H + 2, theme.bg);

    // Restore full-screen drawing for everything else (clearContentArea(),
    // drawFlightInfo(), etc. all assume the whole panel is drawable again).
    tft->resetViewport();
}