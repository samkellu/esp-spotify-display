#include <Arduino.h>
#include "DFRobot_GDL.h"
#include "credentials.h"
#include "PlaybackBar.h"

#define FORMAT_LITTLEFS_ON_FAIL true
#include "LittleFS.h"
#include <TJpg_Decoder.h>
#include <ArduinoJson.h>
#include <base64.h>

#define DEBUG

#if defined(ESP8266)
  #include <ESP8266WiFi.h> 
  #include <ESP8266HTTPClient.h>
  #include <ESP8266WebServer.h>
#elif defined(ESP32)

  #ifdef DEBUG
    #define _ASYNC_TCP_SSL_LOGLEVEL_    1
    #define _ASYNC_HTTPS_LOGLEVEL_      1 
    #define DEBUG_ESP_SSL               1
    #define DEBUG_ESP_PORT Serial
  #endif

  #include <AsyncHTTPSRequest_Generic.h>
  #include <WiFi.h>
  #include <HTTPClient.h>
  #include <WebServer.h>
#endif

#define POT                       A3
#define POT_READ_RATE             400        // ms
#define POT_WAIT                  1000       // ms
#define TFT_CS                    15         // D6
#define TFT_RST                   2          // D5
#define TFT_DC                    4          // D4
#define SONG_REQUEST_RATE         7000       // ms
#define REQUEST_RATE              200        // ms
#define MAX_AUTH_REFRESH_FAILS    3
#define IMG_PATH                  "/img.jpg"
#define STREAM_BUF_SIZE           128
#define TOKEN_PATH                "/token.txt"
#define REQ_TIMEOUT               5000       // ms
#define TFT_WIDTH                 240
#define TFT_HEIGHT                320
#define IMG_Y                     40
#define IMG_X                     45
#define IMG_SCALE                 2
#define IMG_W                     150
#define IMG_H                     150
#define TEXT_Y                    240
#define TEXT_X                    0
#define GRADIENT_BLACK_THRESHOLD  5

struct SongInfo {
  // General song info
  String songName;
  String artistName;
  String albumName;
  String id;

  // Album art
  String imgUrl;
  uint16_t height;
  uint16_t width;

  // Playback info
  int durationMs;
  int progressMs;
  int volume;
  String deviceName;
  bool isPlaying;
};

struct AuthInfo {
  String accessToken;
  String refreshToken;
  int expiry;
};