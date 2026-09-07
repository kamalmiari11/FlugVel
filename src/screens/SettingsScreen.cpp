#include "SettingsScreen.h"
#include "../MyColors.h"
#include "../ui/Theme.h"
#include "../ui/UiChrome.h"
#include "../network/CaptivePortal.h"
#include "../version.h"
#include "Header.h"
#include "ActionLegend.h"
#include "ScreenManager.h"
#include "ScreenRegistry.h"
#include "../ui/Units.h"
#include "../external/qrcodegen.hpp"
#include <WiFi.h>

using qrcodegen::QrCode;

// Height of the persistent top status bar (Header::headerHeight). Settings
// draws everything below this line and must never paint over it.
static const int HEADER_H = 20;

// ---- Main-menu (Option A) layout ----
// Six rows, comfortably inside the body between the 20px header and the
// action-legend strip at y=210.
static const int MENU_ROW_H     = 20;
static const int MENU_Y0        = 22;   // top of the first row
static const int MENU_ROW_TEXT_DY = 4;  // label offset within a row
static const int MENU_DANGER_GAP = 4;   // extra space above the destructive group

// Free function target for performOTAUpdate()'s progress callback - a raw
// C function pointer, not a member function, so it can't capture `this`.
// Points at whichever tft instance is currently mid-update; nullptr the
// rest of the time so a stray late callback can't draw into a screen that
// isn't showing the update anymore.
static TFT_eSPI* g_otaProgressTft = nullptr;
static void settingsOtaProgress(int percent) {
    if (!g_otaProgressTft) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    const Theme &theme = ThemeManager::current();
    const int barX = 40, barY = 140, barW = 240, barH = 20;
    int fillWidth = (barW - 4) * percent / 100;
    g_otaProgressTft->fillRect(barX + 2, barY + 2, barW - 4, barH - 4, theme.bg);
    if (fillWidth > 0) g_otaProgressTft->fillRect(barX + 2, barY + 2, fillWidth, barH - 4, theme.accent2);

    char pctLabel[8];
    snprintf(pctLabel, sizeof(pctLabel), "%d%%", percent);
    g_otaProgressTft->setTextDatum(MC_DATUM);
    g_otaProgressTft->setTextSize(1);
    g_otaProgressTft->setTextColor(theme.fg);
    g_otaProgressTft->fillRect(130, 165, 60, 12, theme.bg);
    g_otaProgressTft->drawString(pctLabel, 160, 171);
    g_otaProgressTft->setTextDatum(TL_DATUM);
}

// Formats device uptime as e.g. "2h 14m" or "45m" or "<1m" - minute
// resolution only (seconds ticking once per second was just visual noise
// on the Device Info page).
static String formatUptime(unsigned long ms) {
    unsigned long totalMin = ms / 60000;
    unsigned long h = totalMin / 60;
    unsigned long m = totalMin % 60;

    if (h > 0)  return String(h) + "h " + String(m) + "m";
    if (m > 0)  return String(m) + "m";
    return String("<1m");
}

SettingsScreen::SettingsScreen(TFT_eSPI* display, CaptivePortal* portal)
    : Screen(display), _portal(portal), _selectedOption(0), _inSubmenu(false),
      _needsRedraw(true), _themeChoice(0), _restartConfirmed(false),
      _restartWaitingForRelease(false), _restartButtonHeld(false), _restartPressStartMs(0),
      _hasCheckedUpdate(false),
      _lastPainted(PAINTED_NONE), _lastDrawnOption(-1), _lastDrawnTheme(-1),
      _lastDrawnUptime(""), _lastRestartConfirmed(false), _lastDrawnHoldPixels(-1)
{
}

bool SettingsScreen::allowsScreenSwitch() const {
    // While the config page is up the web server is listening. Cycling away
    // would strand it running with nothing on screen to say so, and no way
    // to close it - so the encoder is locked out until KO closes the page.
    return !(_inSubmenu && _selectedOption == OPTION_CUSTOMISE);
}

void SettingsScreen::init() {
    Serial.println("[SettingsScreen] Initialized");
    // Belt and braces: allowsScreenSwitch() should prevent leaving with the
    // page open, but a re-init must never leave a server listening.
    closeConfigPortal();
    _selectedOption = 0;
    _inSubmenu = false;
    _restartConfirmed = false;
    _needsRedraw = true;
    // Force one full repaint on (re)entry - whatever was on screen belongs
    // to a different screen now (its own redraw already wiped this area).
    _lastPainted = PAINTED_NONE;
}

void SettingsScreen::update() {
    // The web server only gets serviced while its page is open. main.cpp's
    // loop pumps it too (portal.isRunning()), but doing it here as well
    // keeps the page responsive during the ticks this screen owns.
    if (_inSubmenu && _selectedOption == OPTION_CUSTOMISE && _portal) {
        _portal->handle();

        // The phone finished and saved. Shut the server down and drop back
        // to the settings menu rather than leaving a QR on the panel for a
        // page nobody is on any more.
        if (_portal->closeRequested()) {
            Serial.println("[SettingsScreen] Saved from the phone - closing the page");
            closeConfigPortal();
            _inSubmenu   = false;
            _needsRedraw = true;
            _lastPainted = PAINTED_NONE;
        }
    }

    // Uptime shown on the Device Info page changes every second even while
    // nothing is pressed - keep that page live, but drawDeviceInfo() itself
    // still only repatches the one line whose text actually changed.
    if (_inSubmenu && _selectedOption == OPTION_DEVICE_INFO) {
        _needsRedraw = true;
    }

    // Restart confirm: poll the button directly rather than reacting to
    // onButtonPress()'s press-edge callback, so a quick tap (go back) can
    // be told apart from a hold (actually restart) - by the time
    // onButtonPress() fires the button has only just gone down, so it
    // can't yet know which one this will turn out to be.
    bool inHoldConfirm = _inSubmenu && !_restartConfirmed &&
                         (_selectedOption == OPTION_RESTART || _selectedOption == OPTION_FACTORY_RESET);
    if (inHoldConfirm) {
        const bool factory = (_selectedOption == OPTION_FACTORY_RESET);
        const unsigned long holdMs = factory ? FACTORY_RESET_HOLD_MS : RESTART_HOLD_MS;
        bool pressed = (digitalRead(KO_BUTTON) == LOW);

        if (_restartWaitingForRelease) {
            // Still the same physical press that opened this submenu -
            // ignore it until released, so it can't itself be read as a
            // tap-to-go-back or count towards the hold timer.
            if (!pressed) _restartWaitingForRelease = false;
        } else if (pressed && !_restartButtonHeld) {
            _restartButtonHeld = true;
            _restartPressStartMs = millis();
        } else if (pressed && _restartButtonHeld) {
            if (millis() - _restartPressStartMs >= holdMs) {
                _restartConfirmed = true;
                if (factory) {
                    Serial.println("[SettingsScreen] Factory reset confirmed by long-press");
                    drawFactoryResetConfirm(false); // patch "Resetting..." in before the wipe
                    delay(600);
                    _portal->factoryReset();        // wipes EEPROM + NVS and reboots - never returns
                } else {
                    Serial.println("[SettingsScreen] Restart confirmed by long-press");
                    drawRestartConfirm(false);
                    delay(600);
                    ESP.restart();
                }
            } else {
                _needsRedraw = true; // keep the hold-progress bar animating
            }
        } else if (!pressed && _restartButtonHeld) {
            // Released before the hold threshold - a quick tap means "back".
            _restartButtonHeld = false;
            _inSubmenu = false;
            _needsRedraw = true;
        }
    }
}

void SettingsScreen::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;

    PaintedScreen wantScreen = !_inSubmenu ? PAINTED_MENU
        : (_selectedOption == OPTION_DEVICE_INFO) ? PAINTED_DEVICE_INFO
        : (_selectedOption == OPTION_THEME) ? PAINTED_THEME
        : (_selectedOption == OPTION_UPDATE) ? PAINTED_UPDATE
        : (_selectedOption == OPTION_FACTORY_RESET) ? PAINTED_FACTORY_RESET
        : PAINTED_RESTART;

    // Only clear the whole panel when switching to a genuinely different
    // screen/submenu - staying put just patches the part that changed.
    bool fullRepaint = (wantScreen != _lastPainted);
    if (fullRepaint) {
        // Clear only the body, NEVER the top 20px - that strip belongs to
        // the persistent Header, which is drawn by ScreenManager and does
        // not know to repaint itself after a wipe. A full fillScreen here
        // made the header vanish every time a submenu ("API", "Update", …)
        // was opened.
        tft->fillRect(0, HEADER_H, tft->width(), tft->height() - HEADER_H, ThemeManager::current().bg);
        _lastDrawnOption = -1;
        _lastDrawnTheme = -1;
        _lastDrawnUptime = "";
        _lastDrawnHoldPixels = -1;
        _lastRestartConfirmed = false;
    }
    _lastPainted = wantScreen;

    if (_inSubmenu) {
        drawSubmenu(fullRepaint);
    } else {
        drawMainMenu(fullRepaint);
    }
}

// y (top edge) of a given menu row - the destructive group (RESTART
// onward) is pushed down by MENU_DANGER_GAP so a divider fits above it.
int SettingsScreen::menuRowTop(int i) {
    int y = MENU_Y0 + i * MENU_ROW_H;
    if (i >= OPTION_DANGER_START) y += MENU_DANGER_GAP;
    return y;
}

void SettingsScreen::drawMainMenu(bool fullRepaint) {
    const Theme &theme = ThemeManager::current();
    tft->setTextDatum(TL_DATUM);

    static const char* options[OPTION_COUNT] = {
        "Device info",
        "Theme",
        "Configure on phone",
        "Software update",
        "Restart device",
        "Factory reset",
    };

    // Current value for the rows that carry one, shown small + right-aligned
    // with a ">" affordance. Null for pure-action rows.
    const char* rowValues[OPTION_COUNT] = { nullptr };
    rowValues[OPTION_THEME] = ThemeRegistry::byId(_portal->getThemeId()).name;
    rowValues[OPTION_UPDATE] = "v" FIRMWARE_VERSION;

    const int rowX = 6;
    const int rowW = tft->width() - 12;

    // Dashed divider in the gap above the destructive group.
    if (fullRepaint) {
        int dy = MENU_Y0 + OPTION_DANGER_START * MENU_ROW_H + MENU_DANGER_GAP / 2;
        for (int x = rowX + 4; x < rowX + rowW - 4; x += 7)
            tft->drawFastHLine(x, dy, 3, theme.rule);
    }

    // Full repaint -> every row. Selection move -> just the two rows that
    // actually changed.
    for (int i = 0; i < OPTION_COUNT; i++) {
        if (!fullRepaint && i != _selectedOption && i != _lastDrawnOption) continue;

        int top = menuRowTop(i);
        bool selected = (i == _selectedOption);
        bool danger   = (i >= OPTION_DANGER_START);

        tft->fillRect(rowX, top, rowW, MENU_ROW_H, selected ? theme.selectBg : theme.bg);
        if (selected)
            tft->fillRect(rowX, top, 4, MENU_ROW_H, theme.accent); // left accent bar

        tft->setTextSize(2);
        tft->setTextDatum(TL_DATUM);
        tft->setTextColor(selected ? theme.selectFg : (danger ? theme.danger : theme.fg));
        tft->setCursor(rowX + 12, top + MENU_ROW_TEXT_DY);
        tft->print(options[i]);

        if (rowValues[i]) {
            tft->setTextSize(1);
            tft->setTextDatum(MR_DATUM);
            tft->setTextColor(selected ? theme.selectFg : theme.fgDim);
            tft->drawString(String(rowValues[i]) + " >", rowX + rowW - 6, top + MENU_ROW_H / 2);
            tft->setTextDatum(TL_DATUM);
        }
    }
    _lastDrawnOption = _selectedOption;
}

void SettingsScreen::drawSubmenu(bool fullRepaint) {
    switch (_selectedOption) {
        case OPTION_DEVICE_INFO:
            drawDeviceInfo(fullRepaint);
            break;

        case OPTION_THEME:
            drawThemeSettings(fullRepaint);
            break;

        case OPTION_CUSTOMISE:
            drawCustomiseOnPhone(fullRepaint);
            break;

        case OPTION_UPDATE:
            drawUpdateSettings(fullRepaint);
            break;

        case OPTION_RESTART:
            drawRestartConfirm(fullRepaint);
            break;

        case OPTION_FACTORY_RESET:
            drawFactoryResetConfirm(fullRepaint);
            break;
    }
}

void SettingsScreen::drawDeviceInfo(bool fullRepaint) {
    const Theme &theme = ThemeManager::current();
    tft->setTextDatum(TL_DATUM);
    tft->setTextSize(1);

    if (fullRepaint) {
        tft->setTextColor(theme.fg);

        bool connected = (WiFi.status() == WL_CONNECTED);
        int y = 30;
        const int lineHeight = 16;

        tft->setCursor(20, y);
        tft->print("WiFi: ");
        tft->println(connected ? "Connected" : "Disconnected");
        y += lineHeight;

        tft->setCursor(20, y);
        tft->print("SSID: ");
        tft->println(connected ? WiFi.SSID() : "-");
        y += lineHeight;

        tft->setCursor(20, y);
        tft->print("IP: ");
        tft->println(connected ? WiFi.localIP().toString() : "-");
        y += lineHeight;

        tft->setCursor(20, y);
        tft->print("Signal: ");
        if (connected) {
            tft->print(WiFi.RSSI());
            tft->println(" dBm");
        } else {
            tft->println("-");
        }
        y += lineHeight;

        tft->setCursor(20, y);
        tft->print("Location: ");
        String city = _portal->getCity();
        String country = _portal->getCountry();
        tft->println((city.length() || country.length()) ? (city + ", " + country) : "Not set");
        y += lineHeight;

        tft->setCursor(20, y);
        tft->print("Facing: ");
        String facing = _portal->getFacingDirection();
        tft->println(facing.length() ? facing : "W (default)");
        y += lineHeight;

        tft->setCursor(20, y);
        tft->print("Timezone: ");
        String tz = _portal->getTimezone();
        tft->println(tz.length() ? tz : "Not set");
    }

    // Uptime is the only field that changes tick-to-tick - patch just its
    // value instead of repainting the whole page every second.
    String uptime = formatUptime(millis());
    if (fullRepaint || uptime != _lastDrawnUptime) {
        const int uptimeY = 30 + (6 * 16); // 7th info line, same layout as above
        tft->fillRect(20, uptimeY, 200, 12, theme.bg);
        tft->setTextColor(theme.fg);
        tft->setCursor(20, uptimeY);
        tft->print("Uptime: ");
        tft->println(uptime);
        _lastDrawnUptime = uptime;
    }
}

// One highlighted row - turn to cycle through
// the themes in ThemeRegistry, press to save & exit. The theme is applied
// live on every turn (see onEncoderUp/Down) so the whole panel, including
// this row, is already in the previewed colours by the time we redraw.
void SettingsScreen::drawThemeSettings(bool fullRepaint) {
    const Theme &theme = ThemeManager::current();
    tft->setTextDatum(MC_DATUM);

    if (fullRepaint) {
        UiChrome::drawPanelRow(tft, 20, 60, 280, 40, true, theme.bg, theme.selectBg);

        tft->setTextSize(1);
        tft->setTextColor(theme.fgDim);
        tft->drawString("Turn to preview a colour theme.", 160, 130);
        tft->drawString("Press to save it.", 160, 144);
    }

    if (fullRepaint || _themeChoice != _lastDrawnTheme) {
        UiChrome::drawPanelRow(tft, 24, 64, 272, 32, true, theme.bg, theme.selectBg);
        tft->setTextSize(2);
        tft->setTextColor(theme.selectFg);
        tft->drawString(ThemeRegistry::byId(_themeChoice).name, 160, 80);
        _lastDrawnTheme = _themeChoice;
    }

    tft->setTextDatum(TL_DATUM);
}

// Shows the current firmware version and, once checked this session,
// whatever the manifest said - update checks are user-triggered only
// (never automatic in the background), so this reflects "as of the last
// time you pressed Check", not necessarily right now.
// Renders a QR into a light square. Same approach as WiFiSetupScreen's join
// code: pick the largest integer module size that fits, centre it, and give
// it a light quiet zone regardless of theme - a scanner needs dark modules
// on a light field, and the darker themes would otherwise invert it.
void SettingsScreen::drawQrCode(const char* text, int boxX, int boxY, int boxSize) {
    QrCode qr = QrCode::encodeText(text, QrCode::Ecc::LOW_QR);
    int modules = qr.getSize();
    int scale = (boxSize - 8) / modules;   // leave room for a quiet zone
    if (scale < 1) scale = 1;

    int drawn = scale * modules;
    int ox = boxX + (boxSize - drawn) / 2;
    int oy = boxY + (boxSize - drawn) / 2;

    tft->fillRect(boxX, boxY, boxSize, boxSize, MY_WHITE);
    for (int y = 0; y < modules; y++) {
        for (int x = 0; x < modules; x++) {
            if (qr.getModule(x, y))
                tft->fillRect(ox + x * scale, oy + y * scale, scale, scale, MY_BLACK);
        }
    }
}

void SettingsScreen::openConfigPortal() {
    if (!_portal) return;
    if (_portal->isRunning()) return;

    // Preferred route: serve on the network the device is already joined
    // to, so the phone keeps its internet and does not have to switch
    // networks. Only fall back to raising an access point if there is no
    // connection to serve on.
    if (WiFi.status() == WL_CONNECTED) {
        _portal->beginSTA();
    } else {
        _portal->beginAP();
    }
}

void SettingsScreen::closeConfigPortal() {
    if (_portal) _portal->stop();
}

void SettingsScreen::drawCustomiseOnPhone(bool fullRepaint) {
    // Nothing on this page changes while it is open, so it is painted once
    // on entry and then left alone. Repainting a QR over SPI every tick
    // would be slow and would flicker for no reason.
    if (!fullRepaint) return;

    const Theme &theme = ThemeManager::current();
    tft->fillRect(0, HEADER_H, tft->width(), tft->height() - HEADER_H, theme.bg);

    bool ap = _portal && _portal->isApMode();
    // On the setup AP, show the name rather than 192.168.4.1 - it is the
    // same destination either way, but a name is something someone can
    // actually retype after the QR fails to scan. On the user's LAN the
    // device has no name, so its address is all there is.
    String url = ap ? String("http://") + CaptivePortal::kSetupHost
                    : String("http://") + WiFi.localIP().toString();

    // On the LAN the QR and the printed line deliberately differ. The QR
    // carries the raw IP because it is scanned by a phone, and Android
    // frequently fails to resolve a .local name typed into the browser -
    // a scan that dead-ends is worse than an ugly address nobody reads.
    // The printed line carries the name, because that is the half a person
    // retypes by hand when the scan does not work.
    String qrTarget = url;
    if (!ap)
        url = String("http://") + CaptivePortal::mdnsHost();

    tft->setTextDatum(MC_DATUM);
    tft->setTextSize(1);
    tft->setTextColor(theme.fgDim);
    tft->drawString(ap ? "JOIN THE FLUGVEL WI-FI, THEN SCAN"
                       : "SCAN, OR TYPE THIS IN YOUR BROWSER",
                    tft->width() / 2, 30);

    const int box = 96;
    drawQrCode(qrTarget.c_str(), (tft->width() - box) / 2, 40, box);

    tft->setTextSize(1);
    tft->setTextColor(theme.fg);
    tft->drawString(url, tft->width() / 2, 148);

    if (!ap && _portal) {
        char pin[16];
        snprintf(pin, sizeof(pin), "PIN %04u", _portal->configPin());
        tft->setTextSize(2);
        tft->setTextColor(theme.accent);
        tft->drawString(pin, tft->width() / 2, 172);
    }

    tft->setTextSize(1);
    tft->setTextColor(theme.fgDim);
    tft->drawString("PRESS KO TO CLOSE", tft->width() / 2, 196);

    tft->setTextDatum(TL_DATUM);
}

void SettingsScreen::drawUpdateSettings(bool fullRepaint) {
    if (!fullRepaint) return; // this whole page is only ever repainted as a full page - see runUpdateCheck()/runOTAUpdate(), which paint their own transient states directly

    const Theme &theme = ThemeManager::current();
    tft->setTextDatum(TL_DATUM);
    tft->setTextSize(1);
    tft->setTextColor(theme.fg);
    tft->setCursor(20, 30);
    tft->print("Current: v");
    tft->println(FIRMWARE_VERSION);

    int y = 55;
    if (!_hasCheckedUpdate) {
        tft->setTextColor(theme.fgDim);
        tft->setCursor(20, y);
        tft->println("Not checked yet this session.");
    } else if (!_updateInfo.checkSucceeded) {
        tft->setTextColor(theme.danger);
        tft->setCursor(20, y);
        // Say which kind of failure. "Check failed" on its own sends
        // people hunting for a network problem when the usual cause is
        // simply that no manifest has been published yet.
        char why[48];
        if (_updateInfo.status == 404)     snprintf(why, sizeof(why), "No update published yet");
        else if (_updateInfo.status == 0)  snprintf(why, sizeof(why), "No Wi-Fi connection");
        else if (_updateInfo.status == -2) snprintf(why, sizeof(why), "Manifest wasn't valid JSON");
        else if (_updateInfo.status < 0)   snprintf(why, sizeof(why), "Couldn't reach the server");
        else snprintf(why, sizeof(why), "Check failed (HTTP %d)", _updateInfo.status);
        tft->println(why);
    } else if (!_updateInfo.updateAvailable) {
        tft->setTextColor(theme.accent2);
        tft->setCursor(20, y);
        tft->println("Up to date.");
        y += 16;

        // Nothing new to install, but the notes for the version that's
        // already running are exactly what the *last* update added - the
        // manifest always describes the newest published release, and
        // that's what this device is on. Showing that instead of leaving
        // the page blank means "what changed" is answered even when
        // there's nothing to do about it right now.
        if (_updateInfo.notes.length() > 0) {
            tft->setTextColor(theme.fg);
            tft->setCursor(20, y);
            tft->print("What v");
            tft->print(FIRMWARE_VERSION);
            tft->println(" added:");
            y += 16;
            drawWrappedNotes(_updateInfo.notes, y);
        }
    } else {
        tft->setTextColor(theme.accent);
        tft->setCursor(20, y);
        tft->print("Available: v");
        tft->println(_updateInfo.version);
        y += 16;
        drawWrappedNotes(_updateInfo.notes, y);
    }
}

// Notes can run long - wrap crudely at a fixed character count rather
// than pulling in a text-wrapping library for a few lines. Shared between
// the "available" and "up to date" states above - same column, same
// bottom clip, same dim color, just a different source string and start y.
void SettingsScreen::drawWrappedNotes(const String &notesIn, int y) {
    const Theme &theme = ThemeManager::current();
    tft->setTextColor(theme.fgDim);
    String notes = notesIn;
    const int maxCharsPerLine = 34;
    while (notes.length() > 0 && y < 195) {
        String line;
        if ((int)notes.length() <= maxCharsPerLine) {
            line = notes;
            notes = "";
        } else {
            int breakAt = notes.lastIndexOf(' ', maxCharsPerLine);
            if (breakAt <= 0) breakAt = maxCharsPerLine;
            line = notes.substring(0, breakAt);
            notes = notes.substring(breakAt + 1);
        }
        tft->setCursor(20, y);
        tft->println(line);
        y += 14;
    }
}

// Blocking - draws "Checking..." immediately, then makes the (also
// blocking) HTTP call. This is a deliberate, user-triggered action
// (pressing the button on this page), unlike the app's automatic API
// polling elsewhere, which all runs on the background task specifically
// to keep loop() from ever stalling - a brief pause here after an explicit
// press is expected and fine, the same way WiFiSetupScreen's captive
// portal flow blocks too.
void SettingsScreen::runUpdateCheck() {
    const Theme &theme = ThemeManager::current();
    tft->fillRect(0, HEADER_H, tft->width(), tft->height() - HEADER_H, theme.bg);
    tft->setTextDatum(MC_DATUM);
    tft->setTextSize(2);
    tft->setTextColor(theme.fg);
    tft->drawString("Checking...", 160, 120);
    tft->setTextDatum(TL_DATUM);

    _updateInfo = checkForUpdate();
    _hasCheckedUpdate = true;

    _lastPainted = PAINTED_NONE; // force the next draw() to fully repaint this page with the result
    _needsRedraw = true;
}

// Blocking - shows a live progress bar via performOTAUpdate()'s callback.
// Only ever returns on failure; on success the device reboots itself from
// inside performOTAUpdate() and this function (and the rest of the app)
// never gets control back.
void SettingsScreen::runOTAUpdate() {
    // WebServer + WiFiClientSecure's TLS buffers + HTTPUpdate at once is the
    // RAM-tight moment in this firmware. The config page should already be
    // closed by now, but make sure before pulling a firmware image.
    closeConfigPortal();

    const Theme &theme = ThemeManager::current();
    tft->fillRect(0, HEADER_H, tft->width(), tft->height() - HEADER_H, theme.bg);
    tft->setTextDatum(MC_DATUM);

    tft->setTextSize(2);
    tft->setTextColor(theme.fg);
    tft->drawString("Updating...", 160, 80);

    tft->setTextSize(1);
    tft->setTextColor(theme.fgDim);
    tft->drawString("Do not power off the device", 160, 110);

    tft->drawRect(40, 140, 240, 20, theme.fgDim);
    tft->setTextDatum(TL_DATUM);

    g_otaProgressTft = tft;
    bool ok = performOTAUpdate(_updateInfo.url, settingsOtaProgress);
    g_otaProgressTft = nullptr;

    if (!ok) {
        tft->fillRect(0, HEADER_H, tft->width(), tft->height() - HEADER_H, theme.bg);
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(theme.danger);
        tft->setTextSize(2);
        tft->drawString("Update Failed", 160, 100);
        tft->setTextSize(1);
        tft->setTextColor(theme.fgDim);
        tft->drawString("Check the serial log for details", 160, 130);
        tft->drawString("[Press to continue]", 160, 200);
        tft->setTextDatum(TL_DATUM);

        // Blocking wait for acknowledgement, matching this page's other
        // deliberately-blocking, user-triggered actions above.
        while (digitalRead(KO_BUTTON) == HIGH) { delay(10); }
        delay(30);

        _lastPainted = PAINTED_NONE;
        _needsRedraw = true;
    }
}

void SettingsScreen::drawRestartConfirm(bool fullRepaint) {
    const Theme &theme = ThemeManager::current();
    tft->setTextDatum(TL_DATUM);
    tft->setTextSize(1);

    const int barX = 20, barY = 60, barW = 260, barH = 12;

    if (fullRepaint) {
        if (!_restartConfirmed) {
            tft->drawRect(barX, barY, barW, barH, theme.fgDim);
        }
    }

    // The confirm/"Restarting..." line is the only thing that changes
    // after the first paint until the moment it flips (right before reboot).
    if (fullRepaint || _restartConfirmed != _lastRestartConfirmed) {
        tft->fillRect(20, 30, 260, 14, theme.bg);
        tft->setCursor(20, 30);
        tft->setTextColor(theme.danger);
        tft->println(_restartConfirmed ? "Restarting..." : "Hold to restart");
        _lastRestartConfirmed = _restartConfirmed;
    }

    if (_restartConfirmed) return; // no progress bar once it's already restarting

    // Hold-progress bar fill - only the interior, never the outline, and
    // only repainted when its width in pixels actually changes.
    unsigned long heldMs = _restartButtonHeld ? (millis() - _restartPressStartMs) : 0;
    if (heldMs > RESTART_HOLD_MS) heldMs = RESTART_HOLD_MS;
    int fillPixels = (int)((unsigned long)(barW - 4) * heldMs / RESTART_HOLD_MS);

    if (fullRepaint || fillPixels != _lastDrawnHoldPixels) {
        tft->fillRect(barX + 2, barY + 2, barW - 4, barH - 4, theme.bg);
        if (fillPixels > 0) tft->fillRect(barX + 2, barY + 2, fillPixels, barH - 4, theme.accent);
        _lastDrawnHoldPixels = fillPixels;
    }
}

// Same hold-to-confirm mechanic as drawRestartConfirm(), but with a
// spelled-out warning and a longer hold (FACTORY_RESET_HOLD_MS) since it
// wipes everything and can't be undone. Shares the _restart* tracking
// fields; update() routes the hold to _portal->factoryReset().
void SettingsScreen::drawFactoryResetConfirm(bool fullRepaint) {
    const Theme &theme = ThemeManager::current();
    tft->setTextDatum(TL_DATUM);

    const int barX = 20, barY = 92, barW = 280, barH = 12;

    if (fullRepaint) {
        tft->setTextSize(2);
        tft->setTextColor(theme.danger);
        tft->setCursor(20, 26);
        tft->println("FACTORY RESET");

        tft->setTextSize(1);
        tft->setTextColor(theme.fgDim);
        tft->setCursor(20, 52);
        tft->println("Erases Wi-Fi, location, theme");
        tft->setCursor(20, 66);
        tft->println("and scores. Cannot be undone.");

        if (!_restartConfirmed)
            tft->drawRect(barX, barY, barW, barH, theme.fgDim);
    }

    if (fullRepaint || _restartConfirmed != _lastRestartConfirmed) {
        tft->fillRect(20, 112, 280, 14, theme.bg);
        tft->setTextSize(1);
        tft->setCursor(20, 112);
        tft->setTextColor(theme.danger);
        tft->println(_restartConfirmed ? "Resetting..." : "Hold to wipe");
        _lastRestartConfirmed = _restartConfirmed;
    }

    if (_restartConfirmed) return;

    unsigned long heldMs = _restartButtonHeld ? (millis() - _restartPressStartMs) : 0;
    if (heldMs > FACTORY_RESET_HOLD_MS) heldMs = FACTORY_RESET_HOLD_MS;
    int fillPixels = (int)((unsigned long)(barW - 4) * heldMs / FACTORY_RESET_HOLD_MS);

    if (fullRepaint || fillPixels != _lastDrawnHoldPixels) {
        tft->fillRect(barX + 2, barY + 2, barW - 4, barH - 4, theme.bg);
        if (fillPixels > 0) tft->fillRect(barX + 2, barY + 2, fillPixels, barH - 4, theme.danger);
        _lastDrawnHoldPixels = fillPixels;
    }
}

void SettingsScreen::onEncoderUp() {
    if (!_inSubmenu) {
        _selectedOption = (_selectedOption - 1 + OPTION_COUNT) % OPTION_COUNT;
        _needsRedraw = true;
    } else if (_selectedOption == OPTION_THEME) {
        int n = ThemeRegistry::count();
        _themeChoice = (_themeChoice - 1 + n) % n;
        applyThemePreview();
    } else if (_selectedOption == OPTION_UPDATE) {
        // No continuous value to adjust here (unlike API/Home Screen) -
        // turning the knob just means "back", since press is already
        // spoken for by Check/Update Now.
        _inSubmenu = false;
        _needsRedraw = true;
    }
}

void SettingsScreen::onEncoderDown() {
    if (!_inSubmenu) {
        _selectedOption = (_selectedOption + 1) % OPTION_COUNT;
        _needsRedraw = true;
    } else if (_selectedOption == OPTION_THEME) {
        _themeChoice = (_themeChoice + 1) % ThemeRegistry::count();
        applyThemePreview();
    } else if (_selectedOption == OPTION_UPDATE) {
        _inSubmenu = false;
        _needsRedraw = true;
    }
}

// Apply _themeChoice to the live ThemeManager and force everything -
// including the header/legend strips this screen doesn't normally own -
// to repaint in the new palette.
void SettingsScreen::applyThemePreview() {
    ThemeManager::setThemeById((uint8_t)_themeChoice);
    _lastPainted = PAINTED_NONE;          // recolour the whole settings body
    if (_header) _header->invalidate();
    if (_legend) _legend->invalidate();
    _needsRedraw = true;
}

void SettingsScreen::onButtonPress() {
    if (!_inSubmenu) {
        if (_selectedOption == OPTION_THEME) {
            _themeChoice = _portal->getThemeId();
            if (_themeChoice < 0 || _themeChoice >= ThemeRegistry::count()) _themeChoice = 0;
            _lastDrawnTheme = -1;
        } else if (_selectedOption == OPTION_CUSTOMISE) {
            openConfigPortal();
        } else if (_selectedOption == OPTION_UPDATE) {
            // Auto-check the moment this page opens - no need to press
            // anything first. runUpdateCheck() draws its own "Checking..."
            // screen and result, so _inSubmenu/_needsRedraw below are
            // effectively redundant for this option but harmless.
            runUpdateCheck();
        } else if (_selectedOption == OPTION_RESTART || _selectedOption == OPTION_FACTORY_RESET) {
            // Fresh hold-tracking state for this visit - the press that
            // just opened this submenu must be released once before it
            // can count as either a tap-back or the start of a hold.
            _restartWaitingForRelease = true;
            _restartButtonHeld = false;
            _lastDrawnHoldPixels = -1;
        }
        _inSubmenu = true;
        _needsRedraw = true;
        return;
    }

    // Already in a submenu - what "press" does depends on which one.
    if (_selectedOption == OPTION_THEME) {
        // Preview is already live - just pin it down and persist.
        uint8_t applied = ThemeManager::setThemeById((uint8_t)_themeChoice);
        _portal->setThemeId(applied);
        _portal->saveLocationEEPROM();
        _inSubmenu = false;
    } else if (_selectedOption == OPTION_CUSTOMISE) {
        // KO is the only way out of this page (allowsScreenSwitch() locks
        // the encoder while it is open), so this is the single point where
        // the server gets shut down again.
        closeConfigPortal();
        _inSubmenu = false;
    } else if (_selectedOption == OPTION_UPDATE) {
        if (_hasCheckedUpdate && _updateInfo.checkSucceeded && _updateInfo.updateAvailable) {
            runOTAUpdate(); // only returns on failure - stays on this page either way
        } else {
            // The check already ran automatically when this page opened -
            // pressing KO again means "done, go back", not "check again".
            _inSubmenu = false;
        }
    } else if (_selectedOption == OPTION_RESTART || _selectedOption == OPTION_FACTORY_RESET) {
        // Deliberately a no-op: this fires the instant the button goes
        // down, before we know if it'll be a tap or a hold - update()'s
        // polling above handles both the tap-back and the hold-confirm.
    } else {
        // Device Info - just a "Back" exit
        _inSubmenu = false;
    }

    _needsRedraw = true;
}

void SettingsScreen::getActionLegend(String &line1, String &line2) const {
    if (_inSubmenu && _selectedOption == OPTION_CUSTOMISE) {
        // The encoder is locked out while the server is up, so say so
        // rather than implying a rotation that does nothing.
        line1 = "";
        line2 = "o CLOSE";
        return;
    }

    if (!_inSubmenu) {
        line1 = "^v SELECT";
        line2 = "o ENTER";
        return;
    }

    switch (_selectedOption) {
        case OPTION_DEVICE_INFO:
            line1 = "";
            line2 = "o BACK";
            break;
        case OPTION_THEME:
            line1 = "^v PREVIEW THEME";
            line2 = "o SAVE & BACK";
            break;
        case OPTION_UPDATE:
            line1 = "^v BACK";
            line2 = (_hasCheckedUpdate && _updateInfo.checkSucceeded && _updateInfo.updateAvailable)
                        ? "o UPDATE NOW"
                        : "o BACK";
            break;
        case OPTION_RESTART:
            line1 = "";
            line2 = _restartConfirmed ? "" : "TAP: BACK  HOLD: RESTART";
            break;
        case OPTION_FACTORY_RESET:
            line1 = "";
            line2 = _restartConfirmed ? "" : "TAP: BACK  HOLD: WIPE";
            break;
    }
}
