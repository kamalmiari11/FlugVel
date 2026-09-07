#include "WiFiSetupScreen.h"
#include "../external/qrcodegen.hpp"
#include "../ui/Theme.h"
#include "../ui/UiChrome.h"
#include "../network/CaptivePortal.h"

using namespace qrcodegen;

// The QR sits in a white "chip" on the left of the content box. It stays
// pure black/white (never theme-tinted) with a quiet-zone margin so it
// scans reliably regardless of which theme is active.
#define QR_BOX_X   16
#define QR_BOX_Y   42
#define QR_BOX_SZ  132
#define QR_QUIET   8
#define QR_DRAW_SZ (QR_BOX_SZ - QR_QUIET * 2)

WiFiSetupScreen::WiFiSetupScreen(TFT_eSPI* display, int koPin) {
    tft = display;
    koButton = koPin;
    pinMode(koButton, INPUT_PULLUP);
}

void WiFiSetupScreen::drawQRCode(const char* qrText) {
    QrCode qr = QrCode::encodeText(qrText, QrCode::Ecc::LOW_QR);
    int qrSize = qr.getSize();
    int scale = QR_DRAW_SZ / qrSize;
    if (scale < 1) scale = 1;

    int drawn = scale * qrSize;
    int ox = QR_BOX_X + (QR_BOX_SZ - drawn) / 2;
    int oy = QR_BOX_Y + (QR_BOX_SZ - drawn) / 2;

    tft->fillRect(QR_BOX_X, QR_BOX_Y, QR_BOX_SZ, QR_BOX_SZ, MY_WHITE);
    for (int y = 0; y < qrSize; y++) {
        for (int x = 0; x < qrSize; x++) {
            if (qr.getModule(x, y))
                tft->fillRect(ox + x * scale, oy + y * scale, scale, scale, MY_BLACK);
        }
    }
}

void WiFiSetupScreen::showStep1() {
    const Theme &t = ThemeManager::current();

    tft->fillScreen(t.bg);
    tft->setTextFont(1);
    tft->setTextDatum(TL_DATUM);

    // --- Header strip: same bracket-title + hairline as the app's Header ---
    UiChrome::drawBracketTitle(tft, "WI-FI SETUP", 4, 6, t.fg);
    tft->drawFastHLine(0, 19, tft->width(), t.rule);

    // --- Content box ---
    const int bx = 6, by = 24, bw = tft->width() - 12, bh = 182;
    tft->drawRect(bx, by, bw, bh, t.fg);

    // --- QR chip (left) ---
    char qrPayload[128];
    snprintf(qrPayload, sizeof(qrPayload), "WIFI:T:WPA;S:%s;P:%s;;", ssid, password);
    drawQRCode(qrPayload);
    tft->drawRect(QR_BOX_X - 2, QR_BOX_Y - 2, QR_BOX_SZ + 4, QR_BOX_SZ + 4, t.fg);

    // --- Instructions (right column) ---
    const int cx = QR_BOX_X + QR_BOX_SZ + 12;

    tft->setTextSize(1);
    tft->setTextColor(t.fgDim, t.bg);
    tft->setCursor(cx, 44);
    tft->print("SCAN, OR JOIN WI-FI");

    tft->setTextSize(2);
    tft->setTextColor(t.fg, t.bg);
    tft->setCursor(cx, 58);
    tft->print(ssid);

    tft->setTextSize(1);
    tft->setTextColor(t.fgDim, t.bg);
    tft->setCursor(cx, 84);
    tft->print("pass  ");
    tft->print(password);

    tft->setTextColor(t.fgDim, t.bg);
    tft->setCursor(cx, 118);
    tft->print("THEN OPEN IN A BROWSER");

    // CaptivePortal::kSetupHost ("setup.flugvel.com"), not the raw AP IP -
    // this AP answers every DNS query with itself (see CaptivePortal.h), so
    // the name works with no internet at all and is something a person can
    // actually retype if the QR scan fails. This panel runs landscape
    // (320px wide - see setRotation() in main.cpp), so the ~150px-wide
    // column beside the QR chip fits the whole name on one line at size 1
    // (about 108px) with room to spare.
    tft->setTextSize(1);
    tft->setTextColor(t.accent, t.bg);
    tft->setCursor(cx, 132);
    tft->print(CaptivePortal::kSetupHost);

    // --- Bottom hint strip: same rule + dim line as the ActionLegend ---
    const int sy = tft->height() - 22;
    tft->drawFastHLine(0, sy, tft->width(), t.rule);
    tft->setTextSize(1);
    tft->setTextColor(t.fgDim, t.bg);
    tft->setCursor(8, sy + 7);
    tft->print("Waiting for you to connect...");
}

bool WiFiSetupScreen::nextPressed() {
    return digitalRead(koButton) == LOW;
}
