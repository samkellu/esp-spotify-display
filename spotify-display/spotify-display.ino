#include "spotify-display.h"

DFRobot_ST7789_240x320_HW_SPI screen(TFT_DC, TFT_CS, TFT_RST);

class PlaybackBar {
  private:
    bool playing = 1;
    bool playStateFlag = 0;
    uint16_t color;
    int x, y;
    int width, height;
    float period;
    int amplitude;
    int prevBound = -1;
    uint32_t curTime = 0;
    int prevAmplitude;
    uint32_t lastDraw = 0;

  public:
    int amplitudePercent;
    int progress = 0;
    int duration = 1;

    PlaybackBar(int x, int y, int width, int height, int amplitude, float period, uint16_t color) {
      this->x         = x;
      this->y         = y;
      this->width     = width;
      this->height    = height;
      this->color     = color;
      this->amplitude = amplitude;
      this->period    = period;
    }

    void draw() {
      if (millis() - lastDraw < DRAW_RATE || (!playing && !playStateFlag)) {
        return;
      }

      lastDraw = millis();
      int curAmplitude = amplitude * (amplitudePercent / (float) 100);
      int bound = x + width * (progress / (float) duration);
      uint32_t prevTime = curTime;
      curTime++;

      // Stop playing
      if (!playStateFlag && playing) {

        for (int i = prevAmplitude; i > 0; i--) {
          for (int j = x; j < prevBound; j++) {
            screen.drawPixel(j, y + i * sin(period * (prevTime + j)), COLOR_RGB565_BLACK);
            screen.drawPixel(j, y + (i - 1) * sin(period * (prevTime + j)), color);
          }
          delay(100);
        }

        screen.drawFastVLine(prevBound, y-2*height, 4*height, COLOR_RGB565_BLACK);
        screen.drawFastVLine(bound, y-2*height, 4*height, color);
        playing = 0;

      // Start playing after pause
      } else if (playStateFlag && !playing) {

        screen.drawFastVLine(prevBound, y-2*height, 4*height, COLOR_RGB565_BLACK);
        screen.drawFastVLine(bound, y-2*height, 4*height, color);

        // Animate wave opening to full height when music starts playing again
        for (int i = 1; i <= curAmplitude; i++) {
          for (int j = x; j < bound; j++) {
            screen.drawPixel(j, y + (i - 1) * sin(period * (curTime + j)), COLOR_RGB565_BLACK);
            screen.drawPixel(j, y + i * sin(period * (curTime + j)), color);
          }
          delay(100);
        }
        playing = 1;
        prevBound = bound;
        return;

      } else {
        // Draw wave
        for (int i = x; i < bound; i++) {
          screen.drawPixel(i, y + prevAmplitude * sin(period * (prevTime + i)), COLOR_RGB565_BLACK);
          screen.drawPixel(i, y + curAmplitude * sin(period * (curTime + i)), color);
        }
        prevAmplitude = curAmplitude;
      }

      // Clear pixels between previous progress bar and current
      int sign = prevBound < bound ? 1 : -1;
      for (int i = prevBound; i != bound; i += sign) {
        screen.drawFastVLine(i, y-2*height, 4*height, COLOR_RGB565_BLACK);
      }
      screen.drawFastHLine(bound, y, width + x - bound, color);
      screen.drawFastVLine(bound, y-2*height, 4*height, color);
      prevBound = bound;
    }

    void setPlayState(bool state) {
      playStateFlag = state;
    }
};


class SpotifyConn {
  private:
    BearSSL::WiFiClientSecure client;
    String accessToken;
    String refreshToken;

  public:
    SongInfo song;
    bool accessTokenSet = false;
    int expiry;

    SpotifyConn() {
      client.setInsecure();
    }

    // Connects to the network specified in credentials.h
    void connect(const char* ssid, const char* passphrase) {
      #ifdef DEBUG
        Serial.print(F("Attempting connection to "));
        Serial.println(ssid);
      #endif

      WiFi.begin(ssid, passphrase);
      while ((WiFi.status() != WL_CONNECTED)) {
        delay(200);
      }

      #ifdef DEBUG
        Serial.print(F("Successfully connected to "));
        Serial.println(ssid);
      #endif
    }

    bool getAuth(bool refresh, String code) {
      const String host = F("accounts.spotify.com");
      const String url  = F("/api/token");
      const int port    = 443;

      if (!client.connect(host, port)) {
        #ifdef DEBUG
          Serial.println(F("Connection failed!"));
        #endif
        return false;
      }

      String auth = F("Basic ") + base64::encode(String(CLIENT) + F(":") + String(CLIENT_SECRET));
      String body;

      if (refresh) {
        body = F("grant_type=refresh_token&refresh_token=") + refreshToken;
      
      } else {
        body = F("grant_type=authorization_code&code=") + code + 
               F("&redirect_uri=http://") + WiFi.localIP().toString() + F("/callback");
      }

      String req  = F("POST ") + url + F(" HTTP/1.0\r\nHost: ") + 
                    host + F("\r\nContent-Length: ") + String(body.length()) +
                    F("\r\nContent-Type: application/x-www-form-urlencoded\r\n") +
                    F("Authorization: ") + auth + F("\r\nConnection: close\r\n\r\n") + 
                    body;

      client.print(req);
      client.readStringUntil('{');

      DynamicJsonDocument doc(1024);
      String json = F("{") + client.readStringUntil('\r');
      DeserializationError err = deserializeJson(doc, json);

      if (err) {
        #ifdef DEBUG
          Serial.print(F("Deserialisation failed for string: "));
          Serial.println(json);
          Serial.println(err.f_str());
        #endif
        return false;
      }

      // TODO check if these keys are present first
      accessToken = doc["access_token"].as<String>();
      refreshToken = doc["refresh_token"].as<String>();
      expiry = millis() + 1000 * doc["expires_in"].as<int>();

      accessTokenSet = true;
      return true;
    }

    bool getCurrentlyPlaying() {
      const String host = F("api.spotify.com");
      const String url  = F("/v1/me/player");
      const int port    = 443;

      if (!client.connect(host, port)) {
        #ifdef DEBUG
          Serial.println(F("Connection failed!"));
        #endif
        return false;
      }

      String auth = F("Bearer ") + accessToken;
      String req  = F("GET ") + url + F(" HTTP/1.0\r\nHost: ") +
                    host + F("\r\nAuthorization: ") +
                    auth + F("\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n"); 

      client.print(req);
      String ln = client.readStringUntil('\r');
      int start     = ln.indexOf(' ') + 1;
      int end       = ln.indexOf(' ', start);
      String status = ln.substring(start, end);

      if (strcmp(status.c_str(), "200") != 0) {
        #ifdef DEBUG
          Serial.println(F("An error occurred: HTTP ") + status);
        #endif
        return false;
      }

      if (!client.find("\r\n\r\n")) {
        #ifdef DEBUG
          Serial.println(F("Invalid response from server."));
        #endif
        return false;
      }

      DynamicJsonDocument doc(1024);
      StaticJsonDocument<300> filter;
      filter["progress_ms"] = true;
      filter["is_playing"]  = true;

      JsonObject filter_device            = filter.createNestedObject("device");
      JsonObject filter_item              = filter.createNestedObject("item");
      JsonObject filter_item_album        = filter_item.createNestedObject("album");
      JsonObject filter_item_album_images = filter_item_album["images"].createNestedObject();

      filter_device["volume_percent"]    = true;
      filter_device["name"]              = true;
      filter_item["name"]                = true;
      filter_item["duration_ms"]         = true;
      filter_item["artists"][0]["name"]  = true;
      filter_item["id"]                  = true;
      filter_item_album["name"]          = true;
      filter_item_album_images["url"]    = true;
      filter_item_album_images["width"]  = true;
      filter_item_album_images["height"] = true;

      DeserializationError err = deserializeJson(doc, client, DeserializationOption::Filter(filter));
      if (err) {
        #ifdef DEBUG
          Serial.print(F("Deserialisation failed"));
          Serial.println(err.f_str());
        #endif
        return false;
      }

      JsonObject device = doc["device"];
      JsonObject item   = doc["item"];
      JsonArray images  = item["album"]["images"];
      song.progressMs   = doc["progress_ms"].as<int>();
      song.isPlaying    = doc["is_playing"].as<bool>();
      song.volume       = device["volume_percent"].as<int>();
      song.deviceName   = device["name"].as<String>();
      song.id           = item["id"].as<String>();
      song.songName     = item["name"].as<String>();
      song.albumName    = item["album"]["name"].as<String>();
      song.artistName   = item["artists"][0]["name"].as<String>();
      song.durationMs   = item["duration_ms"].as<int>();

      for (int i = 0; i < images.size(); i++) {
        int height = images[i]["height"].as<int>();
        int width  = images[i]["width"].as<int>();

        if (height <= 300 && width <= 300) {
          song.height = height;
          song.width  = width;
          song.imgUrl = images[i]["url"].as<String>();
          break;
        }
      }

      return true;
    }

    bool getAlbumArt() {
      const String host = F("i.scdn.co");
      const String url  = song.imgUrl.substring(17);
      const int port    = 443;

      if (!client.connect(host, port)) {
        #ifdef DEBUG
          Serial.println(F("Connection failed!"));
        #endif
        return false;
      }

      String req = F("GET ") + url + F(" HTTP/1.0\r\nHost: ") +
                   host + F("\r\nCache-Control: no-cache\r\n");

      if (client.println(req) == 0) {
        #ifdef DEBUG
          Serial.println(F("Failed to send request..."));
        #endif
        return false;
      }

      String ln     = client.readStringUntil('\r');
      int start     = ln.indexOf(' ') + 1;
      int end       = ln.indexOf(' ', start);
      String status = ln.substring(start, end);

      if (strcmp(status.c_str(), "200") != 0) {
        #ifdef DEBUG
          Serial.println(F("An error occurred: HTTP ") + status);
        #endif
        client.stop();
        return false;
      }

      if (!client.find("Content-Length:")) {
        #ifdef DEBUG
          Serial.println(F("Response had not content-length header."));
        #endif
        return false;
      }

      int numBytes = client.parseInt();
      if (!client.find("\r\n\r\n")) {
        #ifdef DEBUG
          Serial.println(F("Invalid response from server."));
        #endif
        client.stop();
        return false;
      }

      File f = LittleFS.open(IMG_PATH, "w+");
      if (!f) {
        #ifdef DEBUG
          Serial.println(F("Failed to write image to file..."));
        #endif
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

      f.close();
      #ifdef DEBUG
        Serial.printf("Wrote to file %d/%d bytes\n", offset, numBytes);
      #endif
      client.stop();
      return true;
    }

    bool updateVolume() {
      const String host = F("api.spotify.com");
      const String url  = F("/v1/me/player/volume?volume_percent=") + String(song.volume);
      const int port    = 443;

      if (!client.connect(host, port)) {
        #ifdef DEBUG
          Serial.println(F("Connection failed!"));
        #endif
        return false;
      }

      String auth = F("Bearer ") + accessToken;
      String req  = F("PUT ") + url +
                    F(" HTTP/1.0\r\nHost: ") + host +
                    F("\r\nAuthorization: ") + auth +
                    F("\r\nContent-Length: 0") + 
                    F("\r\nConnection: close\r\n\r\n");

      if (client.println(req) == 0) {
        #ifdef DEBUG
          Serial.println(F("Failed to send request..."));
        #endif
        return false;
      }

      String ln     = client.readStringUntil('\r');
      int start     = ln.indexOf(' ') + 1;
      int end       = ln.indexOf(' ', start);
      String status = ln.substring(start, end);

      if (strcmp(status.c_str(), "204") != 0) {
        #ifdef DEBUG
          Serial.println(F("An error occurred: HTTP ") + status);
        #endif
        return false;
      }

      return true;
    }
};


bool drawBmp(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  // Stop drawing if out of bounds
  if (y >= TFT_HEIGHT) {
    return false;
  }

  screen.drawRGBBitmap(x, y, bitmap, w, h);
  return true;
}

PlaybackBar playbackBar = PlaybackBar(15, 310, TFT_WIDTH-30, 5, 8, 0.1, COLOR_RGB565_WHITE);
SpotifyConn spotifyConn;

#if defined(ESP8266)
  ESP8266WebServer server(80);
#elif defined(ESP32)
  WebServer server(80);
#endif

void webServerHandleRoot() {
  String header = F("https://accounts.spotify.com/authorize?client_id=") + String(CLIENT) +
                  F("&response_type=code&redirect_uri=http://") + WiFi.localIP().toString() +
                  F("/callback&scope=%20user-modify-playback-state%20user-read-currently-playing%20") +
                  F("user-read-playback-state");

  server.sendHeader("Location", header, true);
  server.send(302, "text/html", "");
}

void webServerHandleCallback() {
  if (server.arg("code") != "") {
    if (spotifyConn.getAuth(false, server.arg("code"))) {
      #ifdef DEBUG
        Serial.println(F("Successfully got access tokens!"));
      #endif
      server.send(200, "text/html", F("Login complete! you may close this tab.\r\n"));
    
    } else {
      server.send(200, "text/html", F("Authentication failed... Please try again :(\r\n"));
    }

  } else {
    #ifdef DEBUG
      Serial.println(F("An error occurred. Server provided no code arg."));
    #endif
  }
}

uint32_t lastRequest   = 0;
uint32_t lastPotRead   = 0;
uint32_t lastPotChange = 0;
bool imageIsSet        = false;

void setup() {
  #ifdef DEBUG
    Serial.begin(115200);
  #endif

  // Initialise LittleFS
  if (!LittleFS.begin()) {
    #ifdef DEBUG
      Serial.println(F("Failed to initialise file system."));
    #endif
    while (1) yield();
  }

  // Initialise tft display
  screen.begin();
  screen.fillScreen(COLOR_RGB565_BLACK);

  // Initialise webserver for spotify OAuth
  server.on("/", webServerHandleRoot);
  server.on("/callback", webServerHandleCallback);
  server.begin();

  TJpgDec.setCallback(drawBmp);
  TJpgDec.setJpgScale(2);
  spotifyConn.connect(SSID, PASSPHRASE);
}

void loop(){
  server.handleClient();
  yield();

  if (!spotifyConn.accessTokenSet) {
    screen.setCursor(0,0);
    screen.setTextSize(2);
    screen.print(F("Visit \nhttp://") + WiFi.localIP().toString() + F("\nto log in :)\n"));
    return;
  }

  if (millis() > spotifyConn.expiry) {
    spotifyConn.getAuth(true, "");
  }

  if (millis() - lastRequest > REQUEST_RATE || playbackBar.progress == playbackBar.duration) {
    String prevId = spotifyConn.song.id;
    lastRequest = millis();
    if (spotifyConn.getCurrentlyPlaying()) {
      
      #ifdef DEBUG
        Serial.println(F("Polled API"));
      #endif

      SongInfo song = spotifyConn.song;

      // Base off song id instead
      if (song.id != prevId) {

        // Clear album art and song/artist text
        screen.fillRect(0, 0, TFT_WIDTH, 300, COLOR_RGB565_BLACK);

        // Rewrite song/artist text
        screen.setCursor(10,240);
        screen.setTextSize(2);
        screen.setTextWrap(false);
        screen.println(song.songName);
        screen.setTextSize(1);
        screen.println(song.artistName);

        // Close playback bar wave when switching songs
        playbackBar.setPlayState(false);
        playbackBar.draw();
        imageIsSet = false;
      }

      playbackBar.duration = song.durationMs;
      playbackBar.progress = song.progressMs;
      playbackBar.setPlayState(song.isPlaying);
      // Draw progress indicator to correct location before image loads
      playbackBar.draw();

      // In the event of failure, continues fetching until success
      if (!imageIsSet) {
        // Get and draw album art
        yield();
        if (spotifyConn.getAlbumArt()) {
          TJpgDec.drawFsJpg((TFT_WIDTH - song.width/2) / 2, 40, IMG_PATH, LittleFS);
          imageIsSet = true;
        }
      }
    
    } else {
      #ifdef DEBUG
        Serial.println(F("Failed to fetch..."));
      #endif
    }

  } else {
    // Interpolate playback bar progress between api calls
    int interpolatedTime = (int) (millis() - lastRequest + spotifyConn.song.progressMs);
    playbackBar.progress = min(interpolatedTime, spotifyConn.song.durationMs);
  }

  // Read potentiometer value at fixed interval
  if (millis() - lastPotRead > POT_READ_RATE) {
    int newVol = 100 * (analogRead(POT) / (float) 1023);

    // Account for pot wobble
    if (abs(spotifyConn.song.volume - newVol) > 2) {
      lastPotChange = millis();
      spotifyConn.song.volume = newVol;
    }

    lastPotRead = millis();
  }

  // Only send api POST when pot hasnt changed for a while
  if (lastPotChange != 0 && millis() - lastPotChange > 3000) {
    lastPotChange = 0;
    spotifyConn.updateVolume();
  }

  // Slowly change amplitude of playback bar wave
  if (playbackBar.amplitudePercent != spotifyConn.song.volume) {
    int inc = playbackBar.amplitudePercent > spotifyConn.song.volume ? -6 : 6; 
    playbackBar.amplitudePercent += inc;
    playbackBar.amplitudePercent = min(max(0, playbackBar.amplitudePercent), 100);
  }

  playbackBar.draw();
}
