#pragma once
#include "Screen.h"
#include "../api/update_check.h"

class CaptivePortal;
class Header;
class ActionLegend;

class SettingsScreen : public Screen {
public:
    SettingsScreen(TFT_eSPI* display, CaptivePortal* portal);

    // The Theme submenu applies a theme live as a preview, which recolours
    // the whole panel - including the persistent header/legend strips that
    // ScreenManager (not this screen) owns. Give this screen a handle to
    // them so it can force those to repaint too. Optional - safe to leave
    // unset, the strips just refresh a beat later on their own.
    void setChrome(Header* header, ActionLegend* legend) { _header = header; _legend = legend; }

    // False while the "Customise on phone" page is open. The web server is
    // running at that point, and cycling away with the encoder would leave
    // it running with nothing on screen telling the user - or offering a
    // way to shut it down. Same reasoning as FocusTimerScreen's lock during
    // a countdown; KO closes the page.
    bool allowsScreenSwitch() const override;

    void init() override;
    void update() override;
    void draw() override;
    void onEncoderUp() override;
    void onEncoderDown() override;
    void onButtonPress() override;
    const char* getName() const override { return "Settings"; }
    ScreenId id() const override { return ScreenId::Settings; }
    void getActionLegend(String &line1, String &line2) const override;

private:
    CaptivePortal* _portal;
    int _selectedOption;
    bool _inSubmenu;
    bool _needsRedraw;

    // No "Back" item - the physical encoder-knob click already cycles to
    // the next top-level screen from anywhere, including here, so a menu
    // row that duplicated that would just be dead weight.
    // OPTION_RESTART onward is the "destructive" group - drawn below a
    // divider in the menu and each guarded by a hold-to-confirm.
    // The flight interval, home screen, clock format and units used to be
    // here too. They live on the phone page now - two places to set one
    // value is two places for them to disagree.
    enum SettingsOption {
        OPTION_DEVICE_INFO = 0,
        OPTION_THEME = 1,
        OPTION_CUSTOMISE = 2,
        OPTION_UPDATE = 3,
        OPTION_RESTART = 4,
        OPTION_FACTORY_RESET = 5
    };
    static const int OPTION_COUNT = 6;
    static const int OPTION_DANGER_START = OPTION_RESTART;

    Header* _header = nullptr;
    ActionLegend* _legend = nullptr;

    // Index into ThemeRegistry, synced from the portal's saved theme id
    // when the Theme submenu opens. Applied live as a preview on every
    // knob turn; persisted to EEPROM only on exit.
    int _themeChoice;

    // True once a restart has been confirmed - used only to show a brief
    // "Restarting..." message for a moment before ESP.restart() actually
    // takes effect, so the confirm click doesn't look like it did nothing.
    bool _restartConfirmed;

    // Restart confirm uses a HOLD instead of a second tap: a quick tap
    // goes back (consistent with every other submenu's "press = back"),
    // holding the button down for RESTART_HOLD_MS actually restarts. This
    // is polled directly from the raw pin in update() - onButtonPress()'s
    // press-edge callback fires the instant the button goes down, before
    // we can know whether it'll turn into a tap or a hold.
    static const unsigned long RESTART_HOLD_MS = 2000;
    // Factory reset wipes everything and can't be undone, so it wants a
    // longer, more deliberate hold than a plain restart. Shares the same
    // _restart* tracking fields below.
    static const unsigned long FACTORY_RESET_HOLD_MS = 3500;
    bool _restartWaitingForRelease; // true right after entering this submenu, until the opening press is released, so that same press can't itself be read as a hold or a tap
    bool _restartButtonHeld;
    unsigned long _restartPressStartMs;

    // ---- Software update ----
    // A check is only ever run when the user actually presses "Check for
    // Update" (or "Retry") on this page - never automatically in the
    // background - so _updateInfo reflects whatever was last requested,
    // not necessarily the true current state of the manifest.
    bool _hasCheckedUpdate;
    UpdateInfo _updateInfo;

    // Tracks which screen/submenu was painted last time, so entering a
    // *different* screen still gets one full clear (nothing stale left
    // behind) but staying on the same screen only patches the bit that
    // changed - no full-screen fillScreen() every tick.
    enum PaintedScreen { PAINTED_NONE, PAINTED_MENU, PAINTED_DEVICE_INFO, PAINTED_THEME, PAINTED_CUSTOMISE, PAINTED_UPDATE, PAINTED_RESTART, PAINTED_FACTORY_RESET };
    PaintedScreen _lastPainted;
    int _lastDrawnOption;      // main menu: which row was highlighted last paint
    int _lastDrawnTheme;       // Theme: which choice was shown last paint
    String _lastDrawnUptime;   // Device Info: last uptime string drawn, so unchanged seconds skip the redraw entirely
    bool _lastRestartConfirmed;
    int _lastDrawnHoldPixels;  // Restart: width of hold-progress bar fill last painted

    void drawMainMenu(bool fullRepaint);
    static int menuRowTop(int i);  // y (top edge) of main-menu row i
    void drawSubmenu(bool fullRepaint);
    void drawDeviceInfo(bool fullRepaint);
    void drawThemeSettings(bool fullRepaint);
    void applyThemePreview();  // apply _themeChoice live + force a full repaint
    // The config page's QR + URL + PIN. Drawn once on entry and then left
    // alone - none of it changes while the page is open, and repainting a
    // QR code every tick is both pointless and slow over SPI.
    void drawCustomiseOnPhone(bool fullRepaint);
    void drawQrCode(const char* text, int boxX, int boxY, int boxSize);
    void openConfigPortal();   // beginSTA(), or beginAP() if there is no connection
    void closeConfigPortal();  // portal->stop(), safe to call when not running

    void drawUpdateSettings(bool fullRepaint);
    void drawRestartConfirm(bool fullRepaint);
    void drawFactoryResetConfirm(bool fullRepaint);

    void runUpdateCheck();  // blocking - shows "Checking..." then calls checkForUpdate()
    void runOTAUpdate();    // blocking - shows a progress bar then calls performOTAUpdate(); only returns on failure
};