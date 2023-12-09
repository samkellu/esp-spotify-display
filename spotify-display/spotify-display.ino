#include "DFRobot_GDL.h"
#include "credentials.h"
#include "webpage.h"
#include <ESP8266WiFi.h> 
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>

#include <ArduinoJson.h>
#include <base64.h>

#define POT        A0
#define TFT_CS     5  // D6
#define TFT_RST    2  // D5
#define TFT_DC     15 // D4
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

const String ENDPOINT = "https://api.spotify.com/v1/me";

DFRobot_ST7789_240x320_HW_SPI screen(TFT_DC, TFT_CS, TFT_RST);

class PlaybackBar {
  private:
    bool playing = 0;
    bool playStateFlag = 0;
    uint16_t time = 0;
    uint16_t color;
    int x;
    int y;
    int width;
    int height;
    int amplitude;
    int* wave;
    int* closingWave;
    int numSamples;
    int prevProgressX = -1;
    int deltaT = 2;

  public:
    int progress = 70;
    int trackLength = 100;

    PlaybackBar(int x, int y, int width, int height, int amplitude, int numSamples, uint16_t color) {
      this->x = x;
      this->y = y;
      this->width = width;
      this->height = height;
      this->color = color;
      this->amplitude = amplitude;
      this->numSamples = numSamples;
      this->wave = (int*)malloc(sizeof(int) * numSamples);
      this->closingWave = (int*)malloc(sizeof(int) * numSamples);

      int idx = 0;
      for (float i = 0; i < 2 * PI; i += 2*PI / (float)numSamples) {
        this->wave[idx] = sin(i) * amplitude;
        this->closingWave[idx] = this->wave[idx++]/2;
      }
    }

    void draw() {
      if (!playing && !playStateFlag) {
        return;
      }

      int bound = x + width * (progress/(float)trackLength);
      // Stop playing
      if (!playStateFlag && playing) {

        // Animate wave closing back to straight line when music is paused
        for (int i = x; i < bound; i++) {
          screen.drawPixel(i, y + wave[(time + i - deltaT)%numSamples], COLOR_RGB565_BLACK);
          screen.drawPixel(i, y + closingWave[(time + i - deltaT)%numSamples], color);
        }
        delay(30);
        for (int i = x; i < bound; i++) {
          screen.drawPixel(i, y + closingWave[(time + i - deltaT)%numSamples], COLOR_RGB565_BLACK);
          screen.drawPixel(i, y, color);
        }
        screen.drawFastHLine(x, y, width, color);
        screen.drawFastVLine(bound, y-2*height, 4*height, color);
        playing = 0;
        return;

      // Start playing after pause
      } else if (playStateFlag && !playing) {
        // Animate wave opening to full height when music starts playing again
        for (int i = x; i < bound; i++) {
          screen.drawPixel(i, y, COLOR_RGB565_BLACK);
          screen.drawPixel(i, y + closingWave[(time + i)%numSamples], color);
        }
        delay(40);
        for (int i = x; i < bound; i++) {
          screen.drawPixel(i, y + closingWave[(time + i)%numSamples], COLOR_RGB565_BLACK);
          screen.drawPixel(i, y + wave[(time + i)%numSamples], color);
        }
        playing = 1;
      }

      // Clear pixels between previous progress bar and current
      for (int i = prevProgressX; i < bound; i++) {
        screen.drawFastVLine(i, y-2*height, 4*height, COLOR_RGB565_BLACK);
      }
      screen.drawFastHLine(bound, y, width + x - bound, color);
      screen.drawFastVLine(bound, y-2*height, 4*height, color);
      prevProgressX = bound;

      // Draw wave
      for (int i = x; i < bound; i++) {
        screen.drawPixel(i, y + wave[(time + i - deltaT)%numSamples], COLOR_RGB565_BLACK);
        screen.drawPixel(i, y + wave[(time + i)%numSamples], color);
      }
      time += deltaT;
    }

    void setPlayState(bool state) {
      playStateFlag = state;
    }
};


class SpotifyConn {
  private:
      BearSSL::WiFiClientSecure client;
    HTTPClient httpsClient;
    String accessToken;
    String refreshToken;
    int expiry;

  public:
    SpotifyConn() {
      client.setInsecure();
    }

    // Connects to the network specified in credentials.h
    void connect(const char* ssid, const char* passphrase) {
      Serial.printf("Attempting connection to %s...\n", ssid);
      WiFi.begin(ssid, passphrase);
      while ((WiFi.status() != WL_CONNECTED)) {
        delay(200);
      }

      Serial.printf("Successfully connected to %s!\n", ssid);
    }

    bool getAuth(String code) {

      const char* host = "accounts.spotify.com";
      const int   port = 443;
      String      url  = "/api/token";

      if (!client.connect(host, port)) {
        Serial.println("Connection failed!");
        return false;
      }

      String auth = "Basic " + base64::encode(String(CLIENT) + ":" + String(CLIENT_SECRET));
      String body = "grant_type=authorization_code&code=" + code + "&redirect_uri=http://192.168.1.15/callback";
      String req  = "POST " + url + " HTTP/1.0\r\n" +
                    "Host: " + host + "\r\n" +
                    "Content-Length: " + String(body.length()) + "\r\n" +
                    "Content-Type: application/x-www-form-urlencoded\r\n" +
                    "Authorization: " + auth + "\r\n" +
                    "Connection: close\r\n\r\n" + 
                    body;

      client.print(req);
      client.readStringUntil('{');

      DynamicJsonDocument doc(1024);
      String json = "{" + client.readStringUntil('\r');
      DeserializationError err = deserializeJson(doc, json);

      if (err) {
        Serial.printf("Deserialisation failed for string: %s\n", json);
        Serial.println(err.f_str());
        return false;
      }

      // TODO check if these keys are present first
      accessToken = doc["access_token"].as<String>();
      refreshToken = doc["refresh_token"].as<String>();
      expiry = doc["expires_in"];

      Serial.println(accessToken);
      Serial.println(refreshToken);
      Serial.println(expiry);
      return true;
    }

    // void refreshAuth() {


    // }

    SongInfo makeRequest(String url) {

      SongInfo song = {.err = true, .name = "", .artist = ""};
      const char* host = "api.spotify.com";
      const int   port = 443;

      if (!client.connect(host, port)) {
        Serial.println("Connection failed!");
        return song;
      }

      String auth = "Bearer " + accessToken;
      String req  = "GET " + url + " HTTP/1.0\r\n" +
                    "Host: " + host + "\r\n" +
                    "Authorization: " + auth + "\r\n" +
                    "Connection: close\r\n\r\n"; 

      client.print(req);

      String ln = client.readStringUntil('{');

      int start = ln.indexOf(' ');
      int end = ln.indexOf(' ', start + 1);
      String status = ln.substring(start, end);

      if (status != 200) {
        Serial.printf("An error occurred: HTTP %s\r\n", status);
        return song;
      }

      String json = "{" + client.readStringUntil('\r');
      StaticJsonDocument<1024> doc;
      StaticJsonDocument<256> filter;
      filter["progress_ms"] = true;
      JsonObject fItem = filter.createNestedObject("item");
      fItem["name"] = true;
      fItem["duration_ms"] = true;
      fItem["artists"][0]["name"] = true;
      DeserializationError err = deserializeJson(doc, json, DeserializationOption::Filter(filter));

      if (err) {
        Serial.printf("Deserialisation failed for string: %s\n", json);
        Serial.println(err.f_str());
        return song;
      }

      song.err = false;
      JsonObject item = doc["item"];
      song.name = (String) item["name"];
      song.artist = (String) item["artists"][0]["name"];
      Serial.println(song.artist);
      Serial.println(song.name);
      return song;
    }

    // bool getNowPlaying() {

    //   StaticJsonDocument doc = makeRequest("/v1/me/player/currently-playing");

    //   if (doc.isNull()) {
    //     return false;
    //   }
      
    //   return true;
    // }
};

PlaybackBar playbackBar = PlaybackBar(15, 280, TFT_WIDTH-30, 5, 5, 40, COLOR_RGB565_WHITE);
SpotifyConn spotifyConn;
ESP8266WebServer server(80);
void webServerHandleRoot() {
  char webPage[1024];
  sprintf(webPage, loginPage, CLIENT, "192.168.1.15");
  Serial.println(webPage);
  server.send(200, "text/html", webPage);
}

void webServerHandleCallback() {
  if (server.arg("code") != "") {
    if (spotifyConn.getAuth(server.arg("code"))) {
      Serial.println("Successfully got access tokens!");

      SongInfo song = spotifyConn.makeRequest("/v1/me/player/currently-playing");

      if (song.err) {
        server.send(200, "text/html", "No bueno\r\n");
      }
      server.send(200, "text/html", song.name + "\r\n");
    
    } else {
      Serial.println("auth fail");
    }

  } else {
    Serial.println("No code arg.");
  }
}

int play = 0;
void setup() {
  Serial.begin(115200);
  screen.begin();
  screen.fillScreen(COLOR_RGB565_BLACK);
  server.on("/", webServerHandleRoot);
  server.on("/callback", webServerHandleCallback);
  server.begin();
  spotifyConn.connect(SSID, PASSPHRASE);
}

void loop(){
  server.handleClient();
  delay(50);
  screen.print(WiFi.localIP());
  screen.setCursor(0,0);
  playbackBar.draw();
  if (play++%50 < 30) {
    playbackBar.setPlayState(1);
    playbackBar.progress++;
    if (playbackBar.progress >= playbackBar.trackLength) {
      playbackBar.progress = 0;
      screen.fillScreen(COLOR_RGB565_BLACK);
    }
  } else {
    playbackBar.setPlayState(0);
  }
}
