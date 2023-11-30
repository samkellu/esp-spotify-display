
#include "DFRobot_GDL.h"
#include "credentials.h"
#include <ESP8266WiFi.h>

#define POT A0
#define TFT_CS  D6
#define TFT_RST D5
#define TFT_DC  D4
#define TFT_WIDTH 240
#define TFT_HEIGHT 320

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
    WiFiClient client;


  public:

    // Connects to the network specified in credentials.h
    void connect(const char* ssid, const char* passphrase) {
      Serial.printf("Attempting connection to %s...\n", ssid);
      WiFi.begin(ssid, passphrase);
      while ((WiFi.status() != WL_CONNECTED)) {
        delay(200);
      }

      Serial.printf("Successfully connected to %s!\n", ssid);
    }

};

PlaybackBar playbackBar = PlaybackBar(15, 280, TFT_WIDTH-30, 5, 5, 40, COLOR_RGB565_WHITE);
SpotifyConn spotifyConn = SpotifyConn();
int play = 0;
void setup() {
  Serial.begin(115200);
  screen.begin();
  screen.fillScreen(COLOR_RGB565_BLACK);
  spotifyConn.connect(SSID, PASSPHRASE);
}

void loop(){
  delay(50);
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
