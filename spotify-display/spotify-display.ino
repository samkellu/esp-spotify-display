
#include "DFRobot_GDL.h"
#include <vector>

#define POT A0
#define TFT_CS  D6
#define TFT_RST D5
#define TFT_DC  D4
#define TFT_WIDTH 240
#define TFT_HEIGHT 320

DFRobot_ST7789_240x320_HW_SPI screen(TFT_DC, TFT_CS, TFT_RST);

class PlaybackBar {
  public:
    int trackLength = 100;
    int progress = 70;
    bool playing = -1;
    uint16_t time = 0;
    uint16_t color;
    int x;
    int y;
    int width;
    int height;
    float period = 4.0f;
    float amplitude = height;
    std::vector<int> wave;


    PlaybackBar(int x, int y, int width, int height, uint16_t color) {
      this->x = x;
      this->y = y;
      this->width = width;
      this->height = height;
      this->color = color;

      for (int i = 0; i < 2*period; i++) {
        wave.push_back(sin(i/period) * amplitude);
      }
    }

    void draw() {

      int bound = x + width * (progress/(float)trackLength);
      screen.drawFastHLine(bound, y, width-x, COLOR_RGB565_WHITE);
      screen.drawFastVLine(bound, y-2*height, 4*height, COLOR_RGB565_WHITE);
      screen.drawFastVLine(bound-1, y-2*height, 4*height, COLOR_RGB565_BLACK);
      screen.drawFastVLine(bound-2, y-2*height, 4*height, COLOR_RGB565_BLACK);
      screen.drawFastVLine(bound-3, y-2*height, 4*height, COLOR_RGB565_BLACK);

      for (int i = x; i < bound; i++) {
        double scaledTime = time/4;
        double scaledTimePrev = (time-1)/4;

        Serial.println((time + i)%8);
        Serial.println(wave[(time + i)%8]);
        screen.drawPixel(i, y + wave[(time + i)%8], COLOR_RGB565_BLACK);
        screen.drawPixel(i, y + wave[(time + i)%8], color);
      }
      time++;
    }
};

PlaybackBar playbackBar(15, 100, TFT_WIDTH-15, 5, COLOR_RGB565_WHITE);
void setup() {
  Serial.begin(115200);
  screen.begin();
  screen.fillScreen(COLOR_RGB565_BLACK);
}

void loop(){
  delay(50);
  playbackBar.draw();
  playbackBar.progress++;
  if (playbackBar.progress >= playbackBar.trackLength) {
    playbackBar.progress = 0;
    screen.fillScreen(COLOR_RGB565_BLACK);
  }
}
