// GOOUUU ESP32-S3-CAM + 128x64 SSD1306 I2C OLED.
// OLED: GND -> GND, VCC -> 3V3, SCL -> GPIO42, SDA -> GPIO41.
// Use the board's TTL USB-C connector. No external libraries required.
#include <Arduino.h>
#include <Wire.h>
#include <string.h>

constexpr int SDA_PIN = 41;
constexpr int SCL_PIN = 42;
uint8_t oledAddress = 0;
uint8_t pixels[128 * 64 / 8];
bool ready = false;
uint32_t lastRetry = 0;
uint32_t lastFrame = 0;
uint32_t animationStart = 0;
uint32_t lastLog = 0;

bool commands(const uint8_t *data, size_t length) {
  Wire.beginTransmission(oledAddress);
  Wire.write(uint8_t(0x00));
  Wire.write(data, length);
  return Wire.endTransmission() == 0;
}

bool showFrame() {
  const uint8_t window[] = {0x21, 0, 127, 0x22, 0, 7};
  if (!commands(window, sizeof(window))) return false;
  for (size_t offset = 0; offset < sizeof(pixels); offset += 16) {
    Wire.beginTransmission(oledAddress);
    Wire.write(uint8_t(0x40));
    Wire.write(pixels + offset, 16);
    if (Wire.endTransmission() != 0) return false;
  }
  return true;
}

void pixel(int x, int y) {
  if (x >= 0 && x < 128 && y >= 0 && y < 64) {
    pixels[x + (y / 8) * 128] |= uint8_t(1 << (y & 7));
  }
}

void eye(int x, int centerY, int height) {
  const int width = 30;
  const int radius = height < 8 ? height / 2 : 5;
  const int top = centerY - height / 2;
  for (int dy = 0; dy < height; ++dy) {
    for (int dx = 0; dx < width; ++dx) {
      int cx = dx < radius ? radius - dx :
               (dx >= width - radius ? dx - (width - radius - 1) : 0);
      int cy = dy < radius ? radius - dy :
               (dy >= height - radius ? dy - (height - radius - 1) : 0);
      if (cx == 0 || cy == 0 || cx * cx + cy * cy <= radius * radius) {
        pixel(x + dx, top + dy);
      }
    }
  }
}

bool initScreen() {
  oledAddress = 0;
  Serial0.println("Scanning I2C: SDA=41, SCL=42, 100 kHz...");
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial0.printf("I2C device: 0x%02X\n", address);
      if (!oledAddress && (address == 0x3C || address == 0x3D)) {
        oledAddress = address;
      }
    }
  }
  if (!oledAddress) {
    Serial0.println("OLED NOT FOUND. Power off before checking the four wires.");
    return false;
  }
  // SSD1306 128x64, internal charge pump, horizontal addressing.
  const uint8_t init[] = {
    0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
    0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
    0x81, 0x7F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6
  };
  if (!commands(init, sizeof(init))) return false;
  memset(pixels, 0, sizeof(pixels));
  for (int x = 0; x < 128; ++x) { pixel(x, 0); pixel(x, 63); }
  for (int y = 0; y < 64; ++y) { pixel(0, y); pixel(127, y); }
  eye(25, 32, 30);
  eye(73, 32, 30);
  if (!showFrame()) return false;
  const uint8_t on[] = {0xAF};
  if (!commands(on, sizeof(on))) return false;
  Serial0.printf("OLED READY at 0x%02X. Look for a border, then blinking eyes.\n", oledAddress);
  delay(1000);
  animationStart = millis();
  return true;
}

void setup() {
  // Explicit UART0 keeps logs on the CH340/TTL connector.
  Serial0.begin(115200);
  delay(800);
  Serial0.println("\nDeskPetScreen v1 - screen only");
  Wire.begin(SDA_PIN, SCL_PIN, 100000);
  Wire.setTimeOut(50);
  ready = initScreen();
  lastRetry = millis();
}

void loop() {
  uint32_t now = millis();
  if (!ready) {
    if (uint32_t(now - lastRetry) >= 3000) {
      ready = initScreen();
      lastRetry = millis();
    }
    delay(10);
    return;
  }
  if (uint32_t(now - lastFrame) < 100) {
    delay(1);
    return;
  }
  lastFrame = now;
  const uint32_t elapsed = now - animationStart;
  const uint32_t phase = elapsed % 4000;
  int height = 30;
  if (phase >= 3400 && phase < 3500) height = 15;
  else if (phase >= 3500 && phase < 3700) height = 3;
  else if (phase >= 3700 && phase < 3800) height = 15;
  int gaze = 0;
  switch ((elapsed / 2500) % 4) {
    case 1: gaze = -6; break;
    case 3: gaze = 6; break;
  }
  memset(pixels, 0, sizeof(pixels));
  eye(25 + gaze, 32, height);
  eye(73 + gaze, 32, height);
  if (!showFrame()) {
    Serial0.println("I2C write failed. Retrying in 3 seconds.");
    ready = false;
    lastRetry = millis();
  }
  if (uint32_t(now - lastLog) >= 5000) {
    Serial0.printf("Running, uptime=%lu s\n", (unsigned long)(now / 1000));
    lastLog = now;
  }
}
