#inlcude "PlaybackBar.h"

PlaybackBar::PlaybackBar(DFRobot_ST7789_240x320_HW_SPI screen, int x, int y, int width, int height, int amplitude, float period, int drawRateMs) {
    this->screen     = screen;
    this->x          = x;
    this->y          = y;
    this->width      = width;
    this->height     = height;
    this->amplitude  = amplitude;
    this->period     = period;
    this->drawRateMs = drawRateMs;
}

void PlaybackBar::draw() {
    // Limit draw rate to improve frame time consistency
    if (millis() - lastDraw < drawRateMs || (!playing && !playStateFlag)) return;

    lastDraw = millis();
    int curAmplitude = amplitude * (amplitudePercent / (float) 100);
    int bound = x + width * (progress / (float) duration);
    uint32_t prevTime = curTime;
    curTime++;

    // Stop playing
    if (!playStateFlag && playing) {
        // Close wave incrementally to zero amplitude
        for (int i = prevAmplitude; i > 0; i--) {
            for (int j = x; j < prevBound; j++) {
            screen.drawPixel(j, y + i * sin(period * (prevTime + j)), COLOR_RGB565_BLACK);
            screen.drawPixel(j, y + (i - 1) * sin(period * (prevTime + j)), color);
            }
            delay(100);
        }

        screen.drawFastVLine(prevBound, y-2*height, 4*height, COLOR_RGB565_BLACK);
        screen.drawFastVLine(bound, y-2*height, 4*height, color);
        playing = 0;

    // Start playing after pause
    } else if (playStateFlag && !playing) {
        screen.drawFastVLine(prevBound, y-2*height, 4*height, COLOR_RGB565_BLACK);
        screen.drawFastVLine(bound, y-2*height, 4*height, color);

        // Animate wave opening to set amplitude
        for (int i = 1; i <= curAmplitude; i++) {
            for (int j = x; j < bound; j++) {
            screen.drawPixel(j, y + (i - 1) * sin(period * (curTime + j)), COLOR_RGB565_BLACK);
            screen.drawPixel(j, y + i * sin(period * (curTime + j)), color);
            }
            delay(100);
        }

        playing = 1;
        prevBound = bound;
        return;

    } else {
        // Draw animated wave
        for (int i = x; i < bound; i++) {
            screen.drawPixel(i, y + prevAmplitude * sin(period * (prevTime + i)), COLOR_RGB565_BLACK);
            screen.drawPixel(i, y + curAmplitude * sin(period * (curTime + i)), color);
        }

        prevAmplitude = curAmplitude;
    }

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
    playStateFlag = state;
}