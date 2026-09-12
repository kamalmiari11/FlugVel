#include "PlaneTrackerScreen.h"
#include "../config/Config.h"
#include "../MyColors.h"
#include "../ui/Theme.h"
#include "../ui/Units.h"
#include "../ui/PlaneSprite.h"
#include <math.h>

// Default display orientation, same idea as the old OLED build's
// DISPLAY_BEARING - overridable per-device via setDisplayBearing().
// The airplane silhouettes this screen draws (the flyover, and the loading
// indicator while a fetch is outstanding) live in ui/PlaneSprite - they are
// shared with the screen switcher's Plane icon and with the games, so every
// plane in the firmware is the same aircraft pointing the same way. PLANE_W/
// PLANE_H below are just local names for that sprite's size, kept because
// the animation math reads better with them.
#define PLANE_W PlaneSprite::LARGE_W
#define PLANE_H PlaneSprite::LARGE_H

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

    // Flight-checking only runs while this screen is showing (see
    // backgroundNetworkTask() in main.cpp), which fires an immediate fetch
    // the instant that becomes true - so landing here with nothing to show
    // yet means that fetch is now in flight. Skip the indicator if a flight
    // is already showing (e.g. the boot-time prefetch already found one, or
    // we're switching back in before the last result went stale) - that
    // data just keeps showing until the fresh result replaces it.
    _fetching = !_hasFlight;

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

    // A flyover in progress takes priority over everything else below -
    // advance it by at most one frame (only if enough time has passed)
    // and return immediately either way. This used to be a single
    // blocking delay()-driven loop inside animatePlane() that froze
    // encoder/button input, background network work, and the header's own
    // clock for the whole multi-second flyover. Now draw() is called
    // normally on every loop() iteration (see stepPlaneAnimation()), so
    // nothing else in loop() is held up while a plane crosses the screen.
    if (_animInProgress) {
        stepPlaneAnimation();
        return;
    }

    if (_hasFlight) {
        // Flight mode: Only redraw when flight callsign changes
        if (_lastDrawnFlight != _currentFlight.callsign) {
            _lastDrawnFlight = _currentFlight.callsign;

            clearContentArea();  // wipe whatever was showing before (old info / bounce text)
            if (ConfigStore::get().planeFlyover) {
                startPlaneAnimation(); // non-blocking - arms the flyover; stepPlaneAnimation()
                                        // (called above on future draw() calls) finishes it and
                                        // calls drawFlightInfo() itself once the plane is off-box
                return;
            }
            drawFlightInfo();
        }
    } else {
        // NO FLIGHT MODE - either genuinely nothing overhead (bouncing
        // text, exactly as before) or the first fetch since switching onto
        // this screen is still outstanding (_fetching - see setFetching()/
        // init()), in which case a bouncing plane bitmap stands in for it
        // instead, so the screen never looks blank/frozen while that
        // request is in flight. Both share the same kmlX/kmlY bounce
        // position (advanced every 50ms by update()) - only what gets
        // drawn at that position differs.
        static int lastDrawnX = 50;
        static int lastDrawnY = 50;
        static int lastDrawnW = 0;
        static int lastDrawnH = 0;
        static bool drawnOnce = false;

        const Theme &theme = ThemeManager::current();
        const char* stateTag = _fetching ? "FETCHING" : "NO_FLIGHT";
        const int margin = 2; // small safety margin against sub-pixel/AA edges

        // On first frame or when transitioning between flight / fetching /
        // no-flight states.
        if (_lastDrawnFlight != stateTag) {
            _lastDrawnFlight = stateTag;
            drawnOnce = false;

            clearContentArea(); // wipe whatever flight info / animation trail was showing
        }

        if (_fetching) {
            // Reuse one of the flyover bitmaps as a simple "still
            // searching" indicator. The draw position is clamped
            // separately from kmlX/kmlY themselves (untouched, so the
            // shared bounce physics in updateKMLPosition() keep working
            // exactly as they do for the text) because that bounce box was
            // sized for the ~16px-tall bounce text - the 24px-tall bitmap
            // could otherwise draw a few pixels past the border box.
            int drawX = kmlX;
            int drawY = kmlY;
            int maxY = _borderY + _borderH - 2 - PLANE_H;
            if (drawY > maxY) drawY = maxY;

            if (drawnOnce) {
                tft->fillRect(lastDrawnX - margin, lastDrawnY - margin,
                              lastDrawnW + margin * 2, lastDrawnH + margin * 2, theme.bg);
            }

            // East-facing: a fixed, level "still looking" pose, not a real
            // heading - nothing has been found yet to have a heading.
            PlaneSprite::drawLarge(tft, drawX, drawY, PlaneSprite::E, theme.accent);

            lastDrawnX = drawX;
            lastDrawnY = drawY;
            lastDrawnW = PLANE_W;
            lastDrawnH = PLANE_H;
            drawnOnce = true;
        } else {
            // Compute the EXACT bounding box of the bounce-text glyphs at the
            // text size we draw it at, so the erase rect always fully covers
            // the previously drawn text - this is what fixes the trailing line.
            tft->setTextSize(2);
            int16_t textW = tft->textWidth(_bounceLabel);
            int16_t textH = tft->fontHeight();

            if (drawnOnce) {
                tft->fillRect(lastDrawnX - margin, lastDrawnY - margin,
                              lastDrawnW + margin * 2, lastDrawnH + margin * 2, theme.bg);
            }

            // Draw new bounce text at new position
            tft->setTextColor(theme.accent);
            tft->setCursor(kmlX, kmlY);
            tft->println(_bounceLabel);

            // Remember where we drew it
            lastDrawnX = kmlX;
            lastDrawnY = kmlY;
            lastDrawnW = textW;
            lastDrawnH = textH;
            drawnOnce = true;
        }
    }
}

void PlaneTrackerScreen::setFlight(const Flight& flight) {
    // A different aircraft arriving mid-flyover: abandon the one currently
    // crossing the box and let draw() start a fresh flyover for the new
    // one instead. Without this the in-flight animation ran to completion
    // and then called drawFlightInfo(), which reads _currentFlight - so it
    // painted the NEW flight's details underneath the OLD flight's
    // flyover, and because _lastDrawnFlight still held the old callsign
    // the very next draw() immediately animated again and redrew the same
    // info: the "animation, info, animation, info" double-up that shows up
    // whenever two fetches land close together (a boot/reconnect prefetch
    // overlapping a background poll, say). Clearing _lastDrawnFlight is
    // what makes draw() treat this as a fresh flight and wipe the
    // abandoned plane off the box before starting over.
    if (_animInProgress && flight.callsign != _currentFlight.callsign) {
        _animInProgress = false;
        _lastDrawnFlight = "";
    }

    _currentFlight = flight;
    _hasFlight = true;
    _fetching = false; // whatever fetch was in flight (if any) is done now
    _lastFlightTime = millis();
    _showAnimation = true;
}

void PlaneTrackerScreen::clearFlight() {
    _hasFlight = false; // draw() will transition to the bouncing-text state next frame
    _fetching = false;  // ditto - the fetch that got us here (if any) is done
}

void PlaneTrackerScreen::setFetching(bool fetching) {
    _fetching = fetching;
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
// from init(). Never called again after that (see draw()/stepPlaneAnimation(),
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
//
// Non-blocking: startPlaneAnimation() below computes the whole path once
// and arms the animation; stepPlaneAnimation() then advances exactly one
// frame per draw() call (throttled to PLANE_ANIM_FRAME_DELAY_MS via
// millis(), never delay()), so loop() keeps handling input and everything
// else at its normal rate while the plane crosses the screen.
void PlaneTrackerScreen::startPlaneAnimation() {
    float rel = getRelativeHeading(_currentFlight.heading);
    _animBmp = PlaneSprite::large(PlaneSprite::fromHeading(rel));

    float angle = rel * (PI / 180.0f);
    _animVx = sin(angle);
    _animVy = -cos(angle);

    // Animate within the border box's interior, not the whole screen -
    // this keeps the flight path (and its start/end "off-screen" points)
    // consistent with the box that's actually drawn on screen.
    _animBoxX = _borderX + 2;
    _animBoxY = _borderY + 2;
    _animBoxW = _borderW - 4;
    _animBoxH = _borderH - 4;
    int cx = _animBoxX + _animBoxW / 2;
    int cy = _animBoxY + _animBoxH / 2;
    int halfW = _animBoxW / 2;
    int halfH = _animBoxH / 2;

    // Distance from the center to the edge the plane actually LEAVES the
    // box through - the FIRST boundary its path crosses, so the nearer of
    // the two, not the farther. This used to take the max, which for any
    // near-vertical or near-horizontal heading is the distance to an edge
    // the path never reaches: a heading 10 degrees off north has a tiny
    // horizontal component, so halfW/|vx| came out several times the width
    // of the panel. The plane then started that absurdly far off-box, spent
    // most of its frames invisible outside the viewport (the box just sat
    // blank), and - because the frame count is clamped at 130 below - it
    // covered the now-huge path at up to 3x the intended px-per-frame, so
    // the short stretch that did cross the box blurred past. Headings near
    // a diagonal happen to give similar values on both axes, which is why
    // the speed looked right some of the time and wrong the rest.
    float travel = 0;
    if (fabs(_animVx) > 0.0001f) travel = (float)halfW / fabs(_animVx);
    if (fabs(_animVy) > 0.0001f) {
        float vertical = (float)halfH / fabs(_animVy);
        travel = (travel > 0.0f) ? min(travel, vertical) : vertical;
    }
    travel += PLANE_W; // clear margin so the bitmap starts/ends fully outside the box

    float startX = cx - _animVx * travel;
    float startY = cy - _animVy * travel;
    float endX   = cx + _animVx * travel;
    float endY   = cy + _animVy * travel;

    float dist = sqrt((endX - startX) * (endX - startX) + (endY - startY) * (endY - startY));

    // Scale the frame count to the path length so the plane always moves at
    // the same ON-SCREEN speed. A fixed frame count made near-vertical
    // flights (short path across the landscape box) crawl and diagonal
    // ones (long path) zip across, since each covered the same number of
    // steps in the same wall-clock time. Now px-per-frame is constant and
    // the total duration varies instead; clamped so a very short path still
    // animates for a beat and a long one doesn't drag.
    const float PX_PER_FRAME = 4.5f;
    _animFrames = constrain((int)(dist / PX_PER_FRAME), 28, 130);
    _animStep = dist / _animFrames;

    _animFrame = 0;
    _animPx = startX;
    _animPy = startY;
    _animLastPx = -10000;
    _animLastPy = -10000;
    _animLastFrameMs = 0; // 0 means "draw the first frame right away" - see stepPlaneAnimation()
    _animInProgress = true;
}

// Advances the flyover by at most one frame, only once PLANE_ANIM_FRAME_
// DELAY_MS has actually elapsed since the last one - if called again
// before then it does nothing and returns, same as a screen with no
// animation in progress would. Called from draw() on every loop()
// iteration while _animInProgress is true.
void PlaneTrackerScreen::stepPlaneAnimation() {
    unsigned long now = millis();
    if (_animLastFrameMs != 0 && now - _animLastFrameMs < (unsigned long)PLANE_ANIM_FRAME_DELAY_MS) {
        return; // not time for the next frame yet - leave the current one on screen
    }
    _animLastFrameMs = now;

    const Theme &theme = ThemeManager::current();

    // The plane deliberately starts/ends OUTSIDE the box (that's the point -
    // it flies in from off-box and exits the other side). That means its
    // erase/draw rects can overlap the border line, or even go past it
    // entirely. Clip all drawing to strictly inside the box interior so the
    // border itself is never touched, no matter where the bitmap is.
    // vpDatum=false: keep (0,0) at the top-left of the PANEL, so the
    // absolute screen coordinates the path math produces still mean what
    // they say. Left at its default (true), every draw below was silently
    // offset by the box origin - about 28px down the screen - which pushed
    // the back half of a southbound flyover straight out of the viewport
    // and clipped it away entirely.
    tft->setViewport(_animBoxX, _animBoxY, _animBoxW, _animBoxH, false);

    _animPx += _animVx * _animStep;
    _animPy += _animVy * _animStep;

    // Erase only the small area the bitmap previously occupied, not the
    // whole box - this is what makes the motion smooth instead of a
    // full-screen flash every frame.
    if (_animLastPx > -10000) {
        tft->fillRect(_animLastPx - PLANE_W / 2 - 1, _animLastPy - PLANE_H / 2 - 1, PLANE_W + 2, PLANE_H + 2, theme.bg);
    }

    tft->drawBitmap((int)_animPx - PLANE_W / 2, (int)_animPy - PLANE_H / 2, _animBmp, PLANE_W, PLANE_H, theme.fg);

    _animLastPx = (int)_animPx;
    _animLastPy = (int)_animPy;

    tft->resetViewport();

    _animFrame++;
    if (_animFrame >= _animFrames) {
        // Flyover done - remove the plane at its final (off-box) position
        // so nothing lingers, then hand off to the normal flight-info
        // draw, exactly like the old blocking version did right after its
        // loop exited.
        // vpDatum=false: keep (0,0) at the top-left of the PANEL, so the
    // absolute screen coordinates the path math produces still mean what
    // they say. Left at its default (true), every draw below was silently
    // offset by the box origin - about 28px down the screen - which pushed
    // the back half of a southbound flyover straight out of the viewport
    // and clipped it away entirely.
    tft->setViewport(_animBoxX, _animBoxY, _animBoxW, _animBoxH, false);
        tft->fillRect(_animLastPx - PLANE_W / 2 - 1, _animLastPy - PLANE_H / 2 - 1, PLANE_W + 2, PLANE_H + 2, theme.bg);
        tft->resetViewport();

        _animInProgress = false;
        clearContentArea();  // remove the plane's box before the info text appears
        drawFlightInfo();
    }
}