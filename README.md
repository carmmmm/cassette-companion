# Cassette Companion

A hand-built cassette-case device with a small OLED screen that shows live Spotify "now playing" info (complete with dancing stick figures), idle weather, and overhead flight tracking — all running standalone off a rechargeable battery.

---

## What it does

- **Now Playing mode** — when Spotify is playing something, the screen shows the track name and artist (scrolling if too long), plus one of 5 procedurally-chosen dancing stick-figure animations (waltz, club, solo freeform, waltz twirl, group jump). The dance style is consistent per track (same song = same dance) via a hash of the track ID.
- **Weather mode** — the idle default. Shows current temperature and conditions for your location via the free [Open-Meteo](https://open-meteo.com/) API (no key required), with a small twirling ballerina animation.
- **Plane tracking mode** — if an aircraft is detected overhead using [OpenSky Network](https://opensky-network.org/)'s free API, this screen alternates with Weather every 15 seconds. Shows the flight's callsign and altitude always; shows origin/destination when [adsbdb.com](https://www.adsbdb.com/) can resolve it (commercial flights only — private/small aircraft will show "destination unknown").

## Hardware

| Part | Notes |
|---|---|
| ESP32 DevKit V1 | Main microcontroller |
| 1.3" OLED, 128x64, I2C, SH1106 driver | Display |
| TP4056 charger/protection board | USB-C, handles battery charging |
| LiPo battery, 3.7V pouch cell | Power source |
| MT3608 step-up boost converter | Boosts battery voltage to 5V for the ESP32 |
| Slide switch (3-pin/SPDT) | Power on/off |
| Copper-clad board | Mounting base |
| Old cassette case | Enclosure |

See [`docs/build-guide.md`](docs/build-guide.md) for full wiring instructions, part numbers, and lessons learned during the build (including two real gotchas that cost significant debugging time — a physically dead boost-converter trimpot, and a 3-pin switch wired incorrectly).

## Software setup

1. Install [Arduino IDE](https://www.arduino.cc/en/software).
2. Add ESP32 board support via Boards Manager URL:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
3. Select board: **ESP32 Dev Module**.
4. Install libraries via Library Manager: `ArduinoJson`, `Adafruit GFX Library`, `Adafruit SH110X`.
5. Copy `secrets.example.h` to `secrets.h` and fill in your own Wi-Fi and Spotify credentials (see below).
6. Flash `cassette_companion.ino` to your board.

### Getting Spotify credentials

1. Create an app at the [Spotify Developer Dashboard](https://developer.spotify.com/dashboard).
2. Set the redirect URI to `http://127.0.0.1:8888/callback`.
3. Note your Client ID and Client Secret.
4. Run a one-time OAuth script locally (see [`scripts/get_token.py`](scripts/get_token.py)) to generate your refresh token — this only needs to be done once.

⚠️ **Never commit `secrets.h` to this repo.** It's already listed in `.gitignore`, but double-check before pushing.

## Known limitations

- Spotify's `/audio-features` endpoint (which would provide real tempo/energy data) is blocked for developer apps created after late 2024. The dance animations are therefore *not* driven by actual song tempo — variety comes from hashing the track ID instead of true audio analysis.
- Plane route resolution only works for commercial flights with public schedule data.

## License

MIT — do whatever you want with this, just don't blame me if your trimpot is also secretly dead.
