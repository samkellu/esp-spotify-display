#ifndef PLAYBACKBAR_H
#define PLAYBACKBAR_H
#include "DFRobot_GDL.h"

class PlaybackBar {
  private:
    bool playing = 1;
    bool playStateFlag = 0;
    uint16_t color = COLOR_RGB565_WHITE;
    int x, y;
    int width, height;
    float period;
    int amplitude;
    int drawRateMs;
    int prevBound;
    int prevAmplitude;
    uint32_t curTime = 0;
    uint32_t lastDraw = 0;

  public:
    int amplitudePercent;
    int progress = 0;
    int duration = 1;

    PlaybackBar(int x, int y, int width, int height, int amplitude, float period, int drawRateMs);
    void draw(DFRobot_ST7789_240x320_HW_SPI& screen);
    void setPlayState(bool state);
};

#endif