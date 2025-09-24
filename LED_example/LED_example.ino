#include <Adafruit_NeoPixel.h>

#define LED_PIN    10    // WCMCU-2812-16 的燈條控制腳位，常見是 GPIO4 或 GPIO5
#define LED_COUNT 16    // 總共有 16 顆 WS2812

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  strip.begin();
  strip.show(); // 初始化全部關閉
  strip.setBrightness(50); // 亮度 (0~255)
}

void loop() {
  rainbowCycle(5); // 彩虹循環
}

// 彩虹循環效果
void rainbowCycle(uint8_t wait) {
  uint16_t i, j;

  for(j=0; j<256*5; j++) { // 彩虹跑 5 次
    for(i=0; i<strip.numPixels(); i++) {
      strip.setPixelColor(i, Wheel(((i * 256 / strip.numPixels()) + j) & 255));
    }
    strip.show();
    delay(wait);
  }
}

// 彩虹顏色轉換
uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if(WheelPos < 85) {
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if(WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}
