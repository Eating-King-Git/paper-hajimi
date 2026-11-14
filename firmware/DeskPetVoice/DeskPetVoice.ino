// GOOUUU ESP32-S3-CAM + 128x64 SSD1306 I2C OLED.
// OLED: GND -> GND, VCC -> 3V3, SCL -> GPIO42, SDA -> GPIO41.
// One IR obstacle module: VCC -> 3V3, GND -> GND, OUT -> GPIO21.
// ASRPRO UART0 PB5 (TX) -> ESP32 GPIO14 (RX), GND -> GND.
// Single USB: power ESP32 through TTL; ESP32 5V -> ASR 5V, shared GND.
// Leave ASR USB unplugged in this arrangement. Verify supply stability in use.
// GPIO47 is assigned as an unused UART TX; leave it disconnected.
// Use the board's TTL USB-C connector. No external libraries required.
#include <Arduino.h>
#include <Wire.h>
#include <string.h>

constexpr int SDA_PIN = 41;
constexpr int SCL_PIN = 42;
constexpr int IR_PIN = 21;
constexpr int ASR_RX_PIN = 14;
constexpr int UNUSED_ASR_TX_PIN = 47;
enum VoiceFace { VOICE_IDLE, VOICE_HELLO, VOICE_FORWARD, VOICE_BACKWARD };
VoiceFace voiceFace = VOICE_IDLE;
uint32_t voiceChangedAt = 0;
uint8_t wakeBytes = 0;
uint32_t wakeLastByteAt = 0;
constexpr int IR_ACTIVE_LEVEL = LOW;
constexpr uint32_t IR_DEBOUNCE_MS = 150;
bool irCandidate = false;
bool handNear = false;
uint32_t irChangedAt = 0;
uint8_t oledAddress = 0;
uint8_t pixels[128 * 64 / 8];
bool ready = false;
uint32_t lastRetry = 0;
uint32_t lastFrame = 0;
uint32_t animationStart = 0;
uint32_t lastLog = 0;

void setVoiceFace(VoiceFace face, uint32_t now) {
  voiceFace = face;
  voiceChangedAt = now;
  Serial0.printf("Voice face=%d (1=wake, 2=forward, 3=backward)\n", int(face));
}

void updateVoice(uint32_t now) {
  if (wakeBytes && uint32_t(now - wakeLastByteAt) > 500) wakeBytes = 0;
  // Limit work per loop so unexpected UART traffic cannot block the display.
  for (int count = 0; count < 64 && Serial1.available(); ++count) {
    const uint8_t value = uint8_t(Serial1.read());
    Serial0.printf("ASR RX: %02X\n", value);
    if (value == 0xA0) {
      wakeBytes = 1;
      wakeLastByteAt = now;
      continue;
    }
    if (wakeBytes && value == 0x00) {
      ++wakeBytes;
      wakeLastByteAt = now;
      if (wakeBytes == 3) {
        wakeBytes = 0;
        setVoiceFace(VOICE_HELLO, now);
      }
      continue;
    }
    wakeBytes = 0;
    if (value == 0x47) setVoiceFace(VOICE_FORWARD, now);
    else if (value == 0x51) setVoiceFace(VOICE_BACKWARD, now);
  }
  if (voiceFace != VOICE_IDLE && uint32_t(now - voiceChangedAt) >= 3000) {
    voiceFace = VOICE_IDLE;
  }
}

void updateSensor(uint32_t now) {
  const bool detected = digitalRead(IR_PIN) == IR_ACTIVE_LEVEL;
  if (detected != irCandidate) {
    irCandidate = detected;
    irChangedAt = now;
  }
  if (irCandidate != handNear && uint32_t(now - irChangedAt) >= IR_DEBOUNCE_MS) {
    handNear = irCandidate;
    Serial0.println(handNear ? "HAND NEAR - hello!" : "HAND AWAY - idle");
  }
}

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
  Serial0.printf("OLED READY at 0x%02X. Move your hand in front of the IR sensor.\n", oledAddress);
  delay(1000);
  animationStart = millis();
  return true;
}

void setup() {
  // Explicit UART0 keeps logs on the CH340/TTL connector.
  Serial0.begin(115200);
  delay(800);
  Serial0.println("\nDeskPetVoice v3 - OLED + IR + ASRPRO UART receive");
  pinMode(IR_PIN, INPUT_PULLUP);
  irCandidate = digitalRead(IR_PIN) == IR_ACTIVE_LEVEL;
  irChangedAt = millis();
  Wire.begin(SDA_PIN, SCL_PIN, 100000);
  Wire.setTimeOut(50);
  ready = initScreen();
  lastRetry = millis();
  Serial1.begin(9600, SERIAL_8N1, ASR_RX_PIN, UNUSED_ASR_TX_PIN);
  Serial0.println("ASR UART ready: 9600 8N1, RX=14. Say Hajimi, then Qianjin/Houtui.");
}

void loop() {
  uint32_t now = millis();
  updateSensor(now);
  updateVoice(now);
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
  if (handNear) {
    height = 42;
    gaze = 0;
  }
  // Voice expressions temporarily take priority over IR for three seconds.
  if (voiceFace == VOICE_HELLO || voiceFace == VOICE_FORWARD) {
    height = 42;
    gaze = 0;
  } else if (voiceFace == VOICE_BACKWARD) {
    height = 6;
    gaze = 0;
  }
  memset(pixels, 0, sizeof(pixels));
  eye(25 + gaze, 32, height);
  eye(73 + gaze, 32, height);
  if (voiceFace == VOICE_HELLO || voiceFace == VOICE_FORWARD ||
      (voiceFace == VOICE_IDLE && handNear)) {
    // Small smile below the eyes.
    pixel(57, 56); pixel(58, 57); pixel(59, 58);
    for (int x = 60; x <= 67; ++x) pixel(x, 59);
    pixel(68, 58); pixel(69, 57); pixel(70, 56);
  }
  if (!showFrame()) {
    Serial0.println("I2C write failed. Retrying in 3 seconds.");
    ready = false;
    lastRetry = millis();
  }
  if (uint32_t(now - lastLog) >= 1000) {
    Serial0.printf("IR raw=%d, state=%s, uptime=%lu s\n",
                   digitalRead(IR_PIN), handNear ? "NEAR" : "IDLE",
                   (unsigned long)(now / 1000));
    lastLog = now;
  }
}
