#include "DFRobot_GDL.h"
#include "credentials.h"
#include "webpage.h"
#include <ESP8266WiFi.h> 
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <TJpg_Decoder.h>

#include <ArduinoJson.h>
#include <base64.h>
#include "LittleFS.h"

#define POT          A0
#define TFT_CS       5  // D6
#define TFT_RST      2  // D5
#define TFT_DC       15 // D4
#define TFT_WIDTH    240
#define TFT_HEIGHT   320
#define REQUEST_RATE 20000 // ms
#define IMG_PATH     "/img.jpg"

DFRobot_ST7789_240x320_HW_SPI screen(TFT_DC, TFT_CS, TFT_RST);

class PlaybackBar {
  private:
    bool playing = 0;
    bool playStateFlag = 0;
    uint16_t time = 0;
    uint16_t color;
    int x, y;
    int width, height;
    int amplitude;
    int* wave;
    int* closingWave;
    int numSamples;
    int prevProgressX = -1;
    uint32_t prevTime = 0;

  public:
    int progress = 70;
    int duration = 100;

    PlaybackBar(int x, int y, int width, int height, int amplitude, int numSamples, uint16_t color) {
      this->x           = x;
      this->y           = y;
      this->width       = width;
      this->height      = height;
      this->color       = color;
      this->amplitude   = amplitude;
      this->numSamples  = numSamples;
      this->wave        = (int*) malloc(sizeof(int) * numSamples);
      this->closingWave = (int*) malloc(sizeof(int) * numSamples);

      int idx = 0;
      for (float i = 0; i < 2*PI && idx < numSamples; i += 2*PI / (float)numSamples) {
        this->wave[idx] = round(sin(i) * amplitude);
        this->closingWave[idx] = this->wave[idx++]/2;
      }
    }

    void draw() {
      if (!playing && !playStateFlag) {
        return;
      }

      uint32_t curTime = millis() / 100;
      int bound = x + width * (progress/(float)duration);
      // Stop playing
      if (!playStateFlag && playing) {

        // Animate wave closing back to straight line when music is paused
        for (int i = x; i < bound; i++) {
          screen.drawPixel(i, y + wave[(curTime + i)%numSamples], COLOR_RGB565_BLACK);
          screen.drawPixel(i, y + closingWave[(curTime + i)%numSamples], color);
        }
        delay(30);
        for (int i = x; i < bound; i++) {
          screen.drawPixel(i, y + closingWave[(curTime + i)%numSamples], COLOR_RGB565_BLACK);
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
          screen.drawPixel(i, y + closingWave[(curTime + i)%numSamples], color);
        }
        delay(40);
        for (int i = x; i < bound; i++) {
          screen.drawPixel(i, y + closingWave[(curTime + i)%numSamples], COLOR_RGB565_BLACK);
          screen.drawPixel(i, y + wave[(curTime + i)%numSamples], color);
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
        screen.drawPixel(i, y + wave[(prevTime + i)%numSamples], COLOR_RGB565_BLACK);
        screen.drawPixel(i, y + wave[(curTime + i)%numSamples], color);
      }
      prevTime = curTime;
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

    int getContentLength() {
      if (!client.find("Content-Length:")) {
        return -1;
      }

      int contentLength = client.parseInt();
      return contentLength;
    }

  public:
    SongInfo song;
    bool accessTokenSet;

    SpotifyConn() {
      client.setInsecure();
      accessTokenSet = false;
      song.imgAlloc = false;
    }

    // Connects to the network specified in credentials.h
    void connect(const char* ssid, const char* passphrase) {
      Serial.print(F("Attempting connection to "));
      Serial.println(ssid);
      WiFi.begin(ssid, passphrase);
      while ((WiFi.status() != WL_CONNECTED)) {
        delay(200);
      }

      Serial.print(F("Successfully connected to "));
      Serial.println(ssid);
    }

    bool getAuth(String code) {

      const char* host = "accounts.spotify.com";
      const int   port = 443;
      String      url  = "/api/token";

      if (!client.connect(host, port)) {
        Serial.println(F("Connection failed!"));
        return false;
      }

      // Reset WDT
      yield();

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
        Serial.print(F("Deserialisation failed for string: "));
        Serial.println(json);
        Serial.println(err.f_str());
        return false;
      }

      // TODO check if these keys are present first
      accessToken = doc["access_token"].as<String>();
      refreshToken = doc["refresh_token"].as<String>();
      expiry = doc["expires_in"];

      accessTokenSet = true;
      return true;
    }

    bool getCurrentlyPlaying() {

      if (!accessTokenSet) {
        return false;
      }

      const String url  = "/v1/me/player/currently-playing";
      const char*  host = "api.spotify.com";
      const int    port = 443;

      if (!client.connect(host, port)) {
        Serial.println(F("Connection failed!"));
        return false;
      }

      // Reset WDT
      yield();

      String auth = F("Bearer ") + accessToken;
      String req  = F("GET ") + url + F(" HTTP/1.0\r\nHost: ") +
                    host + F("\r\nAuthorization: ") +
                    auth + F("\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n"); 

      client.print(req);
      String ln = client.readStringUntil('\r');

      int start = ln.indexOf(' ');
      int end = ln.indexOf(' ', start + 1);
      String status = ln.substring(start, end);

      if (!strcmp(status.c_str(), "200")) {
        Serial.print(F("An error occurred: HTTP "));
        Serial.println(status);
        return false;
      }

      if (!client.find("\r\n\r\n")) {
        Serial.println(F("Invalid response from server."));
        return false;
      }

      // String json = client.readStringUntil('\r');
      // Serial.println(json.length());
      DynamicJsonDocument doc(1024);
      StaticJsonDocument<256> filter;

      filter["progress_ms"] = true;
      JsonObject filter_item = filter.createNestedObject("item");
      filter_item["name"] = true;
      filter_item["duration_ms"] = true;
      filter_item["artists"][0]["name"] = true;
      JsonObject filter_item_album = filter_item.createNestedObject("album");
      filter_item_album["name"] = true;
      JsonObject filter_item_album_images = filter_item_album["images"].createNestedObject();
      filter_item_album_images["url"] = true;
      filter_item_album_images["width"] = true;
      filter_item_album_images["height"] = true;
      DeserializationError err = deserializeJson(doc, client, DeserializationOption::Filter(filter));

      if (err) {
        Serial.print(F("Deserialisation failed"));
        // Serial.println(json);
        Serial.println(err.f_str());
        return false;
      }

      // #define DEBUG
      // #ifdef DEBUG
      //   serializeJsonPretty(doc, Serial);
      // #endif

      song.progressMs = doc["progress_ms"].as<int>();
      JsonObject item = doc["item"];
      song.songName = item["name"].as<String>();
      song.albumName = item["album"]["name"].as<String>();
      song.artistName = item["artists"][0]["name"].as<String>();
      song.durationMs = item["duration_ms"].as<int>();

      JsonArray images = item["album"]["images"];

      for (int i = 0; i < images.size(); i++) {
        int height = images[i]["height"].as<int>();
        int width = images[i]["width"].as<int>();

        if (height <= 300 && width <= 300) {
          song.height = height;
          song.width = width;
          song.imgUrl = images[i]["url"].as<String>();
          break;
        }
      }

      return true;
    }

    bool getAlbumArt() {
      const char*  host = "i.scdn.co";
      const int    port = 443;
      const String url = song.imgUrl.substring(17);

      if (!client.connect(host, port)) {
        Serial.println(F("Connection failed!"));
        return false;
      }

      // Reset WDT
      yield();

      String req  = F("GET ") + url + F(" HTTP/1.0\r\nHost: ") +
                    host + F("\r\nCache-Control: no-cache\r\n");

      if (client.println(req) == 0) {
        Serial.println("Failed to send request...");
        return false;
      }

      String ln = client.readStringUntil('\r');
      int start = ln.indexOf(' ');
      int end = ln.indexOf(' ', start + 1);
      String status = ln.substring(start, end);

      if (!strcmp(status.c_str(), "200")) {
        Serial.print(F("An error occurred: HTTP "));
        Serial.println(status);
        client.flush();
        client.stop();
        return false;
      }

      int numBytes = getContentLength();
      Serial.println(numBytes);
      if (!client.find("\r\n\r\n")) {
        Serial.println(F("Invalid response from server."));
        client.flush();
        client.stop();
        return false;
      }

      File f = LittleFS.open(IMG_PATH, "w+");
      if (!f) {
        Serial.println("Failed to write image to file...");
        return false;
      }

      int offset = 0;
      uint8_t buf[128];
      while (offset < numBytes) {

        size_t available = client.available();

        if (available) {
          int bytes = client.readBytes(buf, min(available, sizeof(buf)));
          f.write(buf, bytes);
          offset += bytes;
        }

        // Reset WDT
        yield();
      }

      Serial.println(offset);
      Serial.println(numBytes);
      f.close();
      Serial.println(F("Wrote to file."));
      song.imgSize = (size_t) numBytes;
      client.flush();
      client.stop();
      return true;
    }
};

bool drawBmp(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {

  if (y >= TFT_HEIGHT) {
    return false;
  }

  screen.drawRGBBitmap(x, y, bitmap, w, h);
  return true;
}

PlaybackBar playbackBar = PlaybackBar(15, 280, TFT_WIDTH-30, 5, 6, 40, COLOR_RGB565_WHITE);
SpotifyConn spotifyConn;
ESP8266WebServer server(80);

void webServerHandleRoot() {
  char webPage[1024];
  sprintf(webPage, loginPage, CLIENT, "192.168.1.15");
  server.send(200, "text/html", webPage);
}

void webServerHandleCallback() {
  if (server.arg("code") != "") {
    if (spotifyConn.getAuth(server.arg("code"))) {
      Serial.println(F("Successfully got access tokens!"));
      server.send(200, "text/html", "Login complete! you may close this tab.\r\n");
    
    } else {
      server.send(200, "text/html", "Authentication failed... Please try again :(\r\n");
    }

  } else {
    Serial.println(F("An error occurred. Server provided no code arg."));
  }
}

int play = 0;
uint32_t lastRequest = 0;
bool imageIsSet = false;
void setup() {

  Serial.begin(115200);
  if (!LittleFS.begin()) {
    Serial.println("Failed to initialise file system.");
    while (1) yield();
  }

  screen.begin();
  screen.fillScreen(COLOR_RGB565_BLACK);
  screen.setTextSize(3);
  server.on("/", webServerHandleRoot);
  server.on("/callback", webServerHandleCallback);
  server.begin();
  spotifyConn.connect(SSID, PASSPHRASE);
}

void loop(){
  server.handleClient();
  yield();

  if (millis() - lastRequest > REQUEST_RATE || playbackBar.progress == playbackBar.duration) {
    if (spotifyConn.accessTokenSet) {
      if (spotifyConn.getCurrentlyPlaying()) {
        
        yield();

        SongInfo song = spotifyConn.song;
        Serial.println(F("Requested"));
        int prevProgress = playbackBar.progress;
        int prevDuration = playbackBar.duration;
        playbackBar.progress = song.progressMs;
        playbackBar.duration = song.durationMs;
        playbackBar.setPlayState(PLAYING);

        // Base off song id instead
        if (playbackBar.progress < prevProgress || playbackBar.duration != prevDuration) {

          screen.fillScreen(COLOR_RGB565_BLACK);
          screen.setCursor(0,0);
          screen.println(song.songName);
          screen.println(song.artistName);
          screen.println(song.albumName);

          imageIsSet = false;
        }

        if (!imageIsSet) {
          // get image
          if (spotifyConn.getAlbumArt()) {

            yield();
            Serial.println(F("Attempting print..."));
            TJpgDec.setJpgScale(2);
            TJpgDec.setCallback(drawBmp);
            imageIsSet = true;
            if (LittleFS.exists(IMG_PATH)) {
              Serial.println("ITS HERE!");
            }
            yield();

            TJpgDec.drawFsJpg(45, 100, IMG_PATH, LittleFS);
            Serial.println(F("Drawd"));
          }
        }
      
      } else {
        Serial.println(F("Failed to fetch..."));
      }

    } else {
      screen.setCursor(0,0);
      screen.println(WiFi.localIP());
    }
    lastRequest = millis();

  } else {
    int interpolatedTime = (int) (millis() - lastRequest + spotifyConn.song.progressMs);
    playbackBar.progress = min(interpolatedTime, spotifyConn.song.durationMs);
  }

  yield();
  playbackBar.draw();
}
