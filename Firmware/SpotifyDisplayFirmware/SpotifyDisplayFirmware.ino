#include <Arduino.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <WiFi.h>
#include <SpotifyEsp32.h>
#include <SPI.h>

#define TFT_CS 0
#define TFT_RST 1
#define TFT_DC 2
#define TFT_SCLK 3
#define TFT_MOSI 4

#define PIN_SKIP_NEXT 6
#define PIN_PLAY_PAUSE 7
#define PIN_SKIP_PREV 8
#define PIN_POT_VOLUME A5

#define VOL_X 10
#define VOL_Y 110
#define VOL_WIDTH 100
#define VOL_HEIGHT 15

char* SSID = "INSERT_SSID_HERE";
char* PASSWORD = "INSERT_PASSWORD_HERE";
const char* CLIENT_ID = "CLIENT_ID";
const char* CLIENT_SECRET = "CLIENT_SECRET";

unsigned long lastUpdateMillis = 0;
const long updateInterval = 2000;
unsigned long lastVolumeMillis = 0;
int displayVolume = -1;

Spotify spotify(CLIENT_ID, CLIENT_SECRET);
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_RST, TFT_MOSI, TFT_SCLK, TFT_RST);

String lastArtist = "";
String lastTrackname = "";
int lastVolume = -1;

void setup() {
  Serial.begin(115200);

  pinMode(PIN_SKIP_NEXT, INPUT_PULLUP);
  pinMode(PIN_PLAY_PAUSE, INPUT_PULLUP);
  pinMode(PIN_SKIP_PREV, INPUT_PULLUP);


  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  Serial.println("TFT Initialized");
  tft.fillScreen(ST77XX_BLACK);


  WiFi.begin(SSID, PASSWORD); //Connect to Wifi
  Serial.print("Connecting to WiFi...");
  while(WiFi.status() != WL_CONNECTED)  {
    delay(1000);
    Serial.print(".");
  }
  Serial.printf("\nConnected\n"); 

  tft.setCursor(0,0);
  tft.write(WiFi.localIP().toString().c_str());

  //Spotify:
  spotify.begin();
  while(!spotify.is_auth()) {
    spotify.handle_client();
  }
  Serial.println("Connected to Spotify!");
}

void handleControls() {
  if (digitalRead(PIN_SKIP_NEXT) == LOW) {
    spotify.skip_to_next();
    delay(300);
  }

  if (digitalRead(PIN_SKIP_PREV) == LOW) {
    spotify.skip_to_previous();
    delay(300);
  }

  if (digitalRead(PIN_PLAY_PAUSE) == LOW) {
    if(spotify.is_playing()) {
      spotify.pause_playback();
    }
    else {
      spotify.start_a_users_playback();
    }
    delay(300);
  }

  int potValue = analogRead(PIN_POT_VOLUME);
  int currentVolume = map(potValue, 0, 4095, 0, 100);

  if(abs(currentVolume - lastVolume) > 2) {
    
    if (millis() - lastVolumeMillis > 200) {
      spotify.set_volume(currentVolume);
      lastVolume = currentVolume;
      lastVolumeMillis = millis();
      displayVolume = currentVolume;
    }
  }

}

void updateDisplay() {
  String currentArtist = spotify.current_artist_names();
  String currentTrackname = spotify.current_track_name();

  if (lastArtist != currentArtist && currentArtist != "Something went wrong" && currentArtist.isEmpty()) {
    tft.fillScreen(ST77XX_BLACK);
    lastArtist = currentArtist;
    Serial.println("Artist: " + lastArtist);
    tft.setCursor(10,10);
    tft.write(lastArtist.c_str());
  }

  if (lastTrackname != currentTrackname && currentTrackname != "Something went wrong" && currentTrackname != "null") {
    lastTrackname = currentTrackname;
    tft.setCursor(10, 40);
    tft.write(lastTrackname.c_str());
  }

  if (displayVolume != -1) {
    drawVolumeBar(displayVolume);
    displayVolume = -1;
  }
}

void drawVolumeBar (int Volume) {
  tft.fillRect(VOL_X, VOL_Y, VOL_WIDTH, VOL_HEIGHT, ST77XX_BLACK);
  tft.drawRect(VOL_X, VOL_Y, VOL_WIDTH, VOL_HEIGHT, ST77XX_WHITE);

  int barWidth = map(Volume, 0, 100, 0, VOL_WIDTH - 2);
  tft.fillRect(VOL_X + 1, VOL_Y + 1, barWidth, VOL_HEIGHT - 2, ST77XX_GREEN);
}

void loop() {
  // put your main code here, to run repeatedly:
  spotify.handle_client();

  if (spotify.is_auth()) {
    handleControls();

    unsigned long currentMillis = millis();
    if (currentMillis - lastUpdateMillis >= updateInterval) {
      lastUpdateMillis = currentMillis;
      updateDisplay();
    }
    updateDisplay();
  }

  delay(100);
}
