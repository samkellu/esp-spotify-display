class SpotifyConn {
  private:
    // Some https client
    String accessToken;
    String refreshToken;

  public:
    SongInfo song;
    bool accessTokenSet = false;
    int expiry;

    SpotifyConn();
    void connect(const char* ssid, const char* passphrase);
    bool getAuth(bool refresh, String code);
    bool getCurrentlyPlaying();
    bool getAlbumArt();
    bool updateVolume();
};

#if defined(ESP8266)
  #error Your not a 8266 
  #include "SpotifyConnESP8266.h"
#elif defined(ESP32)
  #include "SpotifyConnESP32.h"
#else
  #error This code will only run on ESP8266 or ESP32 platforms.
#endif