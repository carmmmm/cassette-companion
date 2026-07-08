# Cassette Companion — Final Build Guide

A hand-built cassette-shell device with an OLED display that shows live Spotify "now playing" info (with dancing stick figures!), idle weather, and overhead flight tracking — all running standalone off a rechargeable battery.

This document reflects the **actual final build**, including the real wiring, the real code, and the real lessons learned along the way.

---

## Final Parts List (as built)

### Electronics
- ESP32 DevKit V1 (ESP32-WROOM-32)
- 1.3" OLED 128×64 I2C, **SH1106 driver**, pin order on this unit: **GND, VCC, SCK (=SCL), SDA**
- TP4056 LiPo charger/protection board (HW-373), USB-C
- LiPo battery, 3.7V pouch cell
- MT3608 step-up (boost) converter — **note: first unit had a broken/stripped trimpot and had to be swapped for the spare**
- Mini slide switch (3-pin/SPDT) — **wired using the middle pin as common; the two outer-pin wiring alone does not work**
- No WS2812B LEDs in this build (omitted — device is OLED + dancing figures only, no reel lighting)

### Build materials
- Copper-clad board as mounting base (not standard perfboard — solid copper surface, so insulation under every joint matters more than usual)
- Hookup wire, red/black only
- Solder, heat shrink/tape for joint insulation

### Software / accounts
- Arduino IDE with ESP32 board package (`esp32` by Espressif Systems, via Boards Manager URL: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`)
- Board selection: **ESP32 Dev Module**
- Libraries: `ArduinoJson`, `Adafruit GFX Library`, `Adafruit SH110X`
- Personal Spotify Developer app (Client ID + Secret + refresh token — **must be your own account's**, not reused from anyone else's app, since a refresh token is tied to the authorizing account)
- Two files: `cassette_companion.ino` (main code) + `secrets.h` (Wi-Fi + Spotify credentials, kept separate from the main file)

---

## Wiring — Final, Verified Working

**Power chain:**
```
Battery (+/-) → TP4056 (B+/B-)
TP4056 OUT+ → Switch MIDDLE (common) pin
Switch OUTER pin → MT3608 VIN+
TP4056 OUT- → MT3608 VIN- (direct, no switch)
MT3608 VOUT+ → ESP32 VIN (5V)
MT3608 VOUT- → ESP32 GND
```

⚠️ **Switch wiring gotcha:** if using a 3-pin slide switch, the **middle pin is the common/hub** — current has no path if you only wire the two outer pins. This cost significant debugging time; wire OUT+ to the middle pin first.

⚠️ **MT3608 trimpot:** set to exactly **5.00V** using a multimeter *before* connecting the ESP32, with USB power feeding the TP4056 (not the battery) during calibration. If turning the trimpot in either direction produces zero voltage change, the pot is likely physically stripped/broken — swap to a spare board rather than continuing to troubleshoot a dead unit.

**OLED (I2C) — note the non-default SCL pin:**
```
OLED GND → ESP32 GND       (black)
OLED VCC → ESP32 3.3V      (red)
OLED SCK → ESP32 GPIO 19   (red)
OLED SDA → ESP32 GPIO 21   (black)
```
GPIO 19 is **not** the ESP32's default SCL pin (that's GPIO 22), so the code must explicitly call `Wire.begin(SDA_PIN, SCL_PIN)` with `SDA=21, SCL=19` — a plain `Wire.begin()` with no arguments will fail to find the display.

**Physical safety notes (copper-clad base):**
- Insulate every solder joint individually (heat shrink or tape) since the mounting board is solid copper, not isolated-pad perfboard.
- Anchor wires so they can't shift during assembly — this caused a short during an earlier assembly attempt.
- Dry-fit everything in the cassette shell and run it for several minutes *before* permanently gluing/closing, checking for flicker or reset that would indicate an intermittent short.

---

## Software Architecture (final)

The final code runs two things in parallel using the ESP32's dual cores, which was the fix for visualizer stuttering:

- **Core 0** — a background task (`networkTask`) that handles *all* networking: Spotify polling, weather fetch, plane detection. Runs on its own loop, updates shared variables when new data arrives.
- **Core 1** — the main `loop()`, which only reads those shared variables and draws to the OLED. It never makes a network call itself, so the on-screen animation never freezes waiting on Wi-Fi/Spotify.

### Three screen states
1. **Now Playing** — shown whenever Spotify reports `is_playing = true`. Track name and artist scroll marquee-style across the top if too long to fit. Below that, one of **5 dancing stick-figure animations** plays, chosen by hashing the Spotify track ID (so the same song always gets the same dance, but different songs vary):
   - Waltz (couple spinning together)
   - Club (3 figures jumping, offset rhythm)
   - Solo Freeform (one figure, loose movement)
   - Waltz Twirl (lead with raised arm, partner orbiting)
   - Group Jump (trio jumping in tight sync)
2. **Weather (Boone, NC)** — idle default screen when nothing's playing. Shows temperature and conditions via the free Open-Meteo API (no key required), refreshed every 5 minutes, with a twirling ballerina animation alongside it. Failed fetches retry in ~10 seconds rather than waiting the full 5-minute interval.
3. **Plane Overhead** — when OpenSky Network detects an aircraft within ~15 miles, this screen alternates with the Weather screen every 15 seconds (so you still catch weather updates while a plane's overhead). Shows callsign and altitude always; shows origin/destination when adsbdb.com can resolve the flight's public schedule (commercial flights only — private/small aircraft will show "destination unknown," which is expected, not a bug).

### Known limitation, by design
Spotify's `/audio-features` endpoint (which would have given real tempo/energy data) is blocked for apps created after late 2024, so the dance animations are **not** driven by actual song tempo — they're a fixed, pleasant pace with per-track variety via the hash trick above, rather than truly reactive to the music.

---

## Files

- `cassette_companion.ino` — main program (see project folder)
- `secrets.h` — Wi-Fi credentials + Spotify Client ID/Secret/refresh token (kept out of the main file; never share this file's contents)

---

## Day-to-Day Use

- **Power on/off:** the slide switch. Off saves battery when not in use.
- **Charging:** plug USB-C into the **TP4056's** port (not the ESP32's). LED solid = charging, changes color when full (roughly 1–2 hours for a full charge on a small pouch cell). Charging works regardless of switch position.
- **Battery life while running:** expect several hours of continuous runtime, not days — the device polls Wi-Fi/Spotify continuously rather than sleeping.
- **No re-flashing needed between charges** — code persists in flash memory permanently; only Wi-Fi/Spotify session state resets on power-up, which happens automatically within a few seconds.

---

## If You Rebuild This Later

The single most time-consuming issues during this build, in order of how much debugging they cost:
1. A physically dead/stripped trimpot on the first MT3608 board (looked fine, never actually adjusted voltage) — swap to spare rather than over-troubleshooting.
2. Wiring only the two outer pins of a 3-pin switch instead of using the middle common pin.
3. The visualizer freezing every 8 seconds during Spotify polling — solved by moving all networking to the ESP32's second core via `xTaskCreatePinnedToCore`, rather than trying to shrink timeouts further.
