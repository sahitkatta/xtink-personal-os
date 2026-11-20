# XTE Terminal Dashboard — Design

A personal productivity device built on an XTEINK X4 e-reader, reflashed with
custom Arduino firmware. No backend: the device talks directly to the
internet for calendar data, and holds its own settings/todos locally so nothing
requires a reflash to change.

## Hardware (XTEINK X4)

Confirmed against the device itself (boot logs) and cross-checked against
three independent open-source projects targeting this board
(`xteink-terminal`, `xteink-x4`, `xteink-4-in-a-row`):

| | |
|---|---|
| MCU | ESP32-C3 (single core, 160MHz), Wi-Fi 2.4GHz + BLE |
| Flash | 16MB, wired **DIO only** (see "Hardware bring-up log" below) |
| RAM | 320KB |
| Display | 4.26", 800x480, SSD1677 controller, GDEQ0426T82 panel, monochrome, GxEPD2 class `GxEPD2_426_GDEQ0426T82` |
| Display pins | SCLK=8, MOSI=10, CS=21, DC=4, RST=5, BUSY=6 |
| Shared SPI bus | SCLK=8, MOSI=10, **MISO=7** (display is write-only and ignores it, but the microSD card needs it — see bring-up log) |
| microSD | CS=12, on the shared SPI bus, 40MHz |
| USB | USB-C, native USB-CDC (auto-resets into bootloader, no button combo needed to flash) |

### Buttons (confirmed on real hardware)

Two ADC resistor ladders + one digital button. `BTN_THRESHOLD = 100` is the
shared tolerance window on every ladder value below.

| Button | Pin | Type | Raw ADC value (idle = 4095) |
|---|---|---|---|
| RIGHT | GPIO1 ladder | analog | 3 |
| LEFT | GPIO1 ladder | analog | 1470 |
| CONFIRM | GPIO1 ladder | analog | 2655 |
| BACK | GPIO1 ladder | analog | **3590** (reference project assumed 3470 — measured different on this unit, see bring-up log) |
| VOLUME_DOWN | GPIO2 ladder | analog | 3 |
| VOLUME_UP | GPIO2 ladder | analog | 2205 |
| POWER | GPIO3 | digital, `INPUT_PULLUP`, active LOW | n/a |

Decoding logic: `readButtonName()` in `src/main.cpp`. Calibration data was
captured live via an on-device logger (`logButtonState()` → `/button_log.csv`
on the SD card, dumped over Serial on the next reconnect) — not guessed or
hand-timed over a serial connection, which turned out to be unreliable (see
bring-up log).

### Battery

GPIO0 (`BAT_ADC_PIN`), through a voltage divider (`dividerMultiplier = 2.0`
in `BatteryMonitor`), polynomial-fit percentage curve adapted from
Idered/xteink-x4 (MIT licensed). USB/charging detection: GPIO20
(`USB_SENSE_PIN`) reads HIGH when USB is connected. **Readings are only
trustworthy with USB disconnected** — plugged into a computer, the ADC picks
up real noise (GPIO0 is a boot-strapping pin), confirmed by comparing
readings plugged vs. unplugged on this exact unit.

### Hardware bring-up log

Four real bugs hit and fixed while getting from "flashes" to "every input
works," in the order found — kept here because the next person flashing a
new unit (or a new batch with component variance) will hit at least the last
one again.

**1. Boot loop: `partition 0 invalid magic number 0xffff`.**
PlatformIO's `esp32-c3-devkitm-1` board defaults to `flash_mode: qio`, and
relabeling a QIO-compiled bootloader's header as DIO does **not** change the
compiled SPI driver logic inside it — it still issues quad-IO commands on
hardware wired for only two data lines, which corrupts every flash read past
the first stage (the chip boots, but can't read its own partition table).
Fixed by forcing `board_build.flash_mode = dio` in `platformio.ini` so the
build selects the real `bootloader_dio_80m.elf`. Also needed both
`board_build.flash_size` (linker/partition table) **and**
`board_upload.flash_size` (the actual `--flash-size` esptool writes) set to
`16MB` — only one of the two is not enough.

**2. SD card never detected.** Assumed the display's MISO line was unused
(`-1`) since the panel is write-only, which is true for the *display* but
not for the *bus* — the microSD card needs a real MISO to read anything
back. Worse, the first guess at that pin (9, from an AI-summarized README)
was also wrong. Ground truth came from reading actual pin-definition source
in a working reference project (Idered/xteink-x4), not another summarized
README: **MISO=7**. Lesson: for pin mappings, read the `.ino`/`.h` constants
directly — a summarized doc can blend details from unrelated repos.

**3. Crash on boot: `ADC: CONFLICT! driver_ng is not allowed to be used with
the legacy driver`.** The ported `BatteryMonitor` used `esp_adc_cal.h` (the
legacy ESP-IDF ADC calibration API) to convert raw readings to millivolts,
which conflicts with `analogRead()` elsewhere in the firmware using the
newer IDF 5.x ADC driver on this Arduino core (3.x). Fixed by switching to
`analogReadMilliVolts()`, which calibrates internally through the new driver
— no legacy include needed, and it's a strict simplification over the
original two-step raw-then-calibrate approach.

**4. BACK button never decoded** (fell through to `NONE`). The reference
project's threshold (3470) doesn't match this unit — real value measured at
3580-3592. Everything else (RIGHT/LEFT/CONFIRM/VOLUME_UP/VOLUME_DOWN/POWER)
matched the reference exactly; only BACK was off, most likely component
tolerance variance on the resistor ladder. This is why the on-device SD
logger (see "Buttons" above) exists — relaying raw values through a
synchronized serial capture while someone presses buttons in person turned
out to be unreliable (timing drift, e-ink partial-refresh latency making it
look "off by one"); logging every distinct state to `/button_log.csv` and
reading the whole session back after the fact removed the guesswork.

## Principles

- **No backend.** The device is the only compute. All data comes from direct
  HTTPS calls to public internet services (Google's ICS export), not a
  server we run.
- **No reflash to change settings.** WiFi credentials, the calendar URL,
  refresh cadence, and the todo list all live on the microSD card and are
  editable from an on-device settings web server.
- **Low power by default.** The e-ink panel holds its image with no power.
  The device deep-sleeps between wake cycles; only explicit actions (button
  long-press) keep it awake and radios on.

## Data sources

| Feature | Source | Notes |
|---|---|---|
| Calendar | Google Calendar's private "Secret address in iCal format" URL (Calendar Settings → your calendar → "Integrate calendar") | Read-only. Device fetches the raw ICS over HTTPS and parses it itself — see RRULE scope below. |
| Todos | Local only | Managed entirely on-device via the settings web server; stored as `todos.json` on LittleFS. Not synced anywhere — this is the tradeoff of skipping OAuth/Google Tasks. |
| Time | NTP (`pool.ntp.org`) | UTC offset is a plain settings field (minutes), not an IANA timezone — no DST auto-conversion. Known simplification, matches a gap in the reference project too. |

### Why ICS over OAuth

Considered Google Calendar API + OAuth device-code flow (lets a TV/limited-input
device get a user-consented token with no browser on-device) because it would
also unlock Google Tasks as a real bidirectional todo source. Decided against
it for v1: needs a Google Cloud project + OAuth client set up by hand, and
you chose the simpler no-setup path. **Revisit this if the local-only todo
list stops being useful enough** — the device's web server and HTTPS stack
already support it; it would layer in cleanly later.

### RRULE scope (what recurrence is actually supported)

Full RFC 5545 recurrence is a lot of surface area for a 320KB-RAM MCU. Scope,
matching how real personal calendars actually use recurrence:

- `FREQ=DAILY`, `WEEKLY` (with simple `BYDAY` lists, no ordinals), `MONTHLY`
  (by day-of-month), `YEARLY` (by month+day — covers birthdays) — all with
  `INTERVAL`.
- Bounded by `COUNT` or `UNTIL`.
- `EXDATE` exclusions.
- Expansion window: ±48h around "now" (same window the reference backend
  used) — enough to catch events spanning midnight; this is a daily-glance
  device, not a long-range planner.

**Not supported** (silently skipped rather than crashing): `BYSETPOS`,
ordinal `BYDAY` ("2nd Tuesday"), `RDATE` additions, combined multi-rule
recurrence. If a real event hits one of these, it just won't expand — noted
here instead of discovered as a mystery bug later.

## Storage — microSD card, default-state fallback

The microSD card (already on the shared SPI bus, CS=12) is the **only**
persistence layer — no NVS, no LittleFS partition. Two files at the card's
root:

- `/config.json` — WiFi SSID/password, ICS URL, refresh interval, notify
  lead minutes, UTC offset.
- `/todos.json` — the local todo list (flat array, optional `parentId` for
  one level of sub-tasks, matching the reference project's shape).

**If the card is missing or fails to mount, the device does not error out —
it just runs on hard-coded defaults** (no WiFi saved → goes straight to
`SETUP_AP`; empty todo list; default refresh/notify timings). Settings
changes made through the web server in that state are kept in RAM for the
current session only and are lost on the next boot/sleep cycle — there's no
secondary fallback store, by design, since the SD card is meant to always be
present in normal use. `checkSdCard()` in `main.cpp` is the first-line
verification of this (mount + write + read-back), confirmed to run before
anything that depends on the card.

Picked over NVS+LittleFS because the device already has a full-size SD slot
sitting unused on the same bus, and JSON files are easier to inspect/edit by
hand (pull the card, open on a laptop) than NVS blobs.

## Device "OS" — state machine

```
BOOT
 ├─ mount SD card; missing/failed → defaults for everything below
 ├─ load settings (/config.json, /todos.json from SD)
 ├─ wake cause: timer | button | power-on
 │
 ├─ long-press detected on the designated button ───────► SETTINGS_MODE
 │                                                           - stays awake, keeps WiFi up
 │                                                           - starts HTTP server on LAN IP
 │                                                           - screen shows IP + QR code
 │                                                           - exits (reboot) on idle timeout
 │                                                             or explicit "done" from the UI
 │
 ├─ no WiFi credentials saved ───────────────────────────► SETUP_AP
 │                                                           - SoftAP "XTE-SETUP" + captive portal
 │                                                           - scan+pick SSID, enter password,
 │                                                             paste ICS URL
 │                                                           - save to /config.json on SD (if
 │                                                             present; else RAM for this boot
 │                                                             only), reboot into NORMAL
 │
 └─ NORMAL_CYCLE
     - connect WiFi (saved creds)
     - fetch + parse ICS → today's events (current/upcoming), classify specials
     - load todos.json
     - compute notification state (event starting within lead time / overdue todo)
     - render portrait dashboard
     - full refresh if: cold boot, forced (button), or anti-ghost cadence hit;
       else partial refresh
     - compute next wake = min(poll interval, next event boundary, notify lead
       boundary), clamped 5–15 min
     - deep sleep (timer + button wake sources armed)
```

## Display layout (portrait, 480×800 after rotation)

```
┌─────────────────────────────┐
│ XTE--          Tue Nov 19   │  header: device id · weekday+date
│ 09:41                  72%  │  clock · battery
├─────────────────────────────┤
│ ⚠ Piano starts in 5 min     │  alert/special strip — becomes an
│                              │  urgent banner when a notification
│                              │  condition is active, otherwise shows
│                              │  today's special items (birthdays,
│                              │  holidays, all-day events)
├─────────────────────────────┤
│ SCHEDULE                    │
│  09:00–10:00     Standup    │
│  NOW             Deep Work  │  current event inverted (black/white)
│  13:00           Piano  [!] │
├─────────────────────────────┤
│ TASKS                       │
│  [ ] Buy groceries        ! │  ! = high priority
│  [x] Pay rent                │
│    [ ] sub-task (indented)  │
├─────────────────────────────┤
│ WIFI:UP  ICS:OK  09:41  72% │  status bar
└─────────────────────────────┘
```

Single column (not the original two-column landscape layout) because 480px
of width doesn't comfortably fit two side-by-side sections — portrait suits
a vertical flow better anyway, closer to a paper notepad.

## Settings web server (long-press activated)

Pages: WiFi (scan/change network), Calendar (ICS URL, refresh interval,
notify lead minutes, UTC offset), Todos (add/edit/delete/check, drag-free
flat list with optional one-level indent for subtasks), Status (last sync,
battery, free flash space), and a "force full refresh" button for manual
ghost-clearing. No login — same trust model as being on your home WiFi.

## Notifications

On-device visual banner only (no speaker/haptics on this hardware). The
device already wakes near event boundaries for the schedule highlight to
update on time; the same mechanism triggers the alert-strip banner for
"starts in N minutes" and overdue todos. No push-to-phone in v1 — straightforward
to add later via a plain HTTPS POST to ntfy.sh if the visual banner isn't
enough, since that needs no backend either.

## On-device UI toolkit: custom lightweight, not LVGL

Considered LVGL (the standard answer for "ESP32 GUI library") and decided
against it. LVGL is built around frequently redrawing a framebuffer
(touch/animation-oriented); that fights the e-ink partial-refresh model
(you'd be constantly working against it to avoid flashing the panel) and
doesn't fit the plain monospace/terminal look we want anyway. It also adds
real RAM/flash overhead for widgets (sliders, animations, touch handling)
this device will never use.

Going with a small custom layer directly on GxEPD2 instead: a "screen"
abstraction (one draw function per screen — dashboard, settings, file
manager), plus a minimal selectable-list/cursor helper driven by the
RIGHT/LEFT/CONFIRM/BACK d-pad. GxEPD2 already provides every drawing
primitive needed (`drawPixel`, `drawLine`, `fillRect`, `drawBitmap` for
icons/QR codes, custom fonts) — this is a thin navigation/state layer on top
of that, not graphics-from-scratch. Expected to be a few hundred lines
total.

## On-device apps

Beyond the dashboard (calendar/todos/status bar), the device gets two more
screens, navigable with the d-pad and reachable via BACK from the dashboard:

- **File manager** — browse the microSD card's contents directly on-device
  (list files/dirs, navigate in/out, matches the Linux-terminal aesthetic:
  monospace `ls`-style listing, a prompt-like current-path line).
- **Settings** — on-device counterpart to the long-press web portal, for
  when you don't have a phone/laptop handy: WiFi, calendar URL, refresh
  cadence, todos.

The whole on-device UI (not just these two) follows the Linux-terminal look:
monospace fonts, box-drawing borders, inverted blocks for selection/current
item, a bottom status/prompt line — consistent with the dashboard's existing
SCHEDULE/TASKS styling rather than icon-and-button chrome.

## Known open items

- No DST-aware timezone handling — UTC offset is a static settings field.
- Todos are local-only (see "Why ICS over OAuth" above for the tradeoff and
  the upgrade path if this becomes limiting).
- No card present → settings edits don't survive a reboot (by design, see
  "Storage" above) — worth a clear on-screen/web warning once the settings
  server exists, so this isn't a silent surprise.
- Battery/button values above were calibrated on one specific unit — a
  second unit may need the same live-recalibration pass (see bring-up log
  item 4) rather than assuming the same constants hold.
