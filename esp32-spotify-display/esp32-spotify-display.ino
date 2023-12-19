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


AsyncHTTPSRequest httpsAuth;
AsyncHTTPSRequest httpsCurrent;
AsyncHTTPSRequest httpsImg;
String accessToken;
String refreshToken;
SongInfo song;
bool accessTokenSet = false;
int expiry;
bool readFlag = false;
bool newSong = false;
bool imageRequested = false;
bool imageDrawFlag = false;

// Connects to the network specified in credentials.h
void connect(const char* ssid, const char* passphrase) {
  #ifdef DEBUG
    Serial.print("Attempting connection to ");
    Serial.println(ssid);
  #endif

  WiFi.begin(ssid, passphrase);
  while ((WiFi.status() != WL_CONNECTED)) {
    delay(200);
  }

  #ifdef DEBUG
    Serial.print("Successfully connected to ");
    Serial.println(ssid);
  #endif
}

void authCB(void* optParam, AsyncHTTPSRequest* request, int readyState) {

  if (readyState != readyStateDone) return;
  Serial.println(request->responseHTTPcode());
  if (request->responseHTTPcode() != 200) return;

  DynamicJsonDocument doc(1024);
  String json = request->responseText();
  Serial.println(json);
  DeserializationError err = deserializeJson(doc, json);

  if (err) {
    #ifdef DEBUG
      Serial.print("Deserialisation failed for string: ");
      Serial.println(json);
      Serial.println(err.f_str());
    #endif
    return;
  }

  // TODO check if these keys are present first
  accessToken = doc["access_token"].as<String>();
  refreshToken = doc["refresh_token"].as<String>();
  expiry = millis() + 1000 * doc["expires_in"].as<int>();

  Serial.println(accessToken);
  accessTokenSet = true;
}

bool getAuth(bool refresh, String code) {

  if (httpsAuth.readyState() != readyStateUnsent && httpsAuth.readyState() != readyStateDone) return false;


  if (httpsAuth.open("POST", "https://accounts.spotify.com/api/token")) {
    String body;

    if (refresh) {
      body = "grant_type=refresh_token&refresh_token=" + refreshToken;
    
    } else {
      body = "grant_type=authorization_code&code=" + code + 
              "&redirect_uri=http://" + WiFi.localIP().toString() + "/callback";
    }

    String auth = "Basic " + base64::encode(String(CLIENT) + ":" + String(CLIENT_SECRET));
    httpsAuth.setReqHeader("Content-Type", "application/x-www-form-urlencoded");
    httpsAuth.setReqHeader("Authorization", auth.c_str());
    httpsAuth.onReadyStateChange(authCB);
    httpsAuth.send(body);
    return true;

  } else {

    #ifdef DEBUG
      Serial.println("Connection failed!");
    #endif
    return false;
  }
}

void currentlyPlayingCB(void* optParam, AsyncHTTPSRequest* request, int readyState) {

  if (readyState != readyStateDone) return;
  if (request->responseHTTPcode() != 200) return;

  String json = request->responseText();

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

  DeserializationError err = deserializeJson(doc, json, DeserializationOption::Filter(filter));
  if (err) {
    #ifdef DEBUG
      Serial.print(F("Deserialisation failed"));
      Serial.println(err.f_str());
    #endif
    return;
  }

  String prevId     = song.id;
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

  yield();
  readFlag = true;
  newSong = song.id != prevId; 
}

bool getCurrentlyPlaying() {

  if (httpsCurrent.readyState() != readyStateUnsent && httpsCurrent.readyState() != readyStateDone) return false;
  if (httpsCurrent.open("GET", "https://api.spotify.com/v1/me/player")) {

    String auth = "Bearer " + accessToken;
    httpsCurrent.setReqHeader("Cache-Control", "no-cache");
    httpsCurrent.setReqHeader("Authorization", auth.c_str());
    httpsCurrent.onReadyStateChange(currentlyPlayingCB);
    Serial.println("Real");
    httpsCurrent.send();
    return true;

  } else {

    #ifdef DEBUG
      Serial.println("Connection failed!");
    #endif
    return false;
  }
}

void albumArtCB(void* optParam, AsyncHTTPSRequest* request, int readyState) {

  if (readyState != readyStateDone) return;
  Serial.println("res");  
  Serial.println(request->responseHTTPcode());
  if (request->responseHTTPcode() != 200) {
    imageRequested = false;
    #ifdef DEBUG
      Serial.println("An error occurred: HTTP " + request->responseHTTPcode());
    #endif
    return;
  }

  File f = LittleFS.open(IMG_PATH, "w+");
  if (!f) {
    #ifdef DEBUG
      Serial.println("Failed to write image to file...");
    #endif
    return;
  }

  char* img = request->responseLongText();
  f.write((uint8_t*) img, strlen(img));
  f.close();
  #ifdef DEBUG
    Serial.println("Wrote image to file");
  #endif
  imageDrawFlag = true;
  return;
}


bool getAlbumArt() {

  if (httpsImg.readyState() != readyStateUnsent && httpsImg.readyState() != readyStateDone) return false;
  Serial.println(song.imgUrl.c_str());
  if (httpsImg.open("GET", song.imgUrl.c_str())) {
    Serial.println("Requestod");
    httpsImg.onReadyStateChange(albumArtCB);
    httpsImg.setReqHeader("Cache-Control", "no-cache");
    httpsImg.send();
    imageRequested = true;
    return true;

  } else {
    #ifdef DEBUG
      Serial.println("Connection failed!");
    #endif
    return false;
  }
}


//     bool updateVolume() {
//       const String host = F("api.spotify.com");
//       const String url  = F("/v1/me/player/volume?volume_percent=") + String(song.volume);
//       const int port    = 443;

//       if (!client.connect(host, port)) {
//         #ifdef DEBUG
//           Serial.println(F("Connection failed!"));
//         #endif
//         return false;
//       }

//       String auth = F("Bearer ") + accessToken;
//       String req  = F("PUT ") + url +
//                     F(" HTTP/1.0\r\nHost: ") + host +
//                     F("\r\nAuthorization: ") + auth +
//                     F("\r\nContent-Length: 0") + 
//                     F("\r\nConnection: close\r\n\r\n");

//       if (client.println(req) == 0) {
//         #ifdef DEBUG
//           Serial.println(F("Failed to send request..."));
//         #endif
//         return false;
//       }

//       String ln     = client.readStringUntil('\r');
//       int start     = ln.indexOf(' ') + 1;
//       int end       = ln.indexOf(' ', start);
//       String status = ln.substring(start, end);

//       if (strcmp(status.c_str(), "204") != 0) {
//         #ifdef DEBUG
//           Serial.println(F("An error occurred: HTTP ") + status);
//         #endif
//         return false;
//       }

//       return true;
//     }
// ;


bool drawBmp(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  // Stop drawing if out of bounds
  if (y >= TFT_HEIGHT) {
    return false;
  }

  screen.drawRGBBitmap(x, y, bitmap, w, h);
  return true;
}

PlaybackBar playbackBar = PlaybackBar(15, 310, TFT_WIDTH-30, 5, 8, 0.1, COLOR_RGB565_WHITE);

#if defined(ESP8266)
  ESP8266WebServer server(80);
#elif defined(ESP32)
  WebServer server(80);
#endif

void webServerHandleRoot() {
  String header = "https://accounts.spotify.com/authorize?client_id=" + String(CLIENT) +
                  "&response_type=code&redirect_uri=http://" + WiFi.localIP().toString() +
                  "/callback&scope=%20user-modify-playback-state%20user-read-currently-playing%20" +
                  "user-read-playback-state";
  
  Serial.println(header);

  server.sendHeader("Location", header, true);
  server.send(302, "text/html", "");
}

void webServerHandleCallback() {
  if (server.arg("code") != "") {
    if (getAuth(false, server.arg("code"))) {
      #ifdef DEBUG
        Serial.println("Successfully got access tokens!");
      #endif
      server.send(200, "text/html", "Login complete! you may close this tab.\r\n");
    
    } else {
      server.send(200, "text/html", "Authentication failed... Please try again :(\r\n");
    }

  } else {
    #ifdef DEBUG
      Serial.println("An error occurred. Server provided no code arg.");
    #endif
  }
}

uint32_t lastRequest   = 0;
uint32_t lastResponse  = 0;
uint32_t lastPotRead   = 0;
uint32_t lastPotChange = 0;

void setup() {
  #ifdef DEBUG
    Serial.begin(115200);
  #endif

  // Initialise LittleFS
  if (!LittleFS.begin()) {
    #ifdef DEBUG
      Serial.println("Failed to initialise file system.");
    #endif
    return;
  }

  // Initialise tft display
  screen.begin();
  screen.fillScreen(COLOR_RGB565_BLACK);

  connect(SSID, PASSPHRASE);
  Serial.println(WiFi.localIP());
  // Initialise webserver for spotify OAuth
  server.on("/", webServerHandleRoot);
  server.on("/callback", webServerHandleCallback);
  server.begin();

  TJpgDec.setCallback(drawBmp);
  TJpgDec.setJpgScale(2);
}

void loop(){
  server.handleClient();
  yield();

  if (!accessTokenSet) {
    screen.setCursor(0,0);
    screen.setTextSize(2);
    screen.print("Visit \nhttp://" + WiFi.localIP().toString() + "\nto log in :)\n");
    return;
  }

  if (millis() > expiry) {
    getAuth(true, "");
  }

  if (millis() - lastRequest > REQUEST_RATE || playbackBar.progress == playbackBar.duration) {
    lastRequest = millis();
    Serial.println("polled");
    getCurrentlyPlaying();
  }

  if (readFlag) {
    lastResponse = millis();
    
    readFlag = false;
    if (newSong) {

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
      imageDrawFlag = false;
      imageRequested = false;
      newSong = false;
    }

    // In the event of failure, continues fetching until success
    if (!imageRequested) {
      getAlbumArt();
    }

    playbackBar.duration = song.durationMs;
    playbackBar.progress = song.progressMs;
    playbackBar.setPlayState(song.isPlaying);

  } else {
    // Interpolate playback bar progress between api calls
    int interpolatedTime = (int) (millis() - lastResponse + song.progressMs);
    playbackBar.progress = min(interpolatedTime, song.durationMs);
  }


  // // Read potentiometer value at fixed interval
  // if (millis() - lastPotRead > POT_READ_RATE) {
  //   int newVol = 100 * (analogRead(POT) / (float) 1023);

  //   // Account for pot wobble
  //   if (abs(song.volume - newVol) > 2) {
  //     lastPotChange = millis();
  //     song.volume = newVol;
  //   }

  //   lastPotRead = millis();
  // }

  // // Only send api POST when pot hasnt changed for a while
  // if (lastPotChange != 0 && millis() - lastPotChange > 3000) {
  //   lastPotChange = 0;
  //   updateVolume();
  // }

  // Slowly change amplitude of playback bar wave
  if (playbackBar.amplitudePercent != song.volume) {
    int inc = playbackBar.amplitudePercent > song.volume ? -6 : 6; 
    playbackBar.amplitudePercent += inc;
    playbackBar.amplitudePercent = min(max(0, playbackBar.amplitudePercent), 100);
  }

  playbackBar.draw();

  if (imageDrawFlag) {
    TJpgDec.drawFsJpg((TFT_WIDTH - song.width/2) / 2, 40, IMG_PATH, LittleFS);
    imageDrawFlag = false;
  }
}
