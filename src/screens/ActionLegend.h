#pragma once
#include <TFT_eSPI.h>
#include <Arduino.h>

// ActionLegend is the persistent bottom strip that tells the user what the
// encoder rotation and KO button currently do, on whichever screen (and
// screen *state*) is showing. It only repaints when the text actually
// changes (see draw()) - constantly redrawing an unchanged strip on every
// loop() iteration was producing a lot of pointless SPI traffic and
// visible screen flicker for no visual benefit, since the pixels never
// actually changed.
class ActionLegend {
public:
    ActionLegend(TFT_eSPI* display);

    // line1: what encoder rotation does right now - pass "" if this
    // screen/state has no rotation behavior (a neutral placeholder is
    // shown instead of inventing one).
    // line2: what the KO button does right now.
    void draw(const String &line1, const String &line2);

    int height() const { return legendHeight; }

    // See Header::invalidate()'s comment - same reasoning, same trigger
    // (ScreenManager calls this on every screen switch).
    void invalidate() { _drawnOnce = false; }

private:
    TFT_eSPI* tft;
    const int legendHeight = 30; // 2 text rows + padding + hairline

    // Only repaint when the text actually changes (or on the first call) -
    // redrawing this every single loop() iteration regardless of content
    // adds a lot of needless SPI traffic when idle and was causing visible
    // flicker across the whole panel.
    String _lastLine1;
    String _lastLine2;
    bool _drawnOnce = false;
};
