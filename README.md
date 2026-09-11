# FlugVel

A homemade desk gadget: one knob, one button, a small TFT screen. Shows the
nearest overhead flight, weather, a merged calendar, notes pulled from a
Notion page, a focus timer, and two working games — turn the knob to move,
click to select. No app, no account: it runs its own settings page over
Wi-Fi.

The full pitch and the manual live at [flugvel.com](https://flugvel.com).

## Layout

- `src/` — firmware (C++, Arduino framework via PlatformIO)
  - `main.cpp` — entry point, GPIO setup, main loop
  - `config/` — persistent settings (`Config`, stored in NVS), the
    calendar feed list, the Notion notes source
  - `screens/` — one file per screen (dashboard, plane tracker, calendar,
    notes, focus timer, games, settings, Wi-Fi setup, OTA update prompt),
    plus `ScreenManager`/`ScreenRegistry` for cycling between them
  - `ui/`, `network/`, `api/`, `external/` — shared drawing helpers,
    Wi-Fi/HTTP plumbing, third-party API clients, vendored external code
- `include/`, `lib/`, `test/` — standard PlatformIO folders; `lib/` and
  `test/` are currently just the framework's own scaffolding, unused
- `enclosure/` — `flugvel_case.scad`, the 3D-printed case (OpenSCAD)
- `web/` — the companion site (flugvel.com): landing page, manual, a
  device dashboard, and the Cloudflare Pages Functions + D1 backend behind
  both the dashboard and the device's own OTA update checks. See
  [`web/DEPLOY.md`](web/DEPLOY.md) to deploy your own copy.

## Building the firmware

This is a [PlatformIO](https://platformio.org) project targeting a plain
`esp32dev` board, an ST7789 240×320 TFT, a rotary encoder, and two
buttons — see `platformio.ini` for the exact pin map and build flags.

```bash
pio run              # build
pio run -t upload    # flash over USB
pio device monitor   # serial output, 115200 baud
```

First boot (or after a factory reset) puts the device into its own Wi-Fi
captive portal — connect to it from a phone or laptop and enter your
network credentials, same as any smart-home gadget's setup flow.

## OTA updates

The device can update its own firmware over Wi-Fi once a new release is
published to `web/functions/api/releases.js` — see `update_manifest.json`
and `src/screens/UpdatePromptScreen.cpp`.
