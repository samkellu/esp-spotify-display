#include "DFRobot_GDL.h"
#include "credentials.h"
#include "PlaybackBar.h"

#define FORMAT_LITTLEFS_ON_FAIL true
#include "LittleFS.h"
#include <TJpg_Decoder.h>
#include <ArduinoJson.h>
#include <base64.h>

#if defined(ESP8266)
  #include <ESP8266WiFi.h> 
  #include <ESP8266HTTPClient.h>
  #include <ESP8266WebServer.h>
#elif defined(ESP32)
  #define _ASYNC_TCP_SSL_LOGLEVEL_    1
  #define _ASYNC_HTTPS_LOGLEVEL_      1
  #include <AsyncHTTPSRequest_Generic.h>
  #include <WiFi.h>
  #include <WiFiClientSecure.h>
  #include <WebServer.h>
#endif

#define POT           A0
#define POT_READ_RATE 400       // ms
#define POT_WAIT      3000      // ms
#define TFT_CS        15        // D6
#define TFT_RST       2         // D5
#define TFT_DC        4         // D4
#define TFT_WIDTH     240
#define TFT_HEIGHT    320
#define REQUEST_RATE  7000     // ms
#define IMG_PATH      "/img.jpg"

#define DEBUG         1

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