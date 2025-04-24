#include "spotify-display.h"

DFRobot_ST7789_240x320_HW_SPI screen(TFT_DC, TFT_CS, TFT_RST);
PlaybackBar playbackBar = PlaybackBar(15, 310, TFT_WIDTH-30, 5, 8, 0.1, 50);
WebServer server(80);

AsyncHTTPSRequest httpsAuth;
AsyncHTTPSRequest httpsCurrent;
AsyncHTTPSRequest httpsVolume;
HTTPClient client;

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
void connect(const char* ssid, const char* passphrase)
{
  #ifdef DEBUG
    Serial.printf("Attempting connection to %s\n", ssid);
  #endif

  WiFi.mode(WIFI_STA);
  // Wait for connection
  WiFi.begin(ssid, passphrase);
  while (WiFi.status() != WL_CONNECTED) delay(100);
  
  #ifdef DEBUG
    Serial.print("Successfully connected to ");
    Serial.println(ssid);
  #endif
}

// ------------------------------- GET/REFRESH ACCESS TOKENS -------------------------------

void authCB(void* optParam, AsyncHTTPSRequest* request, int readyState)
{
  // Fail if client isnt finished reading or response failed
  if (readyState != readyStateDone) return;
  if (request->responseHTTPcode() != 200) return;

  StaticJsonDocument<1024> doc;
  String json = request->responseText();
  DeserializationError err = deserializeJson(doc, json);

  if (err)
  {
    #ifdef DEBUG
      Serial.print("Deserialisation failed for string: ");
      Serial.println(json);
      Serial.println(err.f_str());
    #endif
    return;
  }

  auth.accessToken = doc["access_token"].as<String>();
  auth.expiry = millis() + 1000 * doc["expires_in"].as<int>();
  // Refresh token not included in refresh response
  String refreshToken = doc["refresh_token"].as<String>();
  // As if spotify sends back a string that says "null" when using refresh token rather than excluding it
  if (refreshToken != "null")
  {
    auth.refreshToken = refreshToken;

    // Try to write refresh token to file
    File f = LittleFS.open(TOKEN_PATH, "w");
    if (!f)
    {
      #ifdef DEBUG
        Serial.println("Failed to write token to file...");
      #endif
      return;
    }

    f.print(auth.refreshToken);
    f.close();
  }

  accessTokenSet = true;
  #ifdef DEBUG
    Serial.println("Successfully got access tokens!");
  #endif
}

bool getAuth(bool refresh, bool fromFile, String code)
{
  // Attempt to automatically get access token from saved refresh token
  if (refresh && fromFile)
  {
    File f = LittleFS.open(TOKEN_PATH, "r");
    if (f)
    {
      auth.refreshToken = f.readString();
      if (auth.refreshToken.length() == 0)
      {
        refresh = false;
      }
      f.close();
    }
  }

  // Fail if client is busy
  if (httpsAuth.readyState() != readyStateUnsent && httpsAuth.readyState() != readyStateDone) return false;
  if (httpsAuth.open("POST", "https://accounts.spotify.com/api/token"))
  {
    String body;
    if (refresh)
    {
      body = "grant_type=refresh_token&refresh_token=" + auth.refreshToken;
    
    }
    else
    {
      body = "grant_type=authorization_code&code=" + code + 
             "&redirect_uri=http://" + WiFi.localIP().toString() + "/callback";
    }

    String authStr = "Basic " + base64::encode(String(CLIENT) + ":" + String(CLIENT_SECRET));
    httpsAuth.setReqHeader("Content-Type", "application/x-www-form-urlencoded");
    httpsAuth.setReqHeader("Authorization", authStr.c_str());
    httpsAuth.send(body);
    return true;

  }
  else
  {
    #ifdef DEBUG
      Serial.println("Connection failed!");
    #endif
    return false;
  }
}

// ------------------------------- GET CURRENTLY PLAYING -------------------------------

void currentlyPlayingCB(void* optParam, AsyncHTTPSRequest* request, int readyState)
{
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
  #ifdef DEBUG
    if (doc.overflowed())
    {
      Serial.printf("Deserialization rept: Overrun %d, Capacity %zu\n", doc.overflowed(), doc.capacity());
    }
  #endif

  if (err)
  {
    #ifdef DEBUG
      Serial.print(F("Deserialisation failed"));
      Serial.println(err.f_str());
    #endif
    request->abort();
    return;
  }

  JsonObject item   = doc["item"];
  if (item["id"] == NULL) return;

  String prevId     = song.id;
  JsonObject device = doc["device"];
  JsonArray images  = item["album"]["images"];
  song.id           = item["id"].as<String>();
  song.progressMs   = doc["progress_ms"].as<int>();
  song.isPlaying    = doc["is_playing"].as<bool>();
  song.volume       = device["volume_percent"].as<int>();
  song.deviceName   = device["name"].as<String>();
  song.songName     = item["name"].as<String>();
  song.albumName    = item["album"]["name"].as<String>();
  song.artistName   = item["artists"][0]["name"].as<String>();
  song.durationMs   = item["duration_ms"].as<int>();

  for (int i = 0; i < images.size(); i++)
  {
    int height = images[i]["height"].as<int>();
    int width  = images[i]["width"].as<int>();

    // Only grab appropriate sized image
    if (height <= IMG_H * IMG_SCALE && width <= IMG_W * IMG_SCALE)
    {
      song.height = height;
      song.width  = width;
      song.imgUrl = images[i]["url"].as<String>();
      break;
    }
  }

  readFlag = true;
  newSong = song.id != prevId; 
}

bool getCurrentlyPlaying()
{
  // Fail if client is busy
  if (httpsCurrent.readyState() != readyStateUnsent && httpsCurrent.readyState() != readyStateDone) return false;
  if (httpsCurrent.open("GET", "https://api.spotify.com/v1/me/player"))
  {
    String authStr = "Bearer " + auth.accessToken;
    httpsCurrent.setReqHeader("Cache-Control", "no-cache");
    httpsCurrent.setReqHeader("Authorization", authStr.c_str());
    httpsCurrent.send();
    return true;

  }
  else
  {
    #ifdef DEBUG
      Serial.println("Connection failed!");
    #endif
    return false;
  }
}

// ------------------------------- GET ALBUM ART -------------------------------

// Synchronous due to large file size limitations
bool getAlbumArt()
{

  if (!song.imgUrl)
  {
    #ifdef DEBUG
      Serial.println("No image url available.");
    #endif
    return false;
  }

  client.begin(song.imgUrl);
  client.setTimeout(REQ_TIMEOUT);
  client.addHeader("Cache-Control", "no-cache");

  int resp = client.GET();
  if (resp != 200)
  {
    #ifdef DEBUG
      Serial.printf("An error occurred while getting image %c\nHTTP %d\n", song.imgUrl, resp);
    #endif
    client.end();
    return false;
  }

  File f = LittleFS.open(IMG_PATH, "w");
  if (!f)
  {
    #ifdef DEBUG
      Serial.println("Failed to open file descriptor.");
    #endif
    client.end();
    return false;
  }

  int numBytes = client.getSize();
  int offset = 0;
  uint8_t buf[STREAM_BUF_SIZE];
  Stream* data = client.getStreamPtr();
  while (offset < numBytes)
  {
    int available = data->available();
    if (available)
    {
      int bytes = data->readBytes(buf, min(available, STREAM_BUF_SIZE));
      f.write(buf, bytes);
      offset += bytes;
    }

    // Reset WDT
    yield();
  }

  f.close();
  client.end();
  #ifdef DEBUG
    Serial.printf("Wrote to file %d/%d bytes\n", offset, numBytes);
  #endif
  return true;
}

// ------------------------------- VOLUME CONTROL -------------------------------

void volumeSetCB(void* optParam, AsyncHTTPSRequest* request, int readyState)
{
  // Fail if client hasnt finished reading or response failed
  if (readyState != readyStateDone) return;

  Serial.println("An error occurred: HTTP \n" + request->responseHTTPcode());
  if (request->responseHTTPcode() != 200)
  {
    #ifdef DEBUG
      Serial.println("An error occurred: HTTP " + request->responseHTTPcode());
    #endif
    return;
  }
}

bool updateVolume()
{
  // Fail if client isnt ready
  if (httpsVolume.readyState() != readyStateUnsent && httpsVolume.readyState() != readyStateDone) return false;

  String url = "https://api.spotify.com/v1/me/player/volume?volume_percent=" + String(song.volume);

  #ifdef DEBUG
    Serial.printf("Setting volume to: %d\n", song.volume);
  #endif

  if (httpsVolume.open("PUT", url.c_str()))
  {
    String authStr = "Bearer " + auth.accessToken;
    httpsVolume.setReqHeader("Authorization", authStr.c_str());
    httpsVolume.setReqHeader("Cache-Control", "no-cache");
    httpsVolume.setReqHeader("Content-Length", "0");
    httpsVolume.send();
    Serial.printf("curl -X PUT %s -H Authorization: %s -H Cache-Control: no-cache -H Content-Length: 0\n", url.c_str(), authStr.c_str());
    return true;
  }
  else
  {
    #ifdef DEBUG
      Serial.println("Connection failed!");
    #endif
    return false;
  }
}

// ------------------------------- TEXT -------------------------------

void writeSongText(DFRobot_ST7789_240x320_HW_SPI& screen, uint16_t color)
{
  // Rewrite song/artist text
  screen.setTextSize(2);
  screen.setCursor(TEXT_X, TEXT_Y);
  screen.print(song.songName);
  screen.setTextSize(1);
  screen.print(song.artistName);
}

// ------------------------------- TJPG -------------------------------

static unsigned int rSeed = 43;

// Big primes and overflows should be good enough
int fast_rand()
{
  rSeed = 396437 * rSeed + 45955009;
  return (rSeed >> 16) & 0x7FFF;
}

uint16_t r, g, b;
bool sampleColor = false;

// Callback for TJpg draw function, draws scaled jpeg with gradient backfill
bool processBmp(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap)
{
  // Take average color components of the first block of the image
  if (sampleColor)
  {
    sampleColor = false;
    r = 0;
    g = 0;
    b = 0;
    uint16_t rr = 0, rg = 0, rb = 0;
    for (int i = 0; i < w * h; i++)
    {
      // Ensure average isnt black skewed too heavily
      rr = ((bitmap[i] >> 11) & 0x1F);
      rg = ((bitmap[i] >> 5) & 0x3F);
      rb = (bitmap[i] & 0x1F);
      if (rr + rg + rb < GRADIENT_BLACK_THRESHOLD)
      {
        rr = r;
        rg = g;
        rb = b;
      }

      r = (r * i + rr) / (i + 1);
      g = (g * i + rg) / (i + 1);
      b = (b * i + rb) / (i + 1);
    }

    #ifdef DEBUG
      Serial.printf("Gradient start color = r(%u) g(%u) b(%u)\n", r, g, b);
    #endif
  }

  // Dont draw gradient if its too dark or covered by the image
  bool drawBackfill = x == IMG_X || (y == IMG_Y + IMG_H - h && x == IMG_X + IMG_W - w);
  if (drawBackfill && r + g + b > GRADIENT_BLACK_THRESHOLD)
  {
    int yStart = y == IMG_Y ? 0 : y;
    int yEnd = y == IMG_Y + IMG_H - h && x == IMG_X + IMG_W - w ? 300 : y + h;
    for (int gy = yStart; gy < yEnd; gy++)
    {
      for (int gx = 0; gx < TFT_WIDTH; gx++)
      {
        bool overlapX = gx >= IMG_X && gx < IMG_X + IMG_W;
        bool overlapY = gy >= IMG_Y && gy < IMG_Y + IMG_H;

        if (!(overlapX && overlapY))
        {
          double grad = (300 - gy - (fast_rand() % 25)) / (double) 300;
          grad = grad < 0 ? 0 : grad;
          uint8_t rGrad = r * grad;
          uint8_t gGrad = g * grad;
          uint8_t bGrad = b * grad;
          screen.drawPixel(gx, gy, (rGrad << 11) | (gGrad << 5) | bGrad);
        }
      }

      // Animate playback bar when drawing backfill
      if (gy < IMG_Y || gy > IMG_Y + IMG_H)
        playbackBar.draw(screen, 0);
      
      // Dont overwrite text with background fill
      if (gy > TEXT_Y)
        writeSongText(screen, COLOR_RGB565_WHITE);
    }
  }

  // Animate playback bar when drawing bitmap
  playbackBar.draw(screen, 0);
  screen.drawRGBBitmap(x, y, bitmap, w, h);
  yield();
  return true;
}

// ------------------------------- WEBSERVER -------------------------------

void webServerHandleRoot()
{
  String header = "https://accounts.spotify.com/authorize?client_id=" + String(CLIENT) +
                  "&response_type=code&redirect_uri=http://" + WiFi.localIP().toString() +
                  "/callback&scope=%20user-modify-playback-state%20user-read-currently-playing%20" +
                  "user-read-playback-state";
  
  server.sendHeader("Location", header, true);
  server.send(302, "text/html", "");
}

void webServerHandleCallback()
{
  if (server.arg("code") != "")
  {
    if (getAuth(/*refresh=*/false, /*fromFile=*/false, server.arg("code")))
    {
      server.send(200, "text/html", "Login complete! you may close this tab.\r\n");
    }
    else
    {
      server.send(200, "text/html", "Authentication failed... Please try again :(\r\n");
    }
  } 
  #ifdef DEBUG
    else
    {
      Serial.println("An error occurred. Server provided no code arg.");
    }
  #endif
}

// ------------------------------- MAIN -------------------------------

// Control timers
uint32_t lastRequest      = 0;
uint32_t lastPotRead      = 0;
uint32_t lastPotChange    = 0;
uint32_t lastImgRequest   = 0;
uint32_t lastVolRequest   = 0;
uint32_t lastSongRequest  = 0;
int      authRefreshFails = 0;

void setup()
{
  #ifdef DEBUG
    Serial.begin(9600);
  #endif

  // Initialise LittleFS
  if (!LittleFS.begin())
  {
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

  // Setup TJpg settings
  TJpgDec.setCallback(processBmp);
  TJpgDec.setJpgScale(IMG_SCALE);

  httpsAuth.onReadyStateChange(authCB);
  httpsCurrent.onReadyStateChange(currentlyPlayingCB);
  httpsVolume.onReadyStateChange(volumeSetCB);

  client.setReuse(true);
}

void loop()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    #ifdef DEBUG
      Serial.println("WiFi reconnecting.");
    #endif

    // reconnect causing DHCP issues, disconnect -> begin seems to work
    WiFi.disconnect();
    connect(SSID, PASSPHRASE);
  }

  server.handleClient();
  yield();

  if (!accessTokenSet && !triedFileToken)
  {
    if (getAuth(/*refresh=*/true, /*fromFile=*/true, ""))
    {
      uint32_t timeout = millis() + REQ_TIMEOUT;
      while (!triedFileToken && timeout > millis()) yield();
    }
  #ifdef DEBUG
    else
    {
      Serial.println("Auth from refresh token file failed...");
    }
  #endif

    triedFileToken = true;
  }

  // Auto refresh auth token when it expires
  if (millis() > auth.expiry && authRefreshFails < MAX_AUTH_REFRESH_FAILS)
  {
    getAuth(/*refresh=*/true, /*fromFile=*/false, "");
    accessTokenSet = false;
    uint32_t timeout = millis() + REQ_TIMEOUT;
    while (!accessTokenSet)
    {    
      // Attempt authorisation refresh next iteration if timedout
      if (timeout < millis())
      {
        authRefreshFails++;
        return;
      }

      playbackBar.draw(screen, false);
      yield();
    }

    accessTokenSet = true;
    authRefreshFails = 0;
  }

  // Force user to login if auth refresh fails multiple times
  if (!accessTokenSet)
  {
    authRefreshFails = 0;
    screen.setCursor(0,0);
    screen.setTextSize(2);
    screen.print("Visit \nhttp://" + WiFi.localIP().toString() + "\nto log in :)\n");
    return;
  }

  // Read potentiometer value at fixed interval
  if (millis() - lastPotRead > POT_READ_RATE)
  {
    int newVol = 100 - 100 * (analogRead(POT) / (float) 4096);

    // Account for pot wobble
    if (abs(song.volume - newVol) > 2)
    {
      lastPotChange = millis();
      song.volume = newVol;
    }

    lastPotRead = millis();
  }

  bool forceFetch = false;
  // Only send api POST when pot hasnt changed for a while
  if (lastPotChange != 0 && millis() - lastPotChange > POT_WAIT && millis() - lastVolRequest > SONG_REQUEST_RATE && millis() - lastRequest > REQUEST_RATE)
  {
    forceFetch = true;
    lastPotChange = 0;
    lastVolRequest = millis();
    lastRequest = lastVolRequest;
    updateVolume();
  }

  if (forceFetch || millis() - lastSongRequest > SONG_REQUEST_RATE && millis() - lastRequest > REQUEST_RATE)
  {
    #ifdef DEBUG
      Serial.printf("\nStack:%d,Heap:%lu\n", uxTaskGetStackHighWaterMark(NULL), (unsigned long) ESP.getFreeHeap());
    #endif
    lastSongRequest = millis();
    lastRequest = lastSongRequest;
    getCurrentlyPlaying();
    yield();
  }

  if (readFlag)
  {
    readFlag = false;
    if (newSong)
    {
      // Clear album art and song/artist text
      screen.fillRect(0, 0, TFT_WIDTH, 300, COLOR_RGB565_BLACK);
      // Close playback bar wave when switching songs
      playbackBar.setPlayState(false);
      // Force draw the playback bar closing animation
      playbackBar.draw(screen, true);
      writeSongText(screen, COLOR_RGB565_WHITE);
      newSong  = false;
      imageSet = false;
    }

    playbackBar.setTargetAmplitude(song.volume);
    playbackBar.duration = song.durationMs;
    playbackBar.updateProgress(song.progressMs);
    playbackBar.setPlayState(song.isPlaying);
    playbackBar.draw(screen, true);

    if (!imageSet && millis() - lastImgRequest > REQ_TIMEOUT && millis() - lastRequest > REQUEST_RATE)
    {
      lastImgRequest = millis();
      lastRequest = lastImgRequest;
      if (getAlbumArt())
      {
        // Process and draw background gradient and album art
        sampleColor = true;
        TJpgDec.drawFsJpg(IMG_X, IMG_Y, IMG_PATH, LittleFS);
        imageSet = true;
      }
    }
  }

  // Slowly change amplitude of playback bar wave
  playbackBar.draw(screen, false);
}