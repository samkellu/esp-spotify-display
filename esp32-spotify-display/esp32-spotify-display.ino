#include "spotify-display.h"

DFRobot_ST7789_240x320_HW_SPI screen(TFT_DC, TFT_CS, TFT_RST);
PlaybackBar playbackBar = PlaybackBar(15, 310, TFT_WIDTH-30, 5, 8, 0.1, 50);
WebServer server(80);

AsyncHTTPSRequest httpsAuth;
AsyncHTTPSRequest httpsCurrent;
AsyncHTTPSRequest httpsImg;
String accessToken;
String refreshToken;
SongInfo song;
int expiry;

// Control flags
bool accessTokenSet = false;
bool readFlag       = false;
bool newSong        = false;
bool imageRequested = false;
bool imageDrawFlag  = false;

// Connects to the network specified in credentials.h
void connect(const char* ssid, const char* passphrase) {
  #ifdef DEBUG
    Serial.printf("Attempting connection to %s\n", ssid);
  #endif

  // Wait for connection
  WiFi.begin(ssid, passphrase);
  while ((WiFi.status() != WL_CONNECTED)) delay(200);

  #ifdef DEBUG
    Serial.print("Successfully connected to ");
    Serial.println(ssid);
  #endif
}

void authCB(void* optParam, AsyncHTTPSRequest* request, int readyState) {
  // Fail if client isnt finished reading or response failed
  if (readyState != readyStateDone) return;
  if (request->responseHTTPcode() != 200) return;

  DynamicJsonDocument doc(1024);
  String json = request->responseText();
  DeserializationError err = deserializeJson(doc, json);

  if (err) {
    #ifdef DEBUG
      Serial.print("Deserialisation failed for string: ");
      Serial.println(json);
      Serial.println(err.f_str());
    #endif
    return;
  }

  accessToken = doc["access_token"].as<String>();
  refreshToken = doc["refresh_token"].as<String>();
  expiry = millis() + 1000 * doc["expires_in"].as<int>();
  accessTokenSet = true;
}

bool getAuth(bool refresh, String code) {
  // Fail if client is busy
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
  // Fail if client is not done or response failed
  if (readyState != readyStateDone) return;
  if (request->responseHTTPcode() != 200) return;

  String json = request->responseText();
  DynamicJsonDocument doc(1024);
  StaticJsonDocument<300> filter;

  JsonObject filter_device            = filter.createNestedObject("device");
  JsonObject filter_item              = filter.createNestedObject("item");
  JsonObject filter_item_album        = filter_item.createNestedObject("album");
  JsonObject filter_item_album_images = filter_item_album["images"].createNestedObject();

  filter["progress_ms"]               = true;
  filter["is_playing"]                = true;
  filter_device["volume_percent"]     = true;
  filter_device["name"]               = true;
  filter_item["name"]                 = true;
  filter_item["duration_ms"]          = true;
  filter_item["artists"][0]["name"]   = true;
  filter_item["id"]                   = true;
  filter_item_album["name"]           = true;
  filter_item_album_images["url"]     = true;
  filter_item_album_images["width"]   = true;
  filter_item_album_images["height"]  = true;

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

    // Only grab appropriate sized image
    if (height <= 300 && width <= 300) {
      song.height = height;
      song.width  = width;
      song.imgUrl = images[i]["url"].as<String>();
      break;
    }
  }

  readFlag = true;
  newSong = song.id != prevId; 
}

bool getCurrentlyPlaying() {
  // Fail if client is busy
  if (httpsCurrent.readyState() != readyStateUnsent && httpsCurrent.readyState() != readyStateDone) return false;

  if (httpsCurrent.open("GET", "https://api.spotify.com/v1/me/player")) {
    String auth = "Bearer " + accessToken;
    httpsCurrent.setReqHeader("Cache-Control", "no-cache");
    httpsCurrent.setReqHeader("Authorization", auth.c_str());
    httpsCurrent.onReadyStateChange(currentlyPlayingCB);
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
  // Fail if client hasnt finished reading or response failed
  if (readyState != readyStateDone) return;
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

  // Write image to file
  // TODO - figure out buffered writing and reading from client
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
  // Fail if client isnt ready
  if (httpsImg.readyState() != readyStateUnsent && httpsImg.readyState() != readyStateDone) return false;

  if (httpsImg.open("GET", song.imgUrl.c_str())) {
    httpsImg.onReadyStateChange(albumArtCB);
    httpsImg.setReqHeader("Cache-Control", "no-cache");
    httpsImg.send();
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

void webServerHandleRoot() {
  String header = "https://accounts.spotify.com/authorize?client_id=" + String(CLIENT) +
                  "&response_type=code&redirect_uri=http://" + WiFi.localIP().toString() +
                  "/callback&scope=%20user-modify-playback-state%20user-read-currently-playing%20" +
                  "user-read-playback-state";
  
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

// Control timers
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

  // Initialise wifi
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
      imageRequested = getAlbumArt();
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
