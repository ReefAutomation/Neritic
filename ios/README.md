# Neritic iOS app

Native SwiftUI client for a Neritic controller. It talks to the same REST and
WebSocket API the web UI uses — the firmware is unchanged apart from the mDNS
advertisement that lets the app find the device automatically.

```
ios/
├── project.yml              XcodeGen spec — the .xcodeproj is generated, not committed
├── Neritic/                 app target (SwiftUI)
│   ├── NeriticApp.swift     app entry, root/connection views, the two tabs
│   ├── AppModel.swift       connection, cached device data, actions
│   ├── HomeView.swift       connection status, live output, preset grid, LED bar
│   ├── BrightnessGraphView.swift  24-hour schedule graphic
│   ├── PresetsView.swift    preset manager (editing), reached from Settings
│   ├── ScheduleView.swift   timer list and editor
│   ├── SettingsView.swift   strip/relay, safety, transitions, location and time
│   └── MaintenanceViews.swift  firmware update and Wi-Fi setup
└── NeriticKit/              Swift package: everything that talks to the device
    ├── Sources/NeriticKit/
    │   ├── Models.swift         typed models for every endpoint payload
    │   ├── NeriticClient.swift  REST client
    │   ├── LiveStripStream.swift WebSocket: state, live LED frames, OTA status
    │   ├── BrightnessTimeline.swift  schedule maths for the graph
    │   ├── DeviceClock.swift   device time parsing and local ticking
    │   ├── DeviceDiscovery.swift Bonjour browse for _neritic._tcp
    │   ├── DeviceStore.swift    remembers the chosen host
    │   ├── LEDColor.swift       RGBW frame decoding and colour parsing
    │   └── NeriticError.swift   error surface
    └── Tests/NeriticKitTests/   unit tests (no device required)
```

## Getting started

```bash
# run the networking tests — no device, no Xcode project needed
cd ios/NeriticKit && swift test

# generate and open the app project
cd ios && xcodegen generate && open Neritic.xcodeproj
```

Both commands need a full Xcode install (this project was developed against
Xcode 27 / Swift 6.4; the app targets iOS 17 and builds with Xcode 16 or later).

## App icon

The icon is **generated from the existing brand mark**, not hand-drawn:
`web/public/favicon.svg` (the square `neritic-logo.svg`, not the wide wordmark
in `web/public/images/`). Regenerate it with:

```bash
# from the repository root
rsvg-convert -w 900 -h 900 web/public/favicon.svg -o /tmp/mark.png
magick -size 1024x1024 xc:'#F5F7F8' /tmp/mark.png -gravity center -composite \
  -colorspace sRGB -depth 8 -alpha off -strip PNG24:AppIcon-1024.png
```

Write the result to `Neritic/Assets.xcassets/AppIcon.appiconset/AppIcon-1024.png`.

Three constraints are baked into that command, and the icon breaks if any are
dropped:

- **No alpha channel** (`PNG24:`, `-alpha off`). App Store submission rejects an
  app icon with transparency, and the source SVG is transparent. `-alpha off`
  alone is not enough — the PNG writer will still emit an alpha channel unless
  the `PNG24:` prefix forces RGB.
- **8-bit sRGB** (`-depth 8 -colorspace sRGB`). ImageMagick defaults to 16-bit,
  which the asset catalog does not want.
- **A light background** (`#F5F7F8`). The logo uses negative space for the sky;
  against the navy the blue water elements lose contrast. The light background
  keeps all three brand colours (#C86433 sun, #2CA99E water, #285582 fish)
  legible down to the smallest Home Screen sizes.

The mark is inset at 900 of 1024 px. Nothing important is clipped by the iOS
squircle mask: the waterline bar runs edge to edge but sits at mid-height, where
the mask edge is straight.

The icon is referenced through `CFBundleIconName` in `project.yml`. That key is
**required** here because the Info.plist is declared in `project.yml` rather than
generated automatically — without it the catalog icon compiles but is never
referenced and the app ships blank.

## How the app finds a controller

1. **Bonjour.** The firmware advertises `_neritic._tcp` on the LAN, so the app
   lists controllers it finds. The first connection on a network triggers the
   iOS _local network_ permission prompt — iOS refuses the connection if it is
   denied.
2. **Manual entry.** Typing an address (`192.168.1.42` or `neritic.local`)
   always works, including when mDNS is blocked by the router or the firmware is
   older than the mDNS change.

The chosen host is remembered and reused on launch.

## Things worth knowing before changing this code

- **Presets are the unit the user picks; effects are not.** An effect is a
  reusable building block that a preset is made of. The Lights tab therefore
  leads with a preset grid (mirroring the web UI's `PresetsCard`), and offers
  effects only as a compact picker under "Current look", which adjusts what is
  running now without editing the preset. Do not reintroduce a list of effects
  as a top-level chooser.
- **The lights tab and Settings are the only two tabs**, matching the web UI
  (Home / Config). Applying presets happens on Lights; editing them happens in
  Settings → Presets.
- **Discovery connects by hostname, not by address.** `<name>.local` is preferred
  over the resolved IP: a DHCP lease change silently invalidates a stored
  address, while the Bonjour name follows the device. This depends on the
  firmware advertising its hostname as the service name — `startMdns` in
  `src/network.cpp` sets `mdns_hostname_set` and `mdns_instance_name_set` to the
  same value. `HostAddress.bonjourName` checks the instance name is actually
  usable as a DNS label (Bonjour instance names are free-form UTF-8 and may
  contain spaces) and falls back to the resolved address when it is not. The
  resolved address is still shown in the discovery row for reference.
- **The device clock is a snapshot, not a stream.** The firmware calls
  `broadcastState()` only from `src/state.cpp` when something changes — never on
  a timer — so the `time` it sends freezes between events. Clients must tick
  forward locally from the last reading, which is what `DeviceClockAnchor` does
  (and what `web/LiveClock.jsx` does in the browser). Never render `state.time`
  directly; the clock will appear stuck.
- **The schedule graph is a port of `web/Graph.jsx` and the two must agree.**
  `BrightnessTimelineBuilder` in NeriticKit reproduces `getRampStepPoints`,
  `calcRampEnd`, `calcStartBrightness`, `buildTimerChartData` and `getBgColors`
  — including the midnight wrap-around and the "keep the last value for a
  minute" de-duplication. If you change the curve, change both, and update
  `BrightnessTimelineTests` (its expectations were derived from the JS).
  The curve is drawn with **linear** interpolation, matching the web chart's
  `tension: 0` and absence of `stepped`. Step interpolation would flatten each
  scheduled ramp — a diagonal — into a horizontal run plus a vertical jump.
  One deliberate divergence: when the device has not synchronised its clock the
  web UI substitutes the browser clock, while the app shows `--:--` and draws no
  "now" marker. The schedule runs on the device's clock, so a phone-clock value
  would point at a time the device does not agree with.
- **Restart is not a normal request.** `/api/command` replies `{"success":true}`
  and the device calls `esp_restart()` about 200 ms later, so the socket is often
  torn down before the response is read. `NeriticClient` reports that as
  `NeriticError.connectionLost` rather than `unreachable`, and
  `AppModel.performRestartCommand` treats it as success — while a controller that
  was never reachable is still a failure. Factory reset behaves the same way.
  Because a restart leaves no other visible trace, the Device actions screen
  shows an explicit outcome plus the live connection status, so you can watch the
  controller drop and come back.
- **Percentages only.** The device accepts and reports brightness, speed and
  intensity as `0-100`. It converts to its internal `0-255` range itself
  (`percentToHex` / `hexToPercent` in `src/config.h`). Do not convert in the app.
- **`client_pause` does not stop the live stream.** The firmware checks
  `_suspendedClients` in `broadcastText` but _not_ in `broadcastBinary`, so the
  400 ms LED frames keep arriving while paused. `LiveStripStream.suspend()`
  closes the socket instead; the app calls it when it goes to the background.
- **`claim_live` / `release_live` / `ota_ack` are inert** in the current
  firmware (`_activeLiveFd` is never read when broadcasting, `otaAckReceived` is
  never read at all). The app deliberately does not send them.
- **OTA is remote-trigger only.** The app calls `POST /api/update` and the
  device downloads the correct image for its own build environment. Progress
  arrives as `ota_status` frames, which the device only sends to clients that
  subscribed with `{"type":"ota_client"}`. There is no local `.bin` upload.
- **Preset speed is inconsistent in the firmware.** `GET /api/presets` returns
  `params.speed` in the internal `0-255` range, while `POST /api/preset`
  interprets it as a percentage. A read-modify-write therefore does not preserve
  speed. This is _not_ compensated for in the app — see `PresetParams.speed` in
  `Models.swift`. The proper fix is in the firmware.
- **`POST /api/timer` writes one timer per call** and needs an id, so the
  schedule editor writes the whole `timers` array through `POST /api/config`,
  which also makes adding and removing entries uniform.
- **Power and brightness must travel together** when both change: the firmware
  applies power first on purpose so a power-off does not briefly relight the
  strip.

## Local network and transport

The device serves plain HTTP with no authentication and permissive CORS, so the
app:

- requests `NSAllowsLocalNetworking` in App Transport Security rather than
  disabling ATS wholesale (which App Review scrutinises),
- declares `NSBonjourServices` for `_neritic._tcp`,
- provides `NSLocalNetworkUsageDescription`.

Anything on the same LAN can reach the device. Adding authentication would need
coordinated firmware, web and app changes.

## Not implemented yet

- Local firmware upload (`POST /ota` with a chosen `.bin`).
- A jump to the device's own web UI.
- Widgets, Shortcuts actions, and an Apple Watch companion.
