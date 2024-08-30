#ifndef PLAYBACKBAR_H
#define PLAYBACKBAR_H
#include "DFRobot_GDL.h"

#define AMPLITUDE_D 1

class PlaybackBar {
  private:
    bool playing = 1;
    bool playStateFlag = 0;
    uint16_t color = COLOR_RGB565_WHITE;
    int x, y;
    int width, height;
    float period;
    int amplitude;
    int amplitudePercent;
    int prevAmplitudePercent;
    int targetAmplitudePercent;
    int drawRateMs;
    int prevBound;
    uint32_t curTime = 0;
    uint32_t lastDraw = 0;
    uint32_t lastUpdate = 0;
    int lastProgress = 0;

  public:
    int progress = 0;
    int duration = 1;

    PlaybackBar(int x, int y, int width, int height, int amplitude, float period, int drawRateMs);
    void draw(DFRobot_ST7789_240x320_HW_SPI& screen, bool force);
    void setPlayState(bool state);
    void setAmplitudePercent(int amp);
    void setTargetAmplitude(int amp);
    void updateProgress(int val);
};

#endif