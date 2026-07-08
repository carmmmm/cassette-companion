# Cassette Companion 

A hand-built cassette-shell device with an OLED display that shows live Spotify "now playing" info (with dancing stick figures!), idle weather, and overhead flight tracking — all running standalone off a rechargeable battery.


## Parts List

### Electronics
- ESP32 DevKit V1 (ESP32-WROOM-32)
- 1.3" OLED 128×64 I2C, **SH1106 driver**, pin order on this unit: **GND, VCC, SCK (=SCL), SDA** — check your own unit's pin order before wiring, it varies by manufacturer
- TP4056 LiPo charger/protection board (HW-373), USB-C
- LiPo battery, 3.7V pouch cell
- MT3608 step-up (boost) converter — buy at least 2; see the trimpot note below
- Mini slide switch, 3-pin/SPDT

### Build materials
- Hookup wire, red/black
- Solder
- Heat shrink tubing or electrical tape (for insulating every joint)
- Double-sided foam tape
- A rigid mounting surface — an acrylic sheet works well, cut to fit inside the cassette case

### Tools
- Soldering iron + solder
- **Multimeter — not optional.** You need it to set the boost converter voltage and to troubleshoot wiring, and you will almost certainly need to troubleshoot wiring.
- Small flathead screwdriver (for the boost converter's trimpot)
- Wire strippers
- Flux (recommended, not required — helps solder flow cleanly onto joints instead of balling up)

### Software / accounts
- Arduino IDE with ESP32 board package added via Boards Manager URL:
  `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
- Board selection: **ESP32 Dev Module**
- Libraries (Sketch → Include Library → Manage Libraries): `ArduinoJson`, `Adafruit GFX Library`, `Adafruit SH110X`
- A personal Spotify Developer app — **must be your own account's app and your own refresh token.** A refresh token is tied to the Spotify account that authorized it; you cannot reuse someone else's, even if you have their Client ID and Secret.
- Two files in your sketch folder: `cassette_companion.ino` (main code) and `secrets.h` (Wi-Fi + Spotify credentials — never share this file or commit it to a public repo)

---

## Soldering Tips (learned the hard way)

- **Tin your wire tips before soldering them to a pad.** Melt a small bit of solder onto the stripped wire end first — this gives a much cleaner, more reliable joint than trying to melt solder directly on the pad while holding a bare wire in place.
- **Use flux if a joint isn't taking solder cleanly.** If solder is balling up instead of flowing onto the pad, that's usually oxidation — flux fixes it.
- **Tug-test every joint** once it's cooled, gently, to confirm it's mechanically solid and not just resting on the pad.
- **Insulate every single joint individually** with heat shrink or tape once soldered — especially important if your mounting surface has any conductive material anywhere nearby.
- **Leave a blob of solder on your iron tip when you put it down.** A bare hot tip oxidizes and gets harder to use over time; wipe it clean right before your next session instead.
- **Keep wires as short as reasonably possible** given your layout — less slack means less chance of something shifting and shorting later.

---

## Wiring — Step by Step, With Testing Checkpoints

Don't wire this all at once and hope it works — build it in stages and verify each one with a multimeter before moving to the next. This is the order that actually worked.

### Step 1: Set the boost converter voltage FIRST, before anything else is connected
1. Solder leads from a USB power source (5V) to the MT3608's **VIN+/VIN-**.
2. Multimeter on DC volts, probe **VOUT+/VOUT-**.
3. Turn the trimpot slowly with a small screwdriver until you read exactly **5.00V**.
4. Remove the temporary USB leads once set. Don't touch the trimpot again after this.

### Step 2: Battery → TP4056
```
Battery (+) → TP4056 B+
Battery (–) → TP4056 B-
```
Verify: probe TP4056 **OUT+/OUT-** with a multimeter. You should read roughly 3.7–4.2V (LiPo resting voltage).

### Step 3: TP4056 → Switch → Boost converter
```
TP4056 OUT+ → Switch MIDDLE (common) pin
Switch OUTER pin → MT3608 VIN+
TP4056 OUT- → MT3608 VIN- (direct, no switch on this line)
```
**Switch wiring gotcha:** on a 3-pin slide switch, the **middle pin is the common/hub**. Wiring only the two outer pins gives you no path for current no matter how many times you flip it.

Verify: with the switch ON, probe MT3608 **VIN+/VIN-**. You should read the same ~3.7–4.2V from Step 2. If you get 0V here, the switch or its joints are the problem — check continuity mode on your multimeter before assuming a whole board is dead.

### Step 4: Boost converter → ESP32
```
MT3608 VOUT+ → ESP32 VIN (5V pin)
MT3608 VOUT- → ESP32 GND
```
Verify: flip the switch on, confirm the ESP32's onboard LED lights up.

### Step 5: OLED → ESP32 (I2C)
Check your specific OLED's pin labels before wiring — don't assume the order matches another unit.
```
OLED GND → ESP32 GND       (black)
OLED VCC → ESP32 3.3V      (red)
OLED SCK → ESP32 GPIO 19   (red)
OLED SDA → ESP32 GPIO 21   (black)
```
GPIO 19 is **not** the ESP32's default SCL pin (that's GPIO 22). The code must explicitly call `Wire.begin(SDA_PIN, SCL_PIN)` with `SDA=21, SCL=19` — a plain `Wire.begin()` with no arguments will fail to find the display even though it's wired correctly. 

### Step 6: Full power-on test before anything gets glued down
1. Flip switch on — ESP32 LED lights.
2. Plug USB-C into the **TP4056** — its charge LED should light.
3. Unplug USB — device should keep running on battery alone.
4. Flash a basic "Hello" test sketch to confirm the OLED responds over I2C before moving on to the full program.

### Step 7: Dry-fit before permanent assembly
Loosely place everything in the cassette case, close it just enough to check clearance, and run it for several minutes. Watch for any flicker or reset — that indicates an intermittent short from something touching that shouldn't be. Only glue/tape things down permanently once this test is clean.

---

## If Something Doesn't Work: How to Actually Debug It

1. **Multimeter on DC volts.** Start at the battery and work forward one connection at a time: battery → TP4056 OUT → switch → boost VIN → boost VOUT → ESP32. The moment a reading drops to 0V that shouldn't be 0V, that's your problem — it's between that point and the last point that read correctly.
2. **If a connection reads 0V but "looks" fine,** switch your multimeter to continuity mode and check the wire itself, not just the endpoints. A wire can look soldered and still have a cold joint that only beeps under the tiny test current continuity mode uses, without actually carrying real current.
3. **If continuity checks out but voltage still won't come through,** the issue is often a component that's dead or an incorrect pin — this is what happened with both the switch (wrong pins used) and the trimpot (physically broken) on this build.
4. **Don't skip straight to reflashing code if the problem is electrical.** If the ESP32 isn't getting stable power, no amount of code changes will fix a flickering screen or random resets.

---

## Software Architecture

The final code runs two things in parallel using the ESP32's dual cores — this was the fix for the on-screen animation stuttering every time the device checked Spotify.

- **Core 0** — a background task (`networkTask`) that handles *all* networking: Spotify polling, weather fetch, plane detection. Runs continuously, updates shared variables whenever new data arrives.
- **Core 1** — the main `loop()`, which only reads those shared variables and draws to the OLED. It never makes a network call itself, so the animation never freezes waiting on Wi-Fi or Spotify.

### Three screen states
1. **Now Playing** — shown whenever Spotify reports `is_playing = true`. Track name and artist scroll marquee-style across the top if too long to fit. Below that, one of **5 dancing stick-figure animations** plays, chosen by hashing the Spotify track ID (same song = same dance every time; different songs vary):
   - Waltz (couple spinning together)
   - Club (3 figures jumping, offset rhythm)
   - Solo Freeform (one figure, loose movement)
   - Waltz Twirl (lead with raised arm, partner orbiting)
   - Group Jump (trio jumping in tight sync)
2. **Weather** — idle default screen when nothing's playing. Shows temperature and conditions via the free Open-Meteo API (no key required), refreshed every 5 minutes, with a twirling ballerina animation alongside it. Failed fetches retry in ~10 seconds instead of waiting the full 5-minute interval.
3. **Plane Overhead** — when OpenSky Network detects an aircraft within ~15 miles, this screen alternates with the Weather screen every 15 seconds, so weather updates still get seen even while a plane's overhead. Shows callsign and altitude always; shows origin/destination when adsbdb.com can resolve the flight's public schedule (commercial flights only — private/small aircraft show "destination unknown," which is expected, not a bug).

### Known limitation, by design
Spotify's `/audio-features` endpoint (which would have given real tempo/energy data) is blocked for developer apps created after late 2024, so the dance animations are **not** driven by actual song tempo — they run at a fixed, pleasant pace with per-track variety via the hash trick above, rather than truly reacting to the music.

---

## Files

- `cassette_companion.ino` — main program
- `secrets.h` — Wi-Fi credentials + Spotify Client ID/Secret/refresh token (kept separate from the main file; never share or commit this)

---

## Day-to-Day Use

- **Power on/off:** the slide switch. Off saves battery when not in use.
- **Charging:** plug USB-C into the **TP4056's** port, not the ESP32's. LED solid = charging, changes color when full (roughly 1–2 hours for a full charge on a small pouch cell). Charging works regardless of switch position.
- **Battery life while running:** expect several hours of continuous runtime, not days — the device polls Wi-Fi/Spotify continuously rather than sleeping.
- **No re-flashing needed between charges** — code persists in flash memory permanently. Only Wi-Fi/Spotify session state resets on power-up, which happens automatically within a few seconds.
