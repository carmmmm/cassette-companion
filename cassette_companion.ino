#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <time.h>
#include "secrets.h"

#define SDA_PIN 21
#define SCL_PIN 19

Adafruit_SH1106G display = Adafruit_SH1106G(128, 64, &Wire, -1);

String accessToken = "";
unsigned long tokenExpiryMillis = 0;

// Set these to your own location
const float LAT = 36.2168;
const float LON = -81.6746;
const float BOX = 0.22; // degrees, roughly 15mi radius for plane detection

struct PlaneInfo {
  bool found = false;
  String callsign = "";
  float altitudeFt = 0;
  float headingDeg = 0;
  String origin = "";
  String destination = "";
  bool routeKnown = false;
};

struct NowPlaying {
  bool isPlaying = false;
  String trackName = "";
  String artistName = "";
  String trackId = "";
};

volatile bool sharedIsPlaying = false;
String sharedTrackName = "";
String sharedArtistName = "";
String sharedTrackId = "";
unsigned long sharedTrackStartMillis = 0;

volatile bool sharedPlaneFound = false;
PlaneInfo sharedPlaneInfo;

volatile float sharedTempF = 0;
String sharedWeatherDesc = "";

String lastTrackId = "";

int balleFrame = 0;
unsigned long lastBalleFrameMillis = 0;
const unsigned long BALLE_FRAME_MS = 200;

int danceFrame = 0;
unsigned long lastDanceFrameMillis = 0;
const unsigned long DANCE_FRAME_MS = 220;
int danceStyle = 0; // 0=waltz, 1=club(3), 2=solo, 3=waltz twirl, 4=group jump

int trackScrollX = 0;
int artistScrollX = 0;
unsigned long lastTrackScrollMillis = 0;
unsigned long lastArtistScrollMillis = 0;
const int SCROLL_SPEED_MS = 40;

bool showingPlaneScreen = true;
unsigned long lastRotationSwitchMillis = 0;
const unsigned long ROTATION_DURATION_MS = 15000;

// ---------- Wi-Fi ----------
void connectWifi() {
  for (int i = 0; i < WIFI_COUNT; i++) {
    Serial.printf("Trying WiFi: %s\n", WIFI_LIST[i].ssid);
    WiFi.begin(WIFI_LIST[i].ssid, WIFI_LIST[i].pass);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
      delay(300);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("\nConnected, IP: %s\n", WiFi.localIP().toString().c_str());
      return;
    }
    Serial.println("\nFailed, trying next...");
  }
  Serial.println("Could not connect to any WiFi network.");
}

// ---------- Base64 ----------
String base64Encode(const String& input) {
  static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String out = "";
  int val = 0, valb = -6;
  for (unsigned char c : input) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      out += chars[(val >> valb) & 0x3F];
      valb -= 6;
    }
  }
  if (valb > -6) out += chars[((val << 8) >> (valb + 8)) & 0x3F];
  while (out.length() % 4) out += '=';
  return out;
}

// ---------- Spotify token ----------
bool refreshAccessToken() {
  HTTPClient http;
  http.begin("https://accounts.spotify.com/api/token");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String auth = String(SPOTIFY_CLIENT_ID) + ":" + String(SPOTIFY_CLIENT_SECRET);
  http.addHeader("Authorization", "Basic " + base64Encode(auth));
  String body = "grant_type=refresh_token&refresh_token=" + String(SPOTIFY_REFRESH_TOKEN);
  int code = http.POST(body);
  if (code != 200) {
    http.end();
    return false;
  }
  StaticJsonDocument<1024> doc;
  deserializeJson(doc, http.getString());
  accessToken = doc["access_token"].as<String>();
  int expiresIn = doc["expires_in"] | 3600;
  tokenExpiryMillis = millis() + (expiresIn - 60) * 1000UL;
  http.end();
  return true;
}

void ensureValidToken() {
  if (accessToken == "" || millis() > tokenExpiryMillis) refreshAccessToken();
}

// ---------- Spotify: currently playing ----------
bool getCurrentlyPlaying(NowPlaying& np) {
  ensureValidToken();
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  http.begin("https://api.spotify.com/v1/me/player/currently-playing");
  http.addHeader("Authorization", "Bearer " + accessToken);
  int code = http.GET();
  if (code == 204) { np.isPlaying = false; http.end(); return true; }
  if (code != 200) { http.end(); return false; }
  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) return false;
  np.isPlaying = doc["is_playing"] | false;
  if (np.isPlaying) {
    np.trackName = doc["item"]["name"].as<String>();
    np.artistName = doc["item"]["artists"][0]["name"].as<String>();
    np.trackId = doc["item"]["id"].as<String>();
  }
  return true;
}

// ---------- Weather ----------
bool getWeather(float& tempF, String& description) {
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(LAT, 4)
             + "&longitude=" + String(LON, 4)
             + "&current=temperature_2m,weather_code&temperature_unit=fahrenheit";
  http.begin(url);
  int code = http.GET();
  if (code != 200) { http.end(); return false; }
  String payload = http.getString();
  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) { http.end(); return false; }
  tempF = doc["current"]["temperature_2m"] | 0.0;
  int wcode = doc["current"]["weather_code"] | 0;
  if (wcode == 0) description = "Clear";
  else if (wcode <= 3) description = "Cloudy";
  else if (wcode <= 67) description = "Rainy";
  else if (wcode <= 77) description = "Snowy";
  else description = "Stormy";
  http.end();
  return true;
}

// ---------- Plane detection (OpenSky) ----------
bool checkForPlane(PlaneInfo& plane) {
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  String url = "https://opensky-network.org/api/states/all?lamin=" + String(LAT - BOX, 4)
             + "&lomin=" + String(LON - BOX, 4)
             + "&lamax=" + String(LAT + BOX, 4)
             + "&lomax=" + String(LON + BOX, 4);
  http.begin(url);
  int code = http.GET();
  if (code != 200) { http.end(); return false; }

  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) return false;

  JsonArray states = doc["states"].as<JsonArray>();
  if (states.isNull() || states.size() == 0) {
    plane.found = false;
    return true;
  }

  for (JsonArray s : states) {
    bool onGround = s[8] | false;
    if (onGround) continue;
    plane.found = true;
    plane.callsign = s[1].as<String>();
    plane.callsign.trim();
    plane.altitudeFt = (s[7].isNull() ? 0.0 : s[7].as<float>()) * 3.28084;
    plane.headingDeg = s[10].isNull() ? 0.0 : s[10].as<float>();
    return true;
  }
  plane.found = false;
  return true;
}

// ---------- Route lookup ----------
bool getFlightRoute(PlaneInfo& plane) {
  if (plane.callsign == "") return false;
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  http.begin("https://api.adsbdb.com/v0/callsign/" + plane.callsign);
  int code = http.GET();
  if (code != 200) { http.end(); return false; }
  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) return false;

  JsonObject route = doc["response"]["flightroute"];
  if (route.isNull()) return false;

  plane.origin = route["origin"]["municipality"].as<String>();
  plane.destination = route["destination"]["municipality"].as<String>();
  plane.routeKnown = (plane.origin != "" && plane.destination != "");
  return plane.routeKnown;
}

// ---------- Hash for consistent dance-per-track ----------
int hashTrackId(const String& id) {
  unsigned long h = 0;
  for (int i = 0; i < id.length(); i++) {
    h = h * 31 + id[i];
  }
  return h % 5;
}

// ---------- Background network task (runs on core 0) ----------
void networkTask(void* parameter) {
  unsigned long lastPollMillis = 0;
  unsigned long lastPlaneCheckMillis = 0;
  unsigned long lastWeatherFetchMillis = 0;
  const unsigned long POLL_INTERVAL_MS = 8000;
  const unsigned long PLANE_CHECK_INTERVAL_MS = 60000;
  const unsigned long WEATHER_REFRESH_MS = 5UL * 60UL * 1000UL;

  for (;;) {
    if (millis() - lastPollMillis > POLL_INTERVAL_MS) {
      lastPollMillis = millis();
      NowPlaying np;
      if (getCurrentlyPlaying(np)) {
        sharedIsPlaying = np.isPlaying;
        if (np.isPlaying) {
          sharedTrackName = np.trackName;
          sharedArtistName = np.artistName;
          if (np.trackId != lastTrackId) {
            danceStyle = hashTrackId(np.trackId);
            sharedTrackStartMillis = millis();
            lastTrackId = np.trackId;
          }
        } else {
          lastTrackId = "";
        }
      }
    }

    if (millis() - lastPlaneCheckMillis > PLANE_CHECK_INTERVAL_MS) {
      lastPlaneCheckMillis = millis();
      PlaneInfo p;
      if (checkForPlane(p) && p.found) {
        getFlightRoute(p);
        sharedPlaneInfo = p;
        sharedPlaneFound = true;
      } else {
        sharedPlaneFound = false;
      }
    }

    if (millis() - lastWeatherFetchMillis > WEATHER_REFRESH_MS || lastWeatherFetchMillis == 0) {
      float tempF; String desc;
      if (getWeather(tempF, desc)) {
        sharedTempF = tempF;
        sharedWeatherDesc = desc;
        lastWeatherFetchMillis = millis();
      } else {
        lastWeatherFetchMillis = millis() - WEATHER_REFRESH_MS + 10000;
      }
    }

    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}

// ---------- Ballerina (weather screen) ----------
void drawBallerina(int x, int y) {
  float angle = (balleFrame % 8) * (PI / 4.0);
  int armLen = 12;
  display.drawCircle(x, y - 18, 4, SH110X_WHITE);
  display.drawLine(x, y - 14, x, y, SH110X_WHITE);
  int ax1 = x + cos(angle) * armLen;
  int ay1 = y - 10 + sin(angle) * 4;
  int ax2 = x - cos(angle) * armLen;
  int ay2 = y - 10 - sin(angle) * 4;
  display.drawLine(x, y - 10, ax1, ay1, SH110X_WHITE);
  display.drawLine(x, y - 10, ax2, ay2, SH110X_WHITE);
  int flare = 6 + abs(sin(angle)) * 6;
  display.drawLine(x, y, x - flare, y + 10, SH110X_WHITE);
  display.drawLine(x, y, x + flare, y + 10, SH110X_WHITE);
  display.drawLine(x - flare, y + 10, x + flare, y + 10, SH110X_WHITE);
  display.drawLine(x, y + 10, x - 4, y + 18, SH110X_WHITE);
  display.drawLine(x, y + 10, x + 4, y + 18, SH110X_WHITE);
}

void updateBalleFrame() {
  if (millis() - lastBalleFrameMillis > BALLE_FRAME_MS) {
    balleFrame++;
    lastBalleFrameMillis = millis();
  }
}

// ---------- Stick figure primitive ----------
void drawStickFigure(int x, int y, float armAngle, float legAngle, int headR) {
  display.drawCircle(x, y - 16, headR, SH110X_WHITE);
  display.drawLine(x, y - 16 + headR, x, y + 4, SH110X_WHITE);

  int armLen = 9;
  display.drawLine(x, y - 8,
    x + cos(armAngle) * armLen, y - 8 + sin(armAngle) * armLen, SH110X_WHITE);
  display.drawLine(x, y - 8,
    x - cos(armAngle) * armLen, y - 8 - sin(armAngle) * armLen, SH110X_WHITE);

  int legLen = 10;
  display.drawLine(x, y + 4,
    x + sin(legAngle) * legLen, y + 4 + legLen, SH110X_WHITE);
  display.drawLine(x, y + 4,
    x - sin(legAngle) * legLen, y + 4 + legLen, SH110X_WHITE);
}

void drawStickFigureRaisedArm(int x, int y, float legAngle, int headR) {
  display.drawCircle(x, y - 16, headR, SH110X_WHITE);
  display.drawLine(x, y - 16 + headR, x, y + 4, SH110X_WHITE);
  display.drawLine(x, y - 8, x - 8, y - 18, SH110X_WHITE);
  display.drawLine(x, y - 8, x + 7, y - 4, SH110X_WHITE);
  int legLen = 10;
  display.drawLine(x, y + 4,
    x + sin(legAngle) * legLen, y + 4 + legLen, SH110X_WHITE);
  display.drawLine(x, y + 4,
    x - sin(legAngle) * legLen, y + 4 + legLen, SH110X_WHITE);
}

// ---------- Dance 0: Waltz ----------
void drawWaltz() {
  float t = danceFrame * 0.3;
  int cx1 = 54 + sin(t) * 6;
  int cx2 = 74 - sin(t) * 6;
  int cy = 42;
  float armAngle = sin(t) * 0.6;
  drawStickFigure(cx1, cy, armAngle + 0.3, cos(t) * 0.3, 4);
  drawStickFigure(cx2, cy, -armAngle - 0.3, -cos(t) * 0.3, 4);
  display.drawLine(cx1 + 6, cy - 8, cx2 - 6, cy - 8, SH110X_WHITE);
}

// ---------- Dance 1: Club (3 figures) ----------
void drawClub3() {
  float t = danceFrame * 0.5;
  int bounce1 = abs((int)(sin(t) * 5));
  int bounce2 = abs((int)(sin(t + 1.2) * 5));
  int bounce3 = abs((int)(sin(t + 2.4) * 5));
  drawStickFigure(38, 46 - bounce1, sin(t * 2) * 1.2, sin(t) * 0.5, 3);
  drawStickFigure(64, 46 - bounce2, -sin(t * 2 + 1) * 1.2, sin(t + 1) * 0.5, 3);
  drawStickFigure(90, 46 - bounce3, sin(t * 2 + 2) * 1.2, sin(t + 2) * 0.5, 3);
}

// ---------- Dance 2: Solo freeform ----------
void drawFreeform() {
  float t = danceFrame * 0.4;
  int bounce = abs((int)(sin(t * 1.3) * 6));
  drawStickFigure(64, 46 - bounce, sin(t) * 1.4, cos(t * 0.8) * 0.7, 5);
}

// ---------- Dance 3: Waltz Twirl ----------
void drawWaltzTwirl() {
  float t = danceFrame * 0.35;
  int leadX = 46;
  int cy = 42;
  drawStickFigureRaisedArm(leadX, cy + (int)(sin(t * 0.5) * 1), cos(t * 0.4) * 0.2, 4);
  float orbitAngle = t * 2.2;
  int px = leadX + 22 + cos(orbitAngle) * 4;
  int py = cy + sin(orbitAngle) * 3;
  float spinArm = orbitAngle;
  drawStickFigure(px, py, spinArm, sin(orbitAngle * 2) * 0.4, 4);
}

// ---------- Dance 4: Group Jump ----------
void drawGroupJump() {
  float t = danceFrame * 0.6;
  int bounce = abs((int)(sin(t) * 8));
  drawStickFigure(42, 46 - bounce, sin(t * 3) * 1.0, sin(t * 2) * 0.6, 3);
  drawStickFigure(64, 46 - bounce, -sin(t * 3) * 1.0, -sin(t * 2) * 0.6, 3);
  drawStickFigure(86, 46 - bounce, sin(t * 3 + 1) * 1.0, sin(t * 2 + 1) * 0.6, 3);
}

void updateDanceFrame() {
  if (millis() - lastDanceFrameMillis > DANCE_FRAME_MS) {
    danceFrame++;
    lastDanceFrameMillis = millis();
  }
}

// ---------- Scrolling marquee ----------
void drawScrollingText(const String& text, int y, int& scrollX, unsigned long& lastMillis) {
  int textWidth = text.length() * 6;
  if (textWidth <= 128) {
    display.setCursor(0, y);
    display.print(text);
    scrollX = 0;
  } else {
    if (millis() - lastMillis > SCROLL_SPEED_MS) {
      scrollX -= 1;
      if (scrollX < -textWidth) scrollX = 128;
      lastMillis = millis();
    }
    display.setCursor(scrollX, y);
    display.print(text);
  }
}

// ---------- Now Playing screen ----------
void drawNowPlaying() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);

  drawScrollingText(sharedTrackName, 0, trackScrollX, lastTrackScrollMillis);
  drawScrollingText(sharedArtistName, 9, artistScrollX, lastArtistScrollMillis);
  display.drawLine(0, 18, 127, 18, SH110X_WHITE);

  updateDanceFrame();
  switch (danceStyle) {
    case 0: drawWaltz(); break;
    case 1: drawClub3(); break;
    case 2: drawFreeform(); break;
    case 3: drawWaltzTwirl(); break;
    case 4: drawGroupJump(); break;
  }

  display.display();
}

void drawWeatherScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.println("WEATHER");
  display.setTextSize(2);
  display.setCursor(0, 22);
  display.printf("%.0fF", sharedTempF);
  display.setTextSize(1);
  display.setCursor(0, 44);
  display.println(sharedWeatherDesc);
  drawBallerina(105, 40);
  display.display();
}

void drawPlaneScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.println("PLANE OVERHEAD");
  display.setCursor(0, 15);
  display.print("Flight: ");
  display.println(sharedPlaneInfo.callsign);
  display.setCursor(0, 27);
  display.printf("Alt: %.0f ft\n", sharedPlaneInfo.altitudeFt);
  display.setCursor(0, 39);
  if (sharedPlaneInfo.routeKnown) {
    display.println(sharedPlaneInfo.origin + " ->");
    display.setCursor(0, 51);
    display.println(sharedPlaneInfo.destination);
  } else {
    display.println("Destination unknown");
  }
  display.display();
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!display.begin(0x3C, true)) {
    Serial.println("OLED not found!");
    while (1);
  }
  display.clearDisplay();
  display.setCursor(0, 25);
  display.println("Connecting WiFi...");
  display.display();
  connectWifi();
  refreshAccessToken();
  lastRotationSwitchMillis = millis();

  xTaskCreatePinnedToCore(networkTask, "NetworkTask", 8192, NULL, 1, NULL, 0);
}

// ---------- Loop (core 1, animation only, never blocked by network) ----------
void loop() {
  updateBalleFrame();

  if (sharedIsPlaying) {
    drawNowPlaying();
  } else if (sharedPlaneFound) {
    if (millis() - lastRotationSwitchMillis > ROTATION_DURATION_MS) {
      showingPlaneScreen = !showingPlaneScreen;
      lastRotationSwitchMillis = millis();
    }
    if (showingPlaneScreen) drawPlaneScreen();
    else drawWeatherScreen();
  } else {
    showingPlaneScreen = true;
    drawWeatherScreen();
  }

  delay(30);
}
