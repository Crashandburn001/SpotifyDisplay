
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <base64.h>


// --- PINS ---
#define TFT_CS    5
#define TFT_DC    3
#define TFT_RST  -1
#define TFT_SCLK  2
#define TFT_MOSI  4
#define PIN_POT_VOLUME 0
#define PIN_SKIP_NEXT  10
#define PIN_PLAY_PAUSE 7
#define PIN_SKIP_PREV  6


// --- CREDENTIALS ---
const char* ssid = "White House Wifi";
const char* password = "Walter2025@";
const char* client_id = "cb184423bdc544ef9c49c766e407cb93";
const char* client_secret = "70398bb508a74434b3b0fa0d94df0fbf";
const char* refresh_token = "AQBDb7GJrgNzKh1f15osCNGJs4oLxQbchkJttLR-OB7noAhwE5lJ0GDEd8pioDgglFny5J50n5OJVx5P7-UuPCD-yvIZw4Qv74pXNHQgCjhUGOVXx-I7OFk3gdIoc5xHnIo";

IPAddress ip(192, 168, 1, 150);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// --- GLOBALS ---
String access_token = "";
float smoothedPot = 4420; // Default to physical max
int lastVolume = 100;     // Default to 100% logic max
String lastArtist = "";
String lastTrack = "";
String lastAlbum = "";
String lastArtworkUrl = "";
unsigned long lastUpdate = 0;
unsigned long nextDisplayPollAt = 0;
unsigned long nextVolumeSendAt = 0;
unsigned long spotifyRetryAt = 0;
bool isPlaying = false;
uint16_t accentColor = ST77XX_GREEN;
uint16_t accentDark = ST77XX_BLUE;

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return tft.color565(r, g, b);
}

uint16_t blendColor(uint16_t a, uint16_t b, uint8_t amount) {
  uint8_t ar = (a >> 11) & 0x1F;
  uint8_t ag = (a >> 5) & 0x3F;
  uint8_t ab = a & 0x1F;
  uint8_t br = (b >> 11) & 0x1F;
  uint8_t bg = (b >> 5) & 0x3F;
  uint8_t bb = b & 0x1F;

  uint8_t rr = (ar * (255 - amount) + br * amount) / 255;
  uint8_t rg = (ag * (255 - amount) + bg * amount) / 255;
  uint8_t rb = (ab * (255 - amount) + bb * amount) / 255;
  return (rr << 11) | (rg << 5) | rb;
}

uint16_t colorFromHash(const String& value) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < value.length(); i++) {
    hash ^= (uint8_t)value[i];
    hash *= 16777619u;
  }

  uint8_t r = 90 + (hash & 0x3F);
  uint8_t g = 80 + ((hash >> 8) & 0x5F);
  uint8_t b = 90 + ((hash >> 16) & 0x3F);
  return tft.color565(r, g, b);
}

void setThemeFromTrack(const String& artist, const String& track) {
  accentColor = colorFromHash(artist + "|" + track);
  accentDark = blendColor(accentColor, ST77XX_BLACK, 140);
}

void drawBackground() {
  tft.fillScreen(ST77XX_BLACK);
  for (int y = 0; y < 128; y += 8) {
    uint16_t lineColor = blendColor(accentDark, ST77XX_BLACK, (uint8_t)(y * 2));
    tft.drawFastHLine(0, y, 160, lineColor);
  }
}

void drawTopBar() {
  tft.fillRect(0, 0, 160, 16, accentDark);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE, accentDark);
  tft.setCursor(6, 4);
  tft.print("Spotify Now Playing");

  tft.fillRoundRect(116, 2, 38, 12, 3, isPlaying ? accentColor : ST77XX_RED);
  tft.setCursor(isPlaying ? 122 : 121, 4);
  tft.setTextColor(ST77XX_BLACK, isPlaying ? accentColor : ST77XX_RED);
  tft.print(isPlaying ? "LIVE" : "PAUSE");
}

void drawAlbumPanel(const String& artist, const String& track, const String& album) {
  tft.fillRoundRect(6, 24, 148, 68, 6, blendColor(accentDark, ST77XX_BLACK, 90));
  tft.drawRoundRect(6, 24, 148, 68, 6, accentColor);

  tft.fillRoundRect(12, 30, 42, 42, 5, accentColor);
  tft.drawRoundRect(12, 30, 42, 42, 5, ST77XX_WHITE);

  tft.setTextColor(ST77XX_WHITE, accentColor);
  tft.setTextSize(2);
  tft.setCursor(20, 40);
  if (artist.length() > 0) {
    tft.print(artist.substring(0, 1));
  } else {
    tft.print("?");
  }

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE, blendColor(accentDark, ST77XX_BLACK, 90));
  tft.setCursor(62, 32);
  tft.print("Artist");
  tft.setTextColor(accentColor, blendColor(accentDark, ST77XX_BLACK, 90));
  tft.setCursor(62, 43);
  tft.print(artist.substring(0, 18));

  tft.setTextColor(ST77XX_WHITE, blendColor(accentDark, ST77XX_BLACK, 90));
  tft.setCursor(62, 58);
  tft.print("Track");
  tft.setTextColor(ST77XX_WHITE, blendColor(accentDark, ST77XX_BLACK, 90));
  tft.setCursor(62, 69);
  tft.print(track.substring(0, 18));

  if (album.length() > 0) {
    tft.setTextColor(ST77XX_CYAN, blendColor(accentDark, ST77XX_BLACK, 90));
    tft.setCursor(62, 84);
    tft.print(album.substring(0, 18));
  }
}

void drawVolumeBar(int vol) {
  tft.fillRoundRect(10, 104, 140, 14, 5, blendColor(accentDark, ST77XX_BLACK, 130));
  tft.drawRoundRect(10, 104, 140, 14, 5, accentColor);
  int barWidth = map(vol, 0, 100, 0, 136);
  uint16_t fillColor = blendColor(accentColor, ST77XX_WHITE, 50);
  tft.fillRoundRect(11, 105, barWidth, 12, 4, fillColor);
  tft.setCursor(126, 88);
  tft.setTextColor(ST77XX_WHITE, blendColor(accentDark, ST77XX_BLACK, 90));
  tft.print(String(vol) + "%");
}
void renderNowPlaying(const String& artist, const String& track, const String& album, const String& artworkUrl) {
  if (artist == "" || track == "") return;

  setThemeFromTrack(artist, track);
  drawBackground();
  drawTopBar();
  drawAlbumPanel(artist, track, album);
  drawVolumeBar(lastVolume);

  if (artworkUrl != "") {
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.setCursor(10, 123);
    tft.print("Art URL saved for future decode");
  }

  lastArtist = artist;
  lastTrack = track;
  lastAlbum = album;
  lastArtworkUrl = artworkUrl;
}

unsigned long parseRetryAfterMs(const String& retryAfter) {
  if (retryAfter.length() == 0) {
    return 5000;
  }

  long seconds = retryAfter.toInt();
  if (seconds <= 0) {
    return 5000;
  }

  return (unsigned long)seconds * 1000UL;
}
// --- NATIVE SPOTIFY API LOGIC ---
void refreshToken() {
  WiFiClientSecure client;
  client.setInsecure(); // Skip certificate verification for speed
  HTTPClient http;
  
  // CORRECTED URL
  http.begin(client, "https://accounts.spotify.com/api/token");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  
  String auth = String(client_id) + ":" + String(client_secret);
  String auth64 = base64::encode(auth);
  http.addHeader("Authorization", "Basic " + auth64);
  
  String payload = "grant_type=refresh_token&refresh_token=" + String(refresh_token);
  int httpCode = http.POST(payload);
  
  if (httpCode == 200) {
    String response = http.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, response);
    access_token = doc["access_token"].as<String>();
    Serial.println("Token Successfully Refreshed.");
  } else {
    Serial.printf("Token Refresh Failed. HTTP Code: %d\n", httpCode);
  }
  http.end();
}

void spotifyCommand(String endpoint, String method = "POST") {
  if (access_token == "") return;
  
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  // CORRECTED URL
  http.begin(client, "https://api.spotify.com/v1/me/player/" + endpoint);
  http.addHeader("Authorization", "Bearer " + access_token);
  http.addHeader("Content-Length", "0");
  
  int httpCode = (method == "PUT") ? http.PUT("") : http.POST("");
  if (httpCode == 401) refreshToken();
  if (httpCode == 429) {
    spotifyRetryAt = millis() + 3000;
  }
  http.end();
}

void setSpotifyVolume(int vol) {
  if (access_token == "") return;
  
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  // CORRECTED URL
  http.begin(client, "https://api.spotify.com/v1/me/player/volume?volume_percent=" + String(vol));
  http.addHeader("Authorization", "Bearer " + access_token);
  http.addHeader("Content-Length", "0");
  
  int httpCode = http.PUT("");
  if (httpCode == 401) refreshToken();
  if (httpCode == 429) {
    spotifyRetryAt = millis() + 3000;
  }
  http.end();
}

void updateDisplayData() {
  if (access_token == "") {
    refreshToken();
    return;
  }
  
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  // CORRECTED URL
  const char* headerKeys[] = {"Retry-After"};
  http.collectHeaders(headerKeys, 1);
  http.begin(client, "https://api.spotify.com/v1/me/player/currently-playing");
  http.addHeader("Authorization", "Bearer " + access_token);
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(8192); 
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      isPlaying = doc["is_playing"] | false;
      String artist = doc["item"]["artists"][0]["name"].as<String>();
      String track = doc["item"]["name"].as<String>();
      String album = doc["item"]["album"]["name"].as<String>();
      String artworkUrl = doc["item"]["album"]["images"][0]["url"].as<String>();

      drawTopBar();
      
      if (artist != "null" && (artist != lastArtist || track != lastTrack || album != lastAlbum)) {
        renderNowPlaying(artist, track, album, artworkUrl);
      }
    }
  } else if (httpCode == 401) {
    refreshToken(); 
  } else if (httpCode == 429) {
    spotifyRetryAt = millis() + parseRetryAfterMs(http.header("Retry-After"));
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  
  pinMode(PIN_SKIP_NEXT, INPUT_PULLUP);
  pinMode(PIN_PLAY_PAUSE, INPUT_PULLUP);
  pinMode(PIN_SKIP_PREV, INPUT_PULLUP);

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  drawBackground();
  drawTopBar();
  drawAlbumPanel("Connecting", "Waiting for Spotify", "");

  WiFi.begin(ssid, password);
  
  while(WiFi.status() != WL_CONNECTED) { delay(100); }
  
  // Initial Boot Data
  refreshToken(); 
  drawVolumeBar(100);
}

void loop() {
  // 1. Handle Buttons
  if (digitalRead(PIN_SKIP_NEXT) == LOW)  { spotifyCommand("next", "POST"); delay(400); }
  if (digitalRead(PIN_SKIP_PREV) == LOW)  { spotifyCommand("previous", "POST"); delay(400); }
  if (digitalRead(PIN_PLAY_PAUSE) == LOW) { 
     if (isPlaying) {
         spotifyCommand("pause", "PUT");
         isPlaying = false;
     } else {
         spotifyCommand("play", "PUT");
         isPlaying = true;
     }
     delay(400); 
  }

  // 2. Handle Volume (0 to 4420)
  int rawPot = analogRead(PIN_POT_VOLUME);
  smoothedPot = (smoothedPot * 0.9) + (rawPot * 0.1);
  
  // Map constraint updated to your 4420 pot max
  int currentVol = map((int)constrain(smoothedPot, 0, 4420), 0, 4420, 0, 100);

  if (abs(currentVol - lastVolume) > 2 && millis() >= nextVolumeSendAt && millis() >= spotifyRetryAt) {
    setSpotifyVolume(currentVol);
    drawVolumeBar(currentVol);
    lastVolume = currentVol;
    nextVolumeSendAt = millis() + 1200;
  }

  // 3. Poll Spotify API Data periodically
  if (millis() >= nextDisplayPollAt && millis() >= spotifyRetryAt) {
    lastUpdate = millis();
    nextDisplayPollAt = millis() + 2500;
    updateDisplayData();
  }
}