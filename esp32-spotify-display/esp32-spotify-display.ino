#include "spotify-display.h"

DFRobot_ST7789_240x320_HW_SPI screen(TFT_DC, TFT_CS, TFT_RST);
PlaybackBar playbackBar = PlaybackBar(15, 310, TFT_WIDTH-30, 5, 8, 0.1, 50);
WebServer server(80);
WiFiClientSecure client;

// TODO - when have access to hardware again, move to respective functions
//        or see if can use just one
AsyncHTTPSRequest httpsAuth;
AsyncHTTPSRequest httpsCurrent;
AsyncHTTPSRequest httpsImg;
AsyncHTTPSRequest httpsVolume;
SongInfo song;
AuthInfo auth;

// Control flags
// Forces a check on the saved refresh token in flash
bool accessTokenSet = false;
bool triedFileToken = false;
bool readFlag       = false;
bool newSong        = false;
bool imageSet       = false;
bool textSet        = false;
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

// ------------------------------- GET/REFRESH ACCESS TOKENS -------------------------------

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

  auth.accessToken = doc["access_token"].as<String>();
  auth.refreshToken = doc["refresh_token"].as<String>();
  auth.expiry = millis() + 1000 * doc["expires_in"].as<int>();
  accessTokenSet = true;
  #ifdef DEBUG
    Serial.println("Successfully got access tokens!");
  #endif

  // Try to write refresh token to file
  File f = LittleFS.open(TOKEN_PATH, "w");
  if (!f) {
    #ifdef DEBUG
      Serial.println("Failed to write token to file...");
    #endif
    return;
  }

  f.print(auth.refreshToken);
  f.close();
}

bool getAuth(bool refresh, bool fromFile, String code) {

  // Attempt to automatically get access token from saved refresh token
  if (refresh && fromFile) {
    File f = LittleFS.open(TOKEN_PATH, "r");
    if (f) {
      auth.refreshToken = f.readString();
      if (auth.refreshToken.length() == 0) {
        refresh = false;
      }
      Serial.println(auth.refreshToken);
      f.close();
    }
  }

  // Fail if client is busy
  if (httpsAuth.readyState() != readyStateUnsent && httpsAuth.readyState() != readyStateDone) return false;

  if (httpsAuth.open("POST", "https://accounts.spotify.com/api/token")) {
    String body;
    if (refresh) {
      body = "grant_type=refresh_token&refresh_token=" + auth.refreshToken;
    
    } else {
      body = "grant_type=authorization_code&code=" + code + 
             "&redirect_uri=http://" + WiFi.localIP().toString() + "/callback";
    }

    String authStr = "Basic " + base64::encode(String(CLIENT) + ":" + String(CLIENT_SECRET));
    httpsAuth.setReqHeader("Content-Type", "application/x-www-form-urlencoded");
    httpsAuth.setReqHeader("Authorization", authStr.c_str());
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

// ------------------------------- GET CURRENTLY PLAYING -------------------------------

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
    String authStr = "Bearer " + auth.accessToken;
    httpsCurrent.setReqHeader("Cache-Control", "no-cache");
    httpsCurrent.setReqHeader("Authorization", authStr.c_str());
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

// ------------------------------- GET ALBUM ART -------------------------------

// Synchronous due to large file size limitations
bool getAlbumArt() {
  const char* host  = "i.scdn.co";
  const String url  = song.imgUrl.substring(17);
  uint16_t port     = 443;

  if (!client.connect(host, port)) {
    #ifdef DEBUG
      Serial.println("Connection failed!");
    #endif
    return false;
  }

  String req = "GET " + url + " HTTP/1.0\r\nHost: " +
                host + "\r\nCache-Control: no-cache\r\n";

  if (client.println(req) == 0) {
    #ifdef DEBUG
      Serial.println("Failed to send request...");
    #endif
    return false;
  }

  String ln     = client.readStringUntil('\r');
  int start     = ln.indexOf(' ') + 1;
  int end       = ln.indexOf(' ', start);
  String status = ln.substring(start, end);

  if (strcmp(status.c_str(), "200") != 0) {
    #ifdef DEBUG
      Serial.println("An error occurred: HTTP " + status);
      Serial.println(url);
    #endif
    client.stop();
    return false;
  }

  if (!client.find("Content-Length:")) {
    #ifdef DEBUG
      Serial.println("Response had not content-length header.");
    #endif
    return false;
  }

  int numBytes = client.parseInt();
  if (!client.find("\r\n\r\n")) {
    #ifdef DEBUG
      Serial.println("Invalid response from server.");
    #endif
    client.stop();
    return false;
  }

  File f = LittleFS.open(IMG_PATH, "w+");
  if (!f) {
    #ifdef DEBUG
      Serial.println("Failed to write image to file...");
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

// ------------------------------- VOLUME CONTROL -------------------------------

void volumeSetCB(void* optParam, AsyncHTTPSRequest* request, int readyState) {
  // Fail if client hasnt finished reading or response failed
  if (readyState != readyStateDone) return;
  if (request->responseHTTPcode() != 200) {
    #ifdef DEBUG
      Serial.println("An error occurred: HTTP " + request->responseHTTPcode());
    #endif
    return;
  }
}

bool updateVolume() {
  // Fail if client isnt ready
  if (httpsVolume.readyState() != readyStateUnsent && httpsVolume.readyState() != readyStateDone) return false;

  char* format = "https://api.spotify.com/v1/me/player/volume?volume_percent=%d";
  char buf[128];
  sprintf(buf, format, song.volume);

  if (httpsVolume.open("PUT", buf)) {
    sprintf(buf, "Bearer %s", auth.accessToken);
    httpsVolume.setReqHeader("Authorization", buf);
    httpsVolume.setReqHeader("Cache-Control", "no-cache");
    httpsVolume.setReqHeader("Content-Length", "0");
    httpsVolume.send();
    return true;

  } else {
    #ifdef DEBUG
      Serial.println("Connection failed!");
    #endif
    return false;
  }

  return true;
}

// ------------------------------- TJPG -------------------------------

uint16_t* albumBmp = NULL;

void drawBmp(int imgX, int imgY, int imgW, int imgH) {
  // Take average color components of the first few rows of the image
  uint16_t r = 0, g = 0, b = 0, rr = 0, rg = 0, rb = 0;
  for (int i = 0; i < imgW * 5; i++) {

    // Ensure average isnt black skewed too heavily
    rr = ((albumBmp[i] >> 11) & 0x1F);
    rg = ((albumBmp[i] >> 5) & 0x3F);
    rb = (albumBmp[i] & 0x1F);
    rr = rr == 0 ? r : rr;
    rg = rg == 0 ? g : rg;
    rb = rb == 0 ? b : rb;

    r = (r * i + rr) / (i + 1);
    g = (g * i + rg) / (i + 1);
    b = (b * i + rb) / (i + 1);
  }

  Serial.printf("r %uh\n", r);
  Serial.printf("g %uh\n", g);
  Serial.printf("b %uh\n", b);
  for (int y = 0; y < 300; y++) {
    for (int x = 0; x < TFT_WIDTH; x++) {
      yield();

      bool overlapX = x >= imgX && x < imgX + imgW;
      bool overlapY = y >= imgY && y < imgY + imgH;

      if (overlapX && overlapY) {
        int idx = (y - imgY) * imgW + ((x - imgX) % imgW);
        screen.drawPixel(x, y, albumBmp[idx]);
        continue;
      }

      // Dont draw gradient if its too dark
      if (r + g + b > 5) {
        double grad = (300 - y - random(0, 25)) / (double) 300;
        grad = grad < 0 ? 0 : grad;
        uint8_t rGrad = r * grad;
        uint8_t gGrad = g * grad;
        uint8_t bGrad = b * grad;
        screen.drawPixel(x, y, (rGrad << 11) | (gGrad << 5) | bGrad);
      }
    }

    // Dont overwrite text with background fill
    if (y > TEXT_Y) {
      writeSongText(screen, COLOR_RGB565_WHITE);
    }

    // Animate playback bar
    playbackBar.draw(screen, 0);
  }
}

// Callback for TJpg draw function
bool processBmp(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  for (int i = 0; i < w * h; i++) {
    albumBmp[y * 150 + (i % w) + x] = bitmap[i];
    if (i % w == w-1) y++;
  }
  return true;
}

// ------------------------------- WEBSERVER -------------------------------

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
    if (getAuth(/*refresh=*/false, /*fromFile=*/false, server.arg("code"))) {
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

// ------------------------------- TEXT -------------------------------

void writeSongText(DFRobot_ST7789_240x320_HW_SPI& screen, uint16_t color) {
  // Rewrite song/artist text
  screen.setTextSize(2);
  screen.setCursor(TEXT_X, TEXT_Y);
  screen.print(song.songName);
  screen.setTextSize(1);
  screen.print(song.artistName);
}

// ------------------------------- MAIN -------------------------------

// Control timers
uint32_t lastRequest    = 0;
uint32_t lastPotRead    = 0;
uint32_t lastPotChange  = 0;
uint32_t lastImgRequest = 0;

void setup() {
  #ifdef DEBUG
    Serial.begin(115200);
  #endif

  client.setInsecure();

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
  screen.setTextWrap(false);

  // Initialise wifi
  connect(SSID, PASSPHRASE);
  Serial.println(WiFi.localIP());

  // Initialise webserver for spotify OAuth
  server.on("/", webServerHandleRoot);
  server.on("/callback", webServerHandleCallback);
  server.begin();

  TJpgDec.setCallback(processBmp);
  TJpgDec.setJpgScale(2);
}

void loop(){
  server.handleClient();
  yield();

  if (!accessTokenSet && !triedFileToken) {
    if (getAuth(/*refresh=*/true, /*fromFile=*/true, "")) {
      uint32_t timeout = millis() + 5000;
      while (!triedFileToken && timeout > millis()) delay(100);
    }
    #ifdef DEBUG
      else {
        Serial.println("Auth from refresh token file failed...");
      }
    #endif

    triedFileToken = true;
  }

  if (!accessTokenSet) {
    screen.setCursor(0,0);
    screen.setTextSize(2);
    screen.print("Visit \nhttp://" + WiFi.localIP().toString() + "\nto log in :)\n");
    return;
  }

  if (millis() > auth.expiry) {
    auth.expiry = millis() + 10000;
    getAuth(/*refresh=*/true, /*fromFile=*/false, "");
    yield();
  }

  if (millis() - lastRequest > REQUEST_RATE) {
    lastRequest = millis();
    getCurrentlyPlaying();
    yield();
  }

  if (readFlag) {
    readFlag = false;
    if (newSong) {
      // Clear album art and song/artist text
      screen.fillRect(0, 0, TFT_WIDTH, 300, COLOR_RGB565_BLACK);
      // Close playback bar wave when switching songs
      playbackBar.setPlayState(false);
      // Force draw the playback bar closing animation
      playbackBar.draw(screen, 1);
      writeSongText(screen, COLOR_RGB565_WHITE);
      newSong  = 0;
      imageSet = 0;
    }

    playbackBar.duration = song.durationMs;
    playbackBar.updateProgress(song.progressMs);
    playbackBar.setPlayState(song.isPlaying);
    playbackBar.draw(screen, 1);

    if (!imageSet && millis() - lastImgRequest > REQ_TIMEOUT) {
      lastImgRequest = millis();
      if (getAlbumArt()) {
        // Process and draw background gradient and album art
        albumBmp = (uint16_t*) malloc(sizeof(uint16_t) * 150 * 150);
        TJpgDec.drawFsJpg(0, 0, IMG_PATH, LittleFS);
        drawBmp(IMG_X, IMG_Y, 150, 150);
        free(albumBmp);
        imageSet = 1;
      }
    }
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
  // if (lastPotChange != 0 && millis() - lastPotChange > POT_WAIT) {
  //   lastPotChange = 0;
  //   updateVolume();
  // }

  // Slowly change amplitude of playback bar wave
  if (playbackBar.amplitudePercent != song.volume) {
    int inc = playbackBar.amplitudePercent > song.volume ? -6 : 6; 
    playbackBar.amplitudePercent += inc;
    playbackBar.amplitudePercent = min(max(0, playbackBar.amplitudePercent), 100);
  }

  playbackBar.draw(screen, 0);
}
