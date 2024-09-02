#include "PlaybackBar.h"

PlaybackBar::PlaybackBar(int x, int y, int width, int height, int amplitude, float period, int drawRateMs) {
  this->x          = x;
  this->y          = y;
  this->width      = width;
  this->height     = height;
  this->amplitude  = amplitude;
  this->period     = period;
  this->drawRateMs = drawRateMs;
}

void PlaybackBar::draw(DFRobot_ST7789_240x320_HW_SPI& screen, bool force) {

  if (!playing && amplitude == 0) return;
  // Limit draw rate to improve frame time consistency
  if (!force && (millis() - lastDraw < drawRateMs)) return;
  lastDraw = millis(); 

  if (playing) {
    int interpolatedTime = (int) (millis() - lastUpdate + lastProgress);
    progress = min(interpolatedTime, duration);
  }


  // Slowly change amplitude of playback bar wave
  int target = playing ? targetAmplitudePercent : 0;
  if (amplitudePercent != target) {
    int inc = abs(amplitudePercent - target) > AMPLITUDE_D ? AMPLITUDE_D : 1;
    inc *= amplitudePercent > target ? -1 : 1; 
    amplitudePercent += inc;
  }

  int curAmplitudePercent = amplitude * (amplitudePercent / (float) 100);
  int bound = x + width * (progress / (float) duration);
  uint32_t prevTime = curTime;
  curTime++;

  for (int i = x; i < bound; i++) {
    screen.drawPixel(i, y + prevAmplitudePercent * sin(period * (prevTime + i)), COLOR_RGB565_BLACK);
    screen.drawPixel(i, y + curAmplitudePercent * sin(period * (curTime + i)), color);
  }

  prevAmplitudePercent = curAmplitudePercent;

  // Clear pixels between previous progress bar and current
  int sign = prevBound < bound ? 1 : -1;
  for (int i = prevBound; i != bound; i += sign) {
    screen.drawFastVLine(i, y-2*height, 4*height, COLOR_RGB565_BLACK);
  }

  screen.drawFastHLine(bound, y, width + x - bound, color);
  screen.drawFastVLine(bound, y-2*height, 4*height, color);
  prevBound = bound;
}

void PlaybackBar::setPlayState(bool state) {
  playing = state;
}

void PlaybackBar::setAmplitudePercent(int amp) {
  amplitudePercent = amp;
}

void PlaybackBar::setTargetAmplitude(int amp) {
  targetAmplitudePercent = amp;
}

void PlaybackBar::updateProgress(int val) {
  progress = max(0, min(val, duration));
  lastProgress = progress;
  lastUpdate = millis();
}