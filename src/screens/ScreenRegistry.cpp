#include "ScreenRegistry.h"

#include "PlaneTrackerScreen.h"
#include "DashboardScreen.h"
#include "GamesScreen.h"
#include "FocusTimerScreen.h"
#include "CalendarScreen.h"
#include "SettingsScreen.h"

// Each factory takes (tft, portal) so the table can be walked uniformly;
// the ones whose constructor doesn't need the portal just drop it.
static Screen* makePlane   (TFT_eSPI* t, CaptivePortal*)       { return new PlaneTrackerScreen(t); }
static Screen* makeWeather (TFT_eSPI* t, CaptivePortal*)       { return new DashboardScreen(t); }
static Screen* makeGames   (TFT_eSPI* t, CaptivePortal* p)     { return new GamesScreen(t, p); }
static Screen* makeFocus   (TFT_eSPI* t, CaptivePortal*)       { return new FocusTimerScreen(t); }
static Screen* makeCalendar(TFT_eSPI* t, CaptivePortal* p)     { return new CalendarScreen(t, p); }
static Screen* makeSettings(TFT_eSPI* t, CaptivePortal* p)     { return new SettingsScreen(t, p); }

// The names here are the ones shown in the header's bracketed title and in
// the Settings > Home screen list, so they must match each screen's own
// getName(). DashboardScreen is deliberately "Weather" in both places.
static const ScreenDef kScreens[] = {
    { ScreenId::Plane,    "Planes",   makePlane    },
    { ScreenId::Weather,  "Weather",  makeWeather  },
    { ScreenId::Games,    "Games",    makeGames    },
    { ScreenId::Focus,    "Focus",    makeFocus    },
    { ScreenId::Calendar, "Calendar", makeCalendar },
    { ScreenId::Settings, "Settings", makeSettings },  // pinned - must stay last
};
static const int kCount = sizeof(kScreens) / sizeof(kScreens[0]);

namespace ScreenRegistry {

const ScreenDef* all()   { return kScreens; }
int              count() { return kCount; }

bool isPinned(ScreenId id) {
    return id == ScreenId::Settings;
}

const ScreenDef* byId(ScreenId id) {
    for (int i = 0; i < kCount; i++) {
        if (kScreens[i].id == id) return &kScreens[i];
    }
    return nullptr;  // unknown to this build (e.g. after a downgrade)
}

const ScreenDef* selectable(int index) {
    if (index < 0) return nullptr;
    int seen = 0;
    for (int i = 0; i < kCount; i++) {
        if (isPinned(kScreens[i].id)) continue;
        if (seen == index) return &kScreens[i];
        seen++;
    }
    return nullptr;
}

int selectableCount() {
    int n = 0;
    for (int i = 0; i < kCount; i++) {
        if (!isPinned(kScreens[i].id)) n++;
    }
    return n;
}

} // namespace ScreenRegistry
