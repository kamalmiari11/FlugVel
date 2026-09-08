// The device's configuration page.
//
// Split out of CaptivePortal.cpp because it is almost entirely a large
// literal: raw string literals keep the CSS and JS readable as CSS and JS
// rather than as a wall of backslash-escaped quotes. It is still a member
// of CaptivePortal - a member function may be defined in any translation
// unit - so it keeps direct access to the scan results, the saved location
// and the PIN without any of that needing to become public.
//
// Layout follows the FlugVel Setup Console design: a bracketed masthead, a
// numbered tab bar, and on the Customise tab a two-column split with the
// device slab on the left and its controls on the right. The grid collapses
// to one column below 1320px, which is what a phone actually gets.

#include "CaptivePortal.h"
#include <WiFi.h>
#include "../config/Config.h"
#include "../config/CalendarFeeds.h"
#include "../config/NotesSource.h"
#include "../screens/ScreenRegistry.h"
#include "../screens/GamesScreen.h"
#include "../screens/ScreenManager.h"
#include "../version.h"

extern const char *timezones[];
extern const int   timezoneCount;

// ---------------------------------------------------------------------------
// Small builders. Each appends one row to `p` in the console's .opt shape:
// name and description on the left, control on the right.
// ---------------------------------------------------------------------------
namespace {

String esc(const String &v) {
    String o; o.reserve(v.length() + 8);
    for (size_t i = 0; i < v.length(); i++) {
        char c = v[i];
        if      (c == '&')  o += "&amp;";
        else if (c == '<')  o += "&lt;";
        else if (c == '>')  o += "&gt;";
        else if (c == '"')  o += "&quot;";
        else                o += c;
    }
    return o;
}

void optOpen(String &p, const char *label, const char *desc) {
    p += "<div class=\"opt\"><span class=\"k\"><span class=\"kn\">";
    p += label;
    p += "</span>";
    if (desc && desc[0]) { p += "<span class=\"kd\">"; p += desc; p += "</span>"; }
    p += "</span><span class=\"v\">";
}

// Sliding pill switch. A hidden input carries the value so the browser
// submits it like any other field; the button is only the visible part.
void optSwitch(String &p, const char *name, const char *label, const char *desc, bool on) {
    optOpen(p, label, desc);
    p += "<input type=\"hidden\" name=\""; p += name;
    p += "\" id=\"h_"; p += name; p += "\" value=\""; p += (on ? "1" : "0"); p += "\">";
    p += "<button type=\"button\" class=\"tgl\" aria-pressed=\"";
    p += (on ? "true" : "false");
    p += "\" onclick=\"sw(this,'"; p += name; p += "')\"></button>";
    p += "</span></div>";
}

void optSelect(String &p, const char *name, const char *label, const char *desc,
               const char *const *labels, const int *values, int count, int value) {
    optOpen(p, label, desc);
    p += "<select name=\""; p += name; p += "\">";
    for (int i = 0; i < count; i++) {
        int v = values ? values[i] : i;
        p += "<option value=\""; p += String(v); p += "\"";
        if (v == value) p += " selected";
        p += ">"; p += labels[i]; p += "</option>";
    }
    p += "</select></span></div>";
}

void optRange(String &p, const char *name, const char *label, const char *desc,
              int lo, int hi, int step, int value, const char *suffix) {
    optOpen(p, label, desc);
    p += "<input type=\"range\" name=\""; p += name;
    p += "\" min=\""; p += String(lo);
    p += "\" max=\""; p += String(hi);
    p += "\" step=\""; p += String(step);
    p += "\" value=\""; p += String(value);
    p += "\" oninput=\"num(this)\"><span class=\"num\" data-sfx=\"";
    p += (suffix ? suffix : "");
    p += "\">"; p += String(value); p += (suffix ? suffix : "");
    p += "</span></span></div>";
}

void optText(String &p, const char *name, const char *label, const char *desc,
             const String &value, int maxlen, const char *placeholder) {
    optOpen(p, label, desc);
    p += "<input type=\"text\" name=\""; p += name;
    p += "\" value=\""; p += esc(value); p += "\"";
    if (maxlen > 0) { p += " maxlength=\""; p += String(maxlen); p += "\""; }
    if (placeholder) { p += " placeholder=\""; p += placeholder; p += "\""; }
    p += "></span></div>";
}

} // namespace

// ---------------------------------------------------------------------------


// Shown after the final Save. The device closes the portal a tick later, so
// this is the last thing the phone will get - it has to say what happened
// without offering a link back to a page that is about to stop answering.
String CaptivePortal::donePageHtml(bool reconnecting)
{
    String p = R"HTML(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1.0"><meta charset="utf-8">
<title>FlugVel</title><style>
body{margin:0;padding:70px 18px;background:#c8d0b8;color:#23271d;text-align:center;
 font-family:ui-monospace,Menlo,Consolas,monospace;font-size:15px;line-height:1.6}
h1{font-size:20px;letter-spacing:4px;margin:0 0 4px}
h1 b{color:#8a9078}
p{color:#5a6048;font-size:13px;max-width:34ch;margin:10px auto}
.big{color:#23271d;font-size:15px;letter-spacing:1px;text-transform:uppercase;margin-top:22px}
</style></head><body>
<h1><b>[</b> FLUGVEL <b>]</b></h1>
<p class="big">Saved</p>
)HTML";

    p += reconnecting
        ? "<p>The device is reconnecting to the network you chose. Give it a few "
          "seconds &mdash; the panel will show when it is back.</p>"
        : "<p>Your settings are on the device.</p>";

    p += "<p>This page has closed. To change anything else, open "
         "Settings &rsaquo; Configure on phone on the device again.</p>"
         "</body></html>";
    return p;
}
String CaptivePortal::configPageHtml(bool justSaved)
{
    const Config &c = ConfigStore::get();

    String p;
    p.reserve(24000);   // one allocation rather than a hundred reallocs

    // ================= head + styles =================
    p += R"HTML(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1.0"><meta charset="utf-8">
<title>FlugVel</title><style>
:root{--bg:#c8d0b8;--surface:#dbe1cd;--field:#eef1e4;--fg:#23271d;--dim:#5a6048;
--rule:#8a9078;--accent:#c05a1e;--req:#9c3312;
--mono:ui-monospace,"SF Mono",Menlo,Consolas,monospace;}
*{box-sizing:border-box}
body{margin:0;padding:26px 14px 60px;background:var(--bg);color:var(--fg);
 font-family:var(--mono);font-size:15px;line-height:1.5}
.shell{max-width:1320px;margin:0 auto}
.masthead{text-align:center;margin-bottom:22px}
h1{font-size:26px;letter-spacing:6px;margin:0;font-weight:700}
h1 b{color:var(--rule);font-weight:700}
.masthead p{margin:6px 0 0;font-size:11px;letter-spacing:3px;text-transform:uppercase;color:var(--dim)}
.tabs{display:flex;margin:22px 0 0;border:1px solid var(--rule);background:var(--surface)}
.tab{flex:1;display:flex;align-items:baseline;justify-content:center;gap:9px;
 padding:13px 6px;background:transparent;border:0;border-right:1px solid var(--rule);
 font:inherit;font-size:13px;letter-spacing:2px;text-transform:uppercase;color:var(--dim);cursor:pointer}
.tab:last-child{border-right:0}
.tab .n{font-size:10px;letter-spacing:1px;color:var(--rule)}
.tab[aria-selected=true]{background:var(--fg);color:var(--field)}
.tab[aria-selected=true] .n{color:var(--accent)}
.tab:focus-visible{outline:2px solid var(--accent);outline-offset:-3px}
[role=tabpanel][hidden]{display:none}
fieldset{border:1px solid var(--rule);background:var(--surface);padding:10px 16px 18px;margin:0 0 14px}
legend{font-size:12px;letter-spacing:2px;text-transform:uppercase;padding:0 6px}
label{display:block;font-size:11px;letter-spacing:1px;text-transform:uppercase;color:var(--dim);margin:14px 0 3px}
input,select{width:100%;padding:11px 10px;border:1px solid var(--rule);background:var(--field);
 color:var(--fg);font:inherit;border-radius:0;-webkit-appearance:none;appearance:none}
input:focus,select:focus{outline:2px solid var(--accent);outline-offset:-1px}
.badge{font-size:10px;letter-spacing:1px;padding:2px 6px;text-transform:uppercase;border:1px solid var(--dim);color:var(--dim)}
.badge.req{color:#fff;background:var(--req);border-color:var(--req)}
.tag{float:right;font-size:10px;letter-spacing:1px;color:var(--dim)}
.tag.rec{color:var(--accent)}
.hint{font-size:12px;color:var(--dim);margin:6px 0 2px;line-height:1.45}
.chk{display:flex;align-items:center;gap:8px;text-transform:none;letter-spacing:0;
 font-size:13px;color:var(--fg);margin-top:12px}
.chk input{width:auto;padding:0}
.two{display:flex;gap:12px}.two>div{flex:1}
button.act{width:100%;padding:14px;margin-top:10px;border:1px solid var(--accent);
 background:var(--accent);color:#fff;font:inherit;font-size:14px;letter-spacing:2px;
 text-transform:uppercase;cursor:pointer;border-radius:0}
button.act.ghost{background:transparent;color:var(--fg);border-color:var(--rule);letter-spacing:1px}
.ok{border:1px solid var(--accent);color:var(--accent);padding:9px 10px;margin:0 0 14px;font-size:12px;text-align:center}
.custom-grid{display:grid;grid-template-columns:minmax(0,850px) minmax(0,1fr);gap:20px;align-items:start}
@media(max-width:1320px){.custom-grid{grid-template-columns:1fr}.device-col{position:static}}
.device-col{position:sticky;top:20px;min-width:0;overflow:hidden}
.bezel{display:flex;align-items:center;gap:18px;background:#1d1f1a;padding:22px;
 border:1px solid var(--rule);box-shadow:inset 0 0 0 2px #2c2f27;width:fit-content;max-width:100%}
.bezel.lefthand{flex-direction:row-reverse}
.viewport{overflow:hidden;flex:none;position:relative}
.stage{position:absolute;left:0;top:0;width:320px;height:240px;transform-origin:top left;
 background:#c8d0b8;font-family:var(--mono)}
.stage s,.stage u{position:absolute;display:block;text-decoration:none}
.stage u{white-space:pre;line-height:1;font-style:normal}
.panel{width:150px;flex:none;align-self:stretch;display:flex;flex-direction:column;
 align-items:center;justify-content:space-evenly;border-left:1px solid #2c2f27}
/* Turned around, the encoder that sat top-right ends up bottom-left and
   the KO switch above it - so the column reverses as well as swapping
   side. */
.bezel.lefthand .panel{border-left:0;border-right:1px solid #2c2f27;
 flex-direction:column-reverse}
.encoder{position:relative;width:104px;height:104px;border-radius:50%;border:1px solid #000;
 padding:0;cursor:pointer;flex:none;
 background:repeating-conic-gradient(#0c0c0c 0 2.6deg,#242424 2.6deg 5.2deg),
 radial-gradient(circle at 50% 38%,#3a3a3a,#0f0f0f 70%);
 box-shadow:inset 0 3px 9px rgba(255,255,255,.10),inset 0 -7px 16px rgba(0,0,0,.75),0 5px 12px rgba(0,0,0,.55)}
.encoder::after{content:"";position:absolute;inset:12px;border-radius:50%;
 background:radial-gradient(circle at 50% 34%,#333,#0d0d0d 72%)}
.encoder .mark{position:absolute;left:50%;top:7px;width:4px;height:27px;border-radius:2px;
 background:#e9e9e9;transform:translateX(-50%);z-index:2;transform-origin:50% 45px;
 transition:transform .18s ease-out}
.kobtn{position:relative;width:54px;height:54px;flex:none;cursor:pointer;padding:0;
 background:linear-gradient(#8d7458,#6b5540);border:1px solid #46392b;box-shadow:0 4px 9px rgba(0,0,0,.5)}
.kobtn::after{content:"";position:absolute;left:50%;top:50%;width:26px;height:26px;border-radius:50%;
 transform:translate(-50%,-50%);background:radial-gradient(circle at 44% 34%,#4e4e4e,#111 72%)}
.caption{font-size:11px;color:var(--dim);letter-spacing:1px;text-transform:uppercase;
 margin:10px 2px 0;display:flex;justify-content:space-between;gap:10px;flex-wrap:wrap}
.slist{list-style:none;margin:0;padding:0;display:flex;flex-direction:column;gap:6px}
.sitem{border:1px solid var(--rule);background:var(--field)}
.sitem.pinned{background:var(--surface)}
.srow{display:grid;grid-template-columns:20px 1fr auto auto auto;align-items:center;gap:10px;padding:9px 10px}
.sitem.active{border-color:var(--fg)}
.sitem.active .srow{background:var(--fg);color:var(--field)}
.sitem.active .sname{color:var(--field)}
.sitem.active .smeta,.sitem.active .caret{color:var(--rule)}
.sitem.off .sname,.sitem.off .smeta{opacity:.45}
.grip{color:var(--rule);font-size:13px;letter-spacing:-1px;user-select:none}
.sitem.pinned .grip{opacity:.3}
.sname{background:none;border:0;color:inherit;font:inherit;font-size:13px;letter-spacing:1px;
 text-transform:uppercase;text-align:left;cursor:pointer;padding:0;width:auto}
.smeta{font-size:9px;letter-spacing:1px;text-transform:uppercase;color:var(--dim)}
.smeta.home{color:var(--accent)}
.caret{background:none;border:0;color:var(--dim);font:inherit;font-size:11px;cursor:pointer;padding:0 2px;width:auto}
.mv{background:none;border:0;color:var(--rule);font:inherit;font-size:11px;cursor:pointer;padding:0 1px;width:auto}
.sitem.active .mv{color:var(--rule)}
.tgl{width:34px;height:18px;border:1px solid var(--rule);background:var(--bg);position:relative;
 cursor:pointer;padding:0;flex:none}
.tgl::after{content:"";position:absolute;top:1px;left:1px;width:14px;height:14px;background:var(--rule);
 transition:transform .12s,background .12s}
.tgl[aria-pressed=true]{border-color:var(--accent)}
.tgl[aria-pressed=true]::after{transform:translateX(16px);background:var(--accent)}
.tgl:disabled{opacity:.35;cursor:not-allowed}
@media(prefers-reduced-motion:reduce){.tgl::after,.encoder .mark{transition:none}}
.sopts{border-top:1px solid var(--rule);background:var(--surface);padding:4px 12px 12px}
.opt{display:grid;grid-template-columns:1fr auto;align-items:center;gap:12px;padding:9px 0}
.opt+.opt{border-top:1px solid rgba(138,144,120,.35)}
.opt .k{display:flex;flex-direction:column;gap:2px;min-width:0}
.opt .kn{font-size:12px;letter-spacing:.5px}
.opt .kd{font-size:10px;color:var(--dim);letter-spacing:.5px}
.opt .v{display:flex;align-items:center;gap:8px;justify-self:end}
.opt select,.opt input[type=text]{width:auto;min-width:120px;max-width:210px;padding:6px 8px;font-size:12px}
.opt input[type=range]{width:110px;padding:0;border:0;background:transparent;appearance:auto;
 -webkit-appearance:auto;accent-color:var(--accent)}
.opt .num{font-size:11px;color:var(--dim);min-width:56px;text-align:right;font-variant-numeric:tabular-nums}
.feed{display:grid;grid-template-columns:92px minmax(0,1fr);gap:6px;margin-bottom:6px}
.feed input{padding:8px;font-size:12px;min-width:0;width:100%}
.pvn{font-size:11px;color:var(--dim);letter-spacing:1px;text-transform:uppercase;text-align:center;margin:8px 0 0}
</style></head><body><div class="shell">
<div class="masthead"><h1><b>[</b> FLUGVEL <b>]</b></h1><p>Device setup</p>
<nav class="tabs" role="tablist">
<button class="tab" type="button" role="tab" id="t0" onclick="tab(0)"><span class="n">01</span> Wi-Fi</button>
<button class="tab" type="button" role="tab" id="t1" onclick="tab(1)"><span class="n">02</span> Location</button>
<button class="tab" type="button" role="tab" id="t2" onclick="tab(2)"><span class="n">03</span> Customise</button>
</nav></div>
)HTML";

    if (justSaved) p += "<p class=\"ok\">Saved to the device.</p>";

    // ================= 01 Wi-Fi + 02 Location =================
    // During first-run setup the two are one form: the credentials have to
    // be committed together with the location, and the device reboots into
    // the connection straight afterwards. Once it is online they become two
    // independent forms, because someone editing their city should not have
    // to retype a Wi-Fi password or trigger a reconnect to do it.
    // One form across all three steps with a single Save at the end. The
    // tabs are a wizard, not three independent editors - Next validates the
    // step you are on before it will move you forward.
    p += "<form method=\"POST\" action=\"/save\" id=\"wz\">"
         "<input type=\"hidden\" name=\"manualLocation\" value=\"1\">";
    p += "<section role=\"tabpanel\" id=\"p0\"><fieldset>"
         "<legend>Wi-Fi &nbsp;<span class=\"badge req\">Required</span></legend>"
         "<label>Network</label>";

    // Fold in the async scan started when the portal opened, if it has
    // landed. If it has not, offer the network already in use so the form
    // is still submittable, plus the Refresh below.
    collectScanResults();
    if (scannedNetworksHTML.length() > 0) {
        p += scannedNetworksHTML;
    } else {
        String cur = WiFi.SSID();
        p += "<select name=\"ssid\" required><option value=\"" + esc(cur) + "\" selected>" +
             (cur.length() ? esc(cur) : String("Still scanning...")) + "</option></select>"
             "<p class=\"hint\">Still looking for networks &mdash; press Refresh in a moment "
             "to see the full list.</p>";
    }

    // Left blank on purpose: putting the real password in the page source
    // would hand it to anything that can read the response. Blank means
    // "keep the stored one" on the way back in.
    String savedSsid, savedPass;
    bool haveSaved = loadWiFiEEPROM(savedSsid, savedPass);
    p += "<label for=\"pw\">Password</label>"
         "<input type=\"password\" name=\"password\" id=\"pw\" autocomplete=\"new-password\">"
         "<label class=\"chk\"><input type=\"checkbox\" onclick=\"pwv(this)\"> Show password</label>"
         "<p class=\"hint\">2.4 GHz only &mdash; the ESP32 has no 5 GHz radio, so 5 GHz "
         "networks never appear here.</p>";
    if (haveSaved) {
        p += "<p class=\"hint\">Leave the password empty to keep the current one.</p>";
    }
    if (!_apMode) {
        p += "<p class=\"hint\">Currently connected to <b>" + esc(WiFi.SSID()) +
             "</b>. Choosing a different network reconnects the device when you save.</p>";
    }
    p += "</fieldset>"
         "<a class=\"act ghost\" style=\"display:block;text-align:center;text-decoration:none\" "
         "href=\"/refresh\">Refresh networks</a>"
         "<button class=\"act\" type=\"button\" onclick=\"next(0)\">Next: location</button>"
         "</section>";

    p += "<section role=\"tabpanel\" id=\"p1\" hidden><fieldset>"
         "<legend>Location &nbsp;<span class=\"badge\">Optional</span></legend>"
         "<p class=\"hint\">All optional. Typing a city is enough &mdash; the device looks up its "
         "coordinates. Leave everything blank and it estimates your location from the network "
         "(less precise).</p>"
         "<label>City <span class=\"tag rec\">recommended</span></label>"
         "<input type=\"text\" name=\"city\" value=\"" + esc(_city) + "\">"
         "<label>Country <span class=\"tag\">optional</span></label>"
         "<input type=\"text\" name=\"country\" value=\"" + esc(_country) + "\">"
         "<div class=\"two\"><div><label>Latitude <span class=\"tag\">optional</span></label>"
         "<input type=\"text\" name=\"latitude\" inputmode=\"decimal\" value=\"" +
         (_latitude != 0 ? String(_latitude, 4) : String("")) + "\"></div>"
         "<div><label>Longitude <span class=\"tag\">optional</span></label>"
         "<input type=\"text\" name=\"longitude\" inputmode=\"decimal\" value=\"" +
         (_longitude != 0 ? String(_longitude, 4) : String("")) + "\"></div></div>"
         "<label>Timezone <span class=\"tag\">optional</span></label>"
         "<select name=\"timezone\"><option value=\"\">Auto-detect</option>";
    for (int i = 0; i < timezoneCount; i++) {
        p += "<option value=\"" + String(timezones[i]) + "\"";
        if (_timezone == timezones[i]) p += " selected";
        p += ">" + String(timezones[i]) + "</option>";
    }
    p += "</select>"
         "<button class=\"act ghost\" type=\"button\" onclick=\"clr()\">Clear location fields</button>"
         "<p class=\"hint\">Clearing and saving makes the device work its location out from "
         "the network instead, which is less precise but needs nothing from you.</p>"
         "</fieldset>";
    p += "<button class=\"act\" type=\"button\" onclick=\"next(1)\">Next: customise</button>"
         "</section>";

    // ================= 03 Customise =================
    p += "<section role=\"tabpanel\" id=\"p2\" hidden>"
         "<input type=\"hidden\" name=\"screens\" id=\"sf\">"
         "<div class=\"custom-grid\"><div class=\"device-col\">"
         "<div class=\"bezel\" id=\"bz\"><div class=\"viewport\" id=\"vp\">"
         "<div class=\"stage\" id=\"stg\"></div></div>"
         "<div class=\"panel\">"
         "<button class=\"encoder\" type=\"button\" onclick=\"step()\"><span class=\"mark\" id=\"mk\"></span></button>"
         "<button class=\"kobtn\" type=\"button\"></button>"
         "</div></div>"
         "<p class=\"caption\"><span>ST7789 &middot; 320&times;240</span>"
         "<span id=\"pvn\">&mdash;</span></p></div>"
         "<div class=\"controls-col\">";

    // ---- screen list, each row carrying its own options ----
    p += "<fieldset><legend>Screens</legend>"
         "<p class=\"hint\">Reorder with the arrows &mdash; the first shown screen is where the "
         "device boots. Open a screen to set what it does. Settings is pinned last; it is the "
         "only way back out.</p><ul class=\"slist\" id=\"sl\">";

    bool seen[256];
    memset(seen, 0, sizeof(seen));

    auto screenRow = [&](const ScreenDef *d, bool enabled) {
        p += "<li class=\"sitem";
        if (!enabled) p += " off";
        p += "\" data-id=\"" + String((int)d->id) + "\" data-on=\"" + (enabled ? "1" : "0") + "\">";
        p += "<div class=\"srow\"><span class=\"grip\">&#8942;&#8942;</span>"
             "<button class=\"sname\" type=\"button\" onclick=\"opn(this)\">" + String(d->name) + "</button>"
             "<span class=\"smeta\"></span>"
             "<span><button class=\"mv\" type=\"button\" onclick=\"mv(this,-1)\">&#9650;</button>"
             "<button class=\"mv\" type=\"button\" onclick=\"mv(this,1)\">&#9660;</button></span>"
             "<button class=\"caret\" type=\"button\" onclick=\"opn(this)\">&#9660;</button>"
             "<button class=\"tgl\" type=\"button\" aria-pressed=\"";
        p += (enabled ? "true" : "false");
        p += "\" onclick=\"tg(this)\"></button></div>";

        p += "<div class=\"sopts\" hidden>";
        switch (d->id) {
            case ScreenId::Plane: {
                optText  (p, "pidle", "Idle text", "bounces when nothing is overhead", _bounceText, 3, "KML");
                static const char *const kFace[] = { "Default (West)", "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
                int fi = 0;
                for (int i = 1; i < 9; i++) if (_facingDirection == kFace[i]) { fi = i; break; }
                optSelect(p, "pface", "Screen faces", "rotates the flyover to match reality", kFace, nullptr, 9, fi);
                static const char *const kIv[] = { "30 sec", "1 min", "2 min", "5 min", "10 min", "15 min", "30 min" };
                static const int kIvV[] = { 0, 1, 2, 3, 4, 5, 6 };
                static const int kIvS[] = { 30, 60, 120, 300, 600, 900, 1800 };
                int ii = 0;
                for (int i = 0; i < 7; i++) if (kIvS[i] == _flightCheckIntervalSec) { ii = i; break; }
                optSelect(p, "piv", "Check interval", "how often the nearest flight is fetched", kIv, kIvV, 7, ii);
                static const char *const kAlt[] = { "No limit", "1 000 m", "3 000 m", "6 000 m" };
                int ai = 0;
                if (c.planeMinAlt100m >= 60) ai = 3; else if (c.planeMinAlt100m >= 30) ai = 2;
                else if (c.planeMinAlt100m >= 10) ai = 1;
                optSelect(p, "palt", "Ignore below", "skips helicopters and circuit traffic", kAlt, nullptr, 4, ai);
                optSwitch(p, "pfly", "Fly-over animation", "blocks input while it plays", c.planeFlyover);
                optSwitch(p, "pq",   "Quote of the day",   "fills the strip below the box", c.planeQuote);
                break;
            }
            case ScreenId::Weather: {
                static const char *const kDays[] = { "3 days", "4 days", "5 days", "6 days", "7 days" };
                static const int kDaysV[] = { 3, 4, 5, 6, 7 };
                optSelect(p, "wdays", "Forecast days", "rows in the week table", kDays, kDaysV, 5, c.weatherDays);
                static const char *const kRef[] = { "30 min", "1 hour", "3 hours", "6 hours" };
                static const int kRefV[] = { 0, 1, 2, 3 };
                static const int kRef10[] = { 3, 6, 18, 36 };
                int ri = 0;
                for (int i = 0; i < 4; i++) if (kRef10[i] == c.weatherRefresh10) { ri = i; break; }
                optSelect(p, "wref", "Refresh every", "how often the forecast is re-fetched", kRef, kRefV, 4, ri);
                optSwitch(p, "wicon", "Condition icons", "a small glyph beside each day", c.weatherIcons);
                optSwitch(p, "wpre",  "Prefetch hourly", "loads each day so detail opens instantly", c.weatherPrefetch);
                break;
            }
            case ScreenId::Games: {
                for (int i = 0; i < GamesScreen::entryCount() && i < 8; i++) {
                    bool on = (c.gamesMask == 0) || ((c.gamesMask >> i) & 1);
                    String key = String("g") + i;
                    optSwitch(p, key.c_str(), GamesScreen::entryName(i),
                              GamesScreen::entryPlayable(i) ? "playable" : "not built yet", on);
                }
                break;
            }
            case ScreenId::Focus: {
                optRange (p, "ffoc", "Focus length", "starting value on the set screen", 5, 60, 5, c.focusMinutes, " min");
                optRange (p, "fbrk", "Break length", "", 1, 30, 1, c.breakMinutes, " min");
                optSwitch(p, "flock", "Lock while running", "stop a stray press abandoning a session", c.focusLock);
                optSwitch(p, "fauto", "Auto-start break",   "roll into the break when focus ends", c.focusAutoBreak);
                break;
            }
            case ScreenId::Calendar: {
                p += "<p class=\"hint\">Read-only iCal addresses. Events from every filled-in "
                     "calendar merge into one list. Clear a link to drop it.</p>";
                for (int i = 0; i < CAL_MAX_FEEDS; i++) {
                    const CalFeed &f = CalendarFeeds::at(i);
                    bool has = i < CalendarFeeds::count();
                    p += "<div class=\"feed\"><input type=\"text\" name=\"cn" + String(i) +
                         "\" maxlength=\"12\" placeholder=\"Label\" value=\"" +
                         (has ? esc(f.name) : String("")) + "\">"
                         "<input type=\"url\" name=\"cu" + String(i) +
                         "\" placeholder=\"https://.../basic.ics\" value=\"" +
                         (has ? esc(f.url) : String("")) + "\"></div>";
                }
                static const char *const kLook[] = { "14 days", "30 days", "60 days", "90 days" };
                static const int kLookV[] = { 14, 30, 60, 90 };
                optSelect(p, "clook", "Look ahead", "how far forward events are listed", kLook, kLookV, 4, c.calLookaheadDays);
                static const char *const kMax[] = { "10 events", "20 events", "40 events", "60 events" };
                static const int kMaxV[] = { 10, 20, 40, 60 };
                optSelect(p, "cmax", "Keep at most", "cap across all feeds", kMax, kMaxV, 4, c.calMaxEvents);
                optSwitch(p, "call",  "All-day events",   "include events with no start time", c.calAllDay);
                optSwitch(p, "cmrg",  "Merge duplicates", "one row when an event is in several calendars", c.calMerge);
                optSwitch(p, "cpast", "Hide past today",  "drops today's events once they have finished", c.calHidePast);
                break;
            }
            case ScreenId::Notes: {
                p += "<p class=\"hint\">Free at notion.so/my-integrations: create an "
                     "internal integration with <strong>read AND update</strong> content "
                     "capability (update is needed to tick things off from the device "
                     "itself), copy its secret, then share your notes page with it from "
                     "the page&#39;s &middot;&middot;&middot; menu &rsaquo; Connections. "
                     "Paste both below.</p>";
                optText(p, "ntok", "Integration token", "starts with secret_ or ntn_",
                        NotesSource::token(), 120, "secret_...");
                optText(p, "npage", "Page", "the page's URL, or just its id",
                        NotesSource::pageId(), 200, "https://www.notion.so/...");
                static const char *const kNMax[] = { "5 items", "8 items", "10 items", "15 items" };
                static const int kNMaxV[] = { 5, 8, 10, 15 };
                int nmi = 1;
                for (int i = 0; i < 4; i++) if (kNMaxV[i] == NotesSource::maxItems()) { nmi = i; break; }
                optSelect(p, "nmax", "Items shown", "how many rows the screen keeps", kNMax, kNMaxV, 4, nmi);
                optSwitch(p, "nchk", "Show checked items", "keep finished to-dos instead of dropping them", NotesSource::showChecked());
                optSwitch(p, "ngrp", "Group notes by day", "collapse into day headings (like \"Monday\"), today opened automatically - turn the knob to move between rows, press to open/close a day or tick a to-do", NotesSource::groupByDay());
                break;
            }
            default:
                p += "<p class=\"hint\" style=\"margin:10px 0 2px\">Always present and always last "
                     "in the cycle &mdash; it is the only way back to this page from the device.</p>";
                break;
        }
        p += "</div></li>";
    };

    for (int i = 0; i < c.screenCount; i++) {
        const ScreenDef *d = ScreenRegistry::byId((ScreenId)c.screens[i].id);
        if (!d || ScreenRegistry::isPinned(d->id)) continue;
        seen[c.screens[i].id] = true;
        screenRow(d, c.screens[i].enabled != 0);
    }
    for (int i = 0; i < ScreenRegistry::count(); i++) {
        const ScreenDef &d = ScreenRegistry::all()[i];
        if (ScreenRegistry::isPinned(d.id) || seen[(uint8_t)d.id]) continue;
        screenRow(&d, true);   // added by a firmware update
    }
    // Settings, shown pinned and not submitted
    p += "<li class=\"sitem pinned\"><div class=\"srow\">"
         "<span class=\"grip\">&middot;&middot;</span>"
         "<span class=\"sname\">Settings</span>"
         "<span class=\"smeta\">pinned last</span><span></span><span></span>"
         "<button class=\"tgl\" type=\"button\" aria-pressed=\"true\" disabled></button>"
         "</div></li></ul></fieldset>";

    // ---- device fieldset ----
    p += "<fieldset><legend>Device</legend>";
    optRange (p, "bright", "Brightness", "backlight level", 10, 100, 5, c.brightness, "%");
    {
        static const char *const kClock[] = { "24h D-M", "24h M-D", "12h D-M", "12h M-D" };
        optSelect(p, "clock", "Clock format", "date order and 12 / 24 hour", kClock, nullptr, 4, c.dateTimeFormat);
        static const char *const kUnits[] = { "Metric", "Imperial" };
        optSelect(p, "units", "Units", "temperature, speed and altitude", kUnits, nullptr, 2, c.imperial ? 1 : 0);
        static const char *const kPager[] = { "Dots", "Counter", "Hidden" };
        optSelect(p, "pager", "Pager", "dots crowd the header past about ten screens", kPager, nullptr, 3, c.pagerStyle);
        static const char *const kKnob[] = { "Normal", "Reversed" };
        optSelect(p, "knob", "Knob direction", "flip if turning feels backwards", kKnob, nullptr, 2, c.knobReversed ? 1 : 0);
        static const char *const kStart[] = { "Home screen", "Last used" };
        optSelect(p, "start", "On power-up", "where the device lands after a restart", kStart, nullptr, 2, c.startupResume ? 1 : 0);
    }
    optSwitch(p, "legend",   "Action legend", "the strip showing what knob and button do", c.legend);
    optSwitch(p, "showdate", "Date in header", "off leaves more room for pager dots", c.showDate);
    optSwitch(p, "lefth",    "Left-handed", "turn the device around so the knob is on the other side", c.leftHanded);
    p += "</fieldset>";

    p += "<button class=\"act\" type=\"submit\">Save &amp; update device</button>"
         "<p class=\"hint\" style=\"text-align:center\">Saves Wi-Fi, location and "
         "customisation together, then closes this page on the device.</p>"
         "</div></div></section></form>";

    // ================= script =================
    p += R"HTML(<script>
var TH={bg:'#c8d0b8',fg:'#2a2e22',dim:'#4a5038',rule:'#8a9078',ac:'#c05a1e'};
var W=320,H=240,HH=20,LH=30,pos=0,ang=0;
/* Written by the device: the screen actually on the panel right now, so the
   preview opens showing what you are looking at rather than always home. */
var HAVESAVED=)HTML";
    p += haveSaved ? "true" : "false";
    p += R"HTML(;
var startId=')HTML";
    p += String((int)ScreenManager::showingId());
    p += R"HTML(';
function startPos(){var c=cyc();for(var i=0;i<c.length;i++)
if(c[i].dataset.id==startId)return i;return c.length;}
var reached=0;   /* furthest step unlocked so far */
function tab(i){if(i>reached)return;for(var k=0;k<3;k++){
document.getElementById('t'+k).setAttribute('aria-selected',k==i);
document.getElementById('p'+k).hidden=(k!=i);
document.getElementById('t'+k).style.opacity=(k>reached)?'0.45':'';}
if(i==2)pv();window.scrollTo(0,0);}
/* Next: check what this step needs before unlocking the following one.
   Only Wi-Fi has anything required - a network has to be chosen, and on a
   device with nothing stored a password has to be given, since there is no
   current one for "leave blank" to fall back to. */
function next(from){
if(from==0){var sel=document.querySelector('[name=ssid]'),pw=document.getElementById('pw');
if(!sel||!sel.value){bad(sel,'Choose a network first.');return;}
if(!HAVESAVED&&!pw.value){bad(pw,'This network needs a password.');return;}}
reached=Math.max(reached,from+1);tab(from+1);}
function bad(el,msg){var w=document.getElementById('warn');
if(!w){w=document.createElement('p');w.id='warn';w.className='ok';
w.style.borderColor='#9c3312';w.style.color='#9c3312';
document.querySelector('.shell').insertBefore(w,document.querySelector('[role=tabpanel]'));}
w.textContent=msg;if(el&&el.focus)el.focus();
setTimeout(function(){if(w.parentNode)w.parentNode.removeChild(w);},4000);}
function pwv(b){document.getElementById('pw').type=b.checked?'text':'password';}
/* Blank every location field. Saving after this hands the job to the
   device's own IP lookup rather than leaving stale values behind. */
function clr(){['city','country','latitude','longitude'].forEach(function(k){
var e=document.querySelector('[name='+k+']');if(e)e.value='';});
var t=document.querySelector('[name=timezone]');if(t)t.selectedIndex=0;}
function num(r){var o=r.parentNode.querySelector('.num');if(o)o.textContent=r.value+(o.dataset.sfx||'');pv();}
function sw(b,n){var h=document.getElementById('h_'+n),on=h.value=='1';h.value=on?'0':'1';
b.setAttribute('aria-pressed',!on);pv();}
/* The rows that are actually in the cycle, in order. The preview walks
   this same list, which is what keeps the two in step. */
function cyc(){var c=[];document.querySelectorAll('#sl li[data-id]').forEach(function(li){
if(li.dataset.on=='1')c.push(li);});return c;}
/* Collapse every open row. */
function shut(){document.querySelectorAll('.sitem').forEach(function(x){
x.classList.remove('active');var o=x.querySelector('.sopts');
if(o&&!o.hidden){o.hidden=true;var k=x.querySelector('.caret');if(k)k.innerHTML='&#9660;';}});}
/* Open exactly the row the preview is showing. Position past the end is
   Settings, which has no options of its own - everything just closes. */
function sync(){var c=cyc();shut();if(pos>=c.length)return;var li=c[pos];
li.classList.add('active');var o=li.querySelector('.sopts');
if(o){o.hidden=false;var k=li.querySelector('.caret');if(k)k.innerHTML='&#9650;';}}
/* Clicking a row's name or caret. A shown screen also moves the preview to
   it; a hidden one has no place in the cycle, so it opens on its own and
   leaves the preview where it was. */
function opn(b){var li=b.closest('.sitem'),o=li.querySelector('.sopts');if(!o)return;
var wasOpen=!o.hidden;
if(li.dataset.on=='1'){var c=cyc(),i=c.indexOf(li);
if(wasOpen){shut();}else{pos=i;sync();}pv();return;}
shut();if(!wasOpen){o.hidden=false;li.querySelector('.caret').innerHTML='&#9650;';}}
function mv(b,d){var li=b.closest('.sitem'),ul=li.parentNode;
if(d<0&&li.previousElementSibling)ul.insertBefore(li,li.previousElementSibling);
if(d>0&&li.nextElementSibling&&!li.nextElementSibling.classList.contains('pinned'))
ul.insertBefore(li.nextElementSibling,li);tags();sync();pv();}
function tg(b){var li=b.closest('.sitem'),on=li.dataset.on=='1';
if(on&&document.querySelectorAll('#sl li[data-on="1"]').length<=1)return;
li.dataset.on=on?'0':'1';li.classList.toggle('off',on);b.setAttribute('aria-pressed',!on);
if(pos>=cyc().length)pos=0;tags();sync();pv();}
function tags(){var f=true;document.querySelectorAll('#sl li[data-id]').forEach(function(li){
var t=li.querySelector('.smeta'),on=li.dataset.on=='1';
t.textContent=on?(f?'home':'in cycle'):'hidden';t.className='smeta'+(on&&f?' home':'');if(on)f=false;});}
function pack(){var o=[];document.querySelectorAll('#sl li[data-id]').forEach(function(li){
o.push(li.dataset.id+':'+li.dataset.on);});document.getElementById('sf').value=o.join(',');}
document.getElementById('wz').addEventListener('submit',pack);
function step(){pos=(pos+1)%(cyc().length+1);ang+=34;
document.getElementById('mk').style.transform='translateX(-50%) rotate('+ang+'deg)';
sync();pv();}
function R(x,y,w,h,c){return '<s style="left:'+x+'px;top:'+y+'px;width:'+w+'px;height:'+h+'px;background:'+c+'"></s>';}
function T(x,y,t,c,z,a){z=z||8;var w=t.length*z*0.75,l=x;if(a=='r')l=x-w;if(a=='c')l=x-w/2;
return '<u style="left:'+l+'px;top:'+y+'px;color:'+c+';font-size:'+z+'px">'+
t.replace(/&/g,'&amp;').replace(/</g,'&lt;')+'</u>';}
function D(cx,cy,f,a,b){return '<s style="left:'+(cx-2)+'px;top:'+(cy-2)+
'px;width:4px;height:4px;border-radius:50%;'+(f?'background:'+a:'border:1px solid '+b)+'"></s>';}
function val(n,d){var e=document.querySelector('[name='+n+']');return e?e.value:d;}
/* Celsius in, whatever the Units setting says out. */
function cv(c,sp){var im=val('units','0')=='1';
var v=im?Math.round(c*9/5+32):c;return v+(sp?' ':'')+(im?'F':'C');}
function body(id,t){
if(id=='0'){var o=R(0,26,W,1,t.fg)+R(0,193,W,1,t.fg)+R(0,26,1,168,t.fg)+R(319,26,1,168,t.fg);
return o+T(18,36,'KLM1071',t.ac,16)+R(14,60,292,1,t.rule)
+T(18,74,'ALTITUDE',t.dim)+T(18,88,'10500 m',t.fg,16)
+T(160,74,'SPEED',t.dim)+T(160,88,'842 km/h',t.fg,16)
+T(18,118,'HEADING',t.dim)+T(18,132,'265 W',t.fg,16)
+T(160,118,'FROM',t.dim)+T(160,132,'Netherlands',t.fg,16);}
if(id=='1'){var o=T(10,24,cv(12,true),t.fg,24)+T(100,26,'Partly cloudy',t.dim)
+T(100,44,'Amsterdam, NL',t.dim)+T(10,58,'Day',t.fg)+T(65,58,'Condition',t.fg)
+T(240,58,'Hi',t.fg)+T(280,58,'Lo',t.fg)+R(10,69,300,1,t.rule);
var d=[['Mon','Clear',14,6],['Tue','Cloudy',13,7],['Wed','Rain',11,8],['Thu','Rain',10,6],
['Fri','Clear',12,4],['Sat','Clear',15,5],['Sun','Cloudy',13,7]],nd=+val('wdays',7);
for(var i=0;i<Math.min(nd,7);i++){var y=74+i*19,s=i==0;if(s)o+=R(6,y-3,306,17,t.fg);
o+=T(10,y,d[i][0],s?t.bg:(i==0?t.ac:t.dim))+T(65,y,d[i][1],s?t.bg:t.dim)
+T(240,y,cv(d[i][2]),s?t.bg:t.fg)+T(280,y,cv(d[i][3]),s?t.bg:t.dim);}return o;}
if(id=='2'){var g=[['Flappy Plane','BEST 27'],['Paddle Catch','BEST 14'],
['Reaction Timer','SOON'],['Simon Says','SOON']],o='',r=0;
for(var i=0;i<4;i++){var h=document.getElementById('h_g'+i);if(h&&h.value=='0')continue;
var y=44+r*36,s=r==0;r++;o+=R(6,y,308,36,s?t.fg:t.bg);if(s)o+=R(6,y,4,36,t.ac);
o+=T(18,y+10,g[i][0],s?t.bg:t.fg,16)+T(306,y+14,g[i][1],s?t.bg:t.dim,8,'r');}
return r?o:T(160,110,'No games enabled',t.dim,8,'c');}
if(id=='3'){var f=+val('ffoc',25),b=+val('fbrk',5);
return T(14,34,'SESSION',t.dim)+R(14,46,292,1,t.rule)
+R(10,56,300,34,t.fg)+R(10,56,4,34,t.ac)+T(26,66,'FOCUS',t.bg,16)+T(250,66,f+' min',t.bg,16,'c')
+T(26,106,'BREAK',t.dim,16)+T(250,106,b+' min',t.fg,16,'c')+T(14,138,'starts now',t.dim);}
if(id=='4'){var u=false;for(var i=0;i<4;i++){var e=document.querySelector('[name=cu'+i+']');
if(e&&e.value.length>7)u=true;}
if(!u)return T(160,104,'No calendar linked',t.fg,16,'c')+T(160,128,'add an iCal link',t.dim,8,'c');
var ev=[['Today','09:30','Standup'],['Today','14:00','Design review'],['Tomorrow','11:00','Dentist'],
['Thu 11','19:30','Flight KL1071'],['Sat 13','08:00','Parkrun'],['Mon 15','all day','Deadline']],o='';
for(var i=0;i<6;i++){var y=26+i*30,s=i==0;o+=R(6,y,308,28,s?t.fg:t.bg);if(s)o+=R(6,y,4,28,t.ac);
o+=T(16,y+3,ev[i][0],s?t.bg:t.ac)+T(16,y+15,ev[i][1],s?t.bg:t.dim)+T(84,y+6,ev[i][2],s?t.bg:t.fg,16);}
return o;}
/* SettingsScreen::drawMainMenu - MENU_Y0 22, MENU_ROW_H 20, a 4px gap
   before the destructive pair, labels at top+4. Six entries since the
   flight interval, home screen, clock and units moved onto this page. */
var op=[['Device info',''],['Theme','Mono'],['Configure on phone',''],
['Software update','v1.0.3'],['Restart device',''],['Factory reset','']],o='';
for(var x=10;x<310;x+=7)o+=R(x,104,3,1,t.rule);
for(var i=0;i<6;i++){var y=22+i*20+(i>=4?4:0),s=i==0;o+=R(6,y,308,20,s?t.fg:t.bg);
if(s)o+=R(6,y,4,20,t.ac);o+=T(18,y+4,op[i][0],s?t.bg:(i>=4?'#b03020':t.fg),16);
if(op[i][1])o+=T(308,y+9,op[i][1]+' >',s?t.bg:t.dim,8,'r');}return o;}
function pv(){var st=document.getElementById('stg');if(!st)return;var t=TH;
var cyc=[];document.querySelectorAll('#sl li[data-id]').forEach(function(li){
if(li.dataset.on=='1')cyc.push({id:li.dataset.id,n:li.querySelector('.sname').textContent});});
cyc.push({id:'255',n:'Settings'});if(pos>=cyc.length)pos=0;var a=cyc[pos];
var lg=document.getElementById('h_legend'),sd=document.getElementById('h_showdate'),
lh=document.getElementById('h_lefth');var pg=val('pager','0'),br=+val('bright',100);
var h=R(0,0,W,H,t.bg)+R(0,HH-1,W,1,t.rule)+T(4,6,'[ '+a.n.toUpperCase()+' ]',t.fg);
if(pg=='0'){var sp=(cyc.length-1)*9+4,ls=Math.round((W+sp)/2)-2;
for(var i=cyc.length-1;i>=0;i--)h+=D(ls-(cyc.length-1-i)*9,9,i==pos,t.fg,t.rule);}
else if(pg=='1')h+=T(W/2,6,(pos+1)+'/'+cyc.length,t.dim,8,'c');
/* Same four formats Units offers: 0/1 are 24h, 2/3 are 12h, and the
   odd ones put the month first. */
var n=new Date(),z=function(v){return(v<10?'0':'')+v;},cf=+val('clock',0);
var hh=n.getHours(),ap='';
if(cf>=2){ap=(hh<12?' AM':' PM');hh=hh%12;if(hh===0)hh=12;}
var ck=z(hh)+':'+z(n.getMinutes())+ap;
if(!sd||sd.value=='1'){var dd=z(n.getDate()),mm=z(n.getMonth()+1);
ck=((cf==1||cf==3)?(mm+'-'+dd):(dd+'-'+mm))+'-'+n.getFullYear()+'  '+ck;}
h+=T(W-4,6,ck,t.dim,8,'r')+body(a.id,t);
if(a.id!='0'&&(!lg||lg.value=='1'))h+=R(0,H-LH,W,LH,t.bg)+R(0,H-LH,W,1,t.rule)
+T(6,216,'^v SELECT',t.dim)+T(6,228,'o ENTER',t.fg);
st.innerHTML=h;st.style.filter='brightness('+(0.45+0.55*br/100).toFixed(3)+')';
/* Derive the scale from .shell, whose width is set by the viewport and a
   max-width - never by its own contents. Measuring anything inside the grid
   fed the last redraw's size back in and the slab grew on every click, until
   it overflowed its column and slid under the controls. The 850 mirrors the
   grid's first track; 212 covers the slab's padding, gap and control panel. */
var vp=document.getElementById('vp'),sh=document.querySelector('.shell');
var shw=(sh&&sh.clientWidth)||360;
var colw=(window.innerWidth>1320)?850:shw;
var sc=Math.max(0.55,Math.min(1.75,Math.min(560,colw-212)/320));
var flip=lh&&lh.value=='1';
/* Deliberately no rotation. Turning the board around is what would put
   the display upside down; setRotation(3) on the device cancels that
   out, so what you read stays upright. Only the controls change side. */
st.style.transform='scale('+sc+')';
vp.style.width=(320*sc)+'px';vp.style.height=(240*sc)+'px';
document.getElementById('bz').classList.toggle('lefthand',!!flip);
document.getElementById('pvn').textContent=cyc.length+' in cycle · press the knob';}
document.addEventListener('change',pv);
window.addEventListener('resize',pv);
tags();pos=startPos();sync();
/* A device that has never had Wi-Fi walks the wizard from step 1;
   one already set up can jump anywhere, since nothing is missing. */
reached=HAVESAVED?2:0;)HTML";

    // Land on Wi-Fi during first-run setup; once the device is on the
    // network there is nothing left to set up, so open on Customise.
    p += _apMode ? "tab(0);" : "tab(2);";
    p += "</script></div></body></html>";

    return p;
}
