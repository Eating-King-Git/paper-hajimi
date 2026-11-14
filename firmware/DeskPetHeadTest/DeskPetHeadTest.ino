// GOOUUU ESP32-S3-CAM + 128x64 SSD1306 I2C OLED.
// OLED: GND -> GND, VCC -> 3V3, SCL -> GPIO42, SDA -> GPIO41.
// One IR obstacle module: VCC -> 3V3, GND -> GND, OUT -> GPIO21.
// ASRPRO UART0 PB5 (TX) -> ESP32 GPIO14 (RX), GND -> GND.
// Single USB: power ESP32 through TTL; ESP32 5V -> ASR 5V, shared GND.
// Leave ASR USB unplugged in this arrangement. Verify supply stability in use.
// GPIO47 is assigned as an unused UART TX; leave it disconnected.
// SG90 signal -> GPIO1; power from a separate regulated 5V supply, shared GND.
// Do not join external +5V to ESP32 +5V while USB is connected.
// Use the board's TTL USB-C connector. No external libraries required.
#include <Arduino.h>
#include <Wire.h>
#include <string.h>
#include <initializer_list>

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
constexpr uint32_t SLEEP_AFTER_MS = 30000;
constexpr uint32_t NEAR_HAPPY_MS = 2000;
constexpr uint32_t GOODBYE_MS = 1500;
uint32_t lastActivityAt = 0;
uint32_t nearStartedAt = 0;
uint32_t awayStartedAt = 0;
bool goodbyeActive = false;
uint8_t oledAddress = 0;
uint8_t pixels[128 * 64 / 8];
bool ready = false;
uint32_t lastRetry = 0;
uint32_t lastFrame = 0;
uint32_t animationStart = 0;
uint32_t lastLog = 0;
bool demo = false;
uint32_t demoStart = 0;
int manualFace = -1;
uint32_t manualAt = 0;
int lastDrawnFace = -1;
uint32_t frameNumber = 0;

constexpr int SERVO_PIN = 1;
constexpr uint32_t SERVO_HZ = 50;
constexpr uint8_t SERVO_BITS = 14;
constexpr int SERVO_CENTER_US = 1500;
constexpr int SERVO_LOW_US = 1400;
constexpr int SERVO_HIGH_US = 1600;
bool servoEnabled = false;
int servoPulseUs = SERVO_CENTER_US;
int servoTargetUs = SERVO_CENTER_US;
uint32_t servoCommandAt = 0;
uint32_t servoStepAt = 0;

bool writeServoPulse(int pulseUs) {
  const uint32_t duty = (uint32_t(pulseUs) * ((1UL << SERVO_BITS) - 1) + 10000) / 20000;
  return ledcWrite(SERVO_PIN, duty);
}

void stopServo() {
  if (servoEnabled) ledcDetach(SERVO_PIN);
  servoEnabled = false;
  pinMode(SERVO_PIN, OUTPUT);
  digitalWrite(SERVO_PIN, LOW);
  Serial0.println("SERVO PWM OFF (power still connected)");
}

void commandServo(char command, uint32_t now) {
  if (command == 'x') { stopServo(); return; }
  if (!servoEnabled) {
    if (command != 'k') {
      Serial0.println("SERVO: send k to enable center first");
      return;
    }
    if (!ledcAttach(SERVO_PIN, SERVO_HZ, SERVO_BITS)) {
      Serial0.println("SERVO ERROR: LEDC attach failed");
      return;
    }
    servoEnabled = true;
    servoPulseUs = SERVO_CENTER_US;
    if (!writeServoPulse(servoPulseUs)) { stopServo(); return; }
  }
  servoTargetUs = command == 'j' ? SERVO_LOW_US : command == 'l' ? SERVO_HIGH_US : SERVO_CENTER_US;
  servoCommandAt = now;
  servoStepAt = now;
  Serial0.printf("SERVO target=%d us; auto PWM off after 5 seconds\n", servoTargetUs);
}

void updateServo(uint32_t now) {
  if (!servoEnabled) return;
  if (uint32_t(now - servoCommandAt) >= 5000) { stopServo(); return; }
  if (uint32_t(now - servoStepAt) < 20) return;
  servoStepAt = now;
  if (servoPulseUs < servoTargetUs) servoPulseUs += 5;
  else if (servoPulseUs > servoTargetUs) servoPulseUs -= 5;
  if (!writeServoPulse(servoPulseUs)) {
    Serial0.println("SERVO ERROR: PWM write failed");
    stopServo();
  }
}

void setVoiceFace(VoiceFace face, uint32_t now) {
  demo = false;
  manualFace = -1;
  voiceFace = face;
  voiceChangedAt = now;
  lastActivityAt = now;
  goodbyeActive = false;
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
  if (voiceFace != VOICE_IDLE && uint32_t(now - voiceChangedAt) >= 8000) {
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
    lastActivityAt = now;
    demo = false;
    manualFace = -1;
    voiceFace = VOICE_IDLE;
    if (handNear) {
      nearStartedAt = now;
      goodbyeActive = false;
    } else {
      awayStartedAt = now;
      goodbyeActive = true;
    }
    Serial0.println(handNear ? "HAND NEAR - hello!" : "HAND AWAY - idle");
  }
}

// Ordinary behavior; voice and manual commands are layered on top in loop().
int behaviorFace(uint32_t now) {
  if (handNear) {
    lastActivityAt = now;
    return uint32_t(now - nearStartedAt) < NEAR_HAPPY_MS ? 1 : 6;
  }
  if (goodbyeActive) {
    if (uint32_t(now - awayStartedAt) < GOODBYE_MS) return 7;
    goodbyeActive = false;
  }
  return uint32_t(now - lastActivityAt) >= SLEEP_AFTER_MS ? 4 : 0;
}

bool commands(const uint8_t *data, size_t length) {
  Wire.beginTransmission(oledAddress);
  Wire.write(uint8_t(0x00));
  Wire.write(data, length);
  return Wire.endTransmission() == 0;
}

bool showFrame() {
  // Explicit page positioning on every frame, matching the vendor page driver.
  for (uint8_t page = 0; page < 8; ++page) {
    const uint8_t position[] = {uint8_t(0xB0 | page), 0x00, 0x10};
    if (!commands(position, sizeof(position))) return false;
    for (int column = 0; column < 128; column += 16) {
      Wire.beginTransmission(oledAddress);
      Wire.write(uint8_t(0x40));
      Wire.write(pixels + page * 128 + column, 16);
      if (Wire.endTransmission() != 0) return false;
    }
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
  // SSD1306 128x64, internal charge pump, explicit page addressing.
  const uint8_t init[] = {
    0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
    0x8D, 0x14, 0x20, 0x02, 0xA1, 0xC8, 0xDA, 0x12,
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
  Serial0.println("\nDeskPetHeadTest v0.7.0 - MANUAL SG90 test; j/k/l/x");
  pinMode(SERVO_PIN, OUTPUT);
  digitalWrite(SERVO_PIN, LOW);  // No PWM until explicit k command.
  pinMode(IR_PIN, INPUT_PULLUP);
  irCandidate = digitalRead(IR_PIN) == IR_ACTIVE_LEVEL;
  irChangedAt = millis();
  Wire.begin(SDA_PIN, SCL_PIN, 100000);
  Wire.setTimeOut(50);
  ready = initScreen();
  lastRetry = millis();
  Serial1.begin(9600, SERIAL_8N1, ASR_RX_PIN, UNUSED_ASR_TX_PIN);
  demoStart = millis();
  lastActivityAt = millis();
  Serial0.println("Servo: k=center/enable, j=1400us, l=1600us, x=PWM off. External 5V required.");
  Serial0.println("Keys: 0-9=face, d=demo, n=normal. Voice overrides demo. Hold=8 seconds.");
  Serial0.println("ASR UART ready: 9600 8N1, RX=14. Say Hajimi, then Qianjin/Houtui.");
}

void line(int x0, int y0, int x1, int y1, int thickness = 2) {
  int dx = abs(x1-x0), sx = x0<x1 ? 1 : -1;
  int dy = -abs(y1-y0), sy = y0<y1 ? 1 : -1;
  int error = dx+dy;
  while (true) {
    for (int a=0; a<thickness; ++a)
      for (int b=0; b<thickness; ++b) pixel(x0+a,y0+b);
    if (x0==x1 && y0==y1) break;
    int twice=2*error;
    if (twice>=dy) { error+=dy; x0+=sx; }
    if (twice<=dx) { error+=dx; y0+=sy; }
  }
}

void disk(int cx, int cy, int radius, bool filled = true) {
  for (int y=-radius;y<=radius;++y) for (int x=-radius;x<=radius;++x) {
    int d=x*x+y*y;
    if (d<=radius*radius && (filled || d>=(radius-2)*(radius-2))) pixel(cx+x,cy+y);
  }
}

void glyph(int x, int y, char c) {
  uint8_t v[5]={0,0,0,0,0};
  const char letters[]="FWDBACKV";
  const uint8_t fonts[][5]={
    {127,9,9,9,1},{63,64,56,64,63},{127,65,65,34,28},
    {127,73,73,73,54},{126,17,17,17,126},{62,65,65,65,34},
    {127,8,20,34,65},{31,32,64,32,31}};
  for (int i=0;i<8;++i) if (letters[i]==c) memcpy(v,fonts[i],5);
  for (int a=0;a<5;++a) for (int b=0;b<7;++b) if (v[a]&(1<<b)) pixel(x+a,y+b);
}

void number(int x, int y, int n) {
  const uint8_t masks[]={0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
  const int segments[7][4]={{0,0,4,0},{4,0,4,3},{4,3,4,6},{0,6,4,6},{0,3,0,6},{0,0,0,3},{0,3,4,3}};
  for (int i=0;i<7;++i) if (masks[n]&(1<<i)) line(x+segments[i][0],y+segments[i][1],x+segments[i][2],y+segments[i][3],1);
}

void smile() { line(52,49,57,54); line(57,54,70,54); line(70,54,75,49); }
void heart(int cx) {
  disk(cx-6,25,7); disk(cx+6,25,7);
  for (int y=26;y<=44;++y) {
    int half=(44-y)*13/18;
    line(cx-half,y,cx+half,y,1);
  }
}

void drawFace(int face, uint32_t now) {
  memset(pixels,0,sizeof(pixels));
  glyph(2,2,'V'); number(9,2,7); number(119,2,face);
  switch (face) {
    case 0: { // neutral with gaze and blink
      int gaze=int((now/1800)%3)*4-4;
      int h=(now%4000>3700) ? 3 : 26;
      eye(24+gaze,31,h); eye(74+gaze,31,h); line(58,51,69,51);
      break;
    }
    case 1: // happy: inverted-U eyes
      for(int x: {25,75}) { line(x,35,x+14,21,3); line(x+14,21,x+28,35,3); }
      smile(); break;
    case 2: // sad, eyebrows rise towards centre, one tear
      eye(25,34,13); eye(75,34,13);
      line(25,20,53,15); line(75,15,103,20);
      line(53,55,63,50); line(63,50,74,55); disk(106,45,3); break;
    case 3: // angry sloping eyes
      for(int x=0;x<28;++x) {
        line(25+x,23+x/3,25+x,42,1);
        line(75+x,32-x/3,75+x,42,1);
      }
      line(55,54,73,54,3); break;
    case 4: // sleepy
      line(25,34,52,34,3); line(75,34,102,34,3);
      disk(64,51,4,false); line(96,13,108,13); line(108,13,96,23); line(96,23,108,23); break;
    case 5: // surprise: ring eyes and O mouth
      disk(39,30,13,false); disk(89,30,13,false); disk(64,52,5,false); break;
    case 6: heart(39); heart(89); smile(); break;
    case 7: // wink
      eye(25,30,27); line(75,22,91,31,3); line(91,31,75,40,3); smile(); break;
    case 8: // forward, joyful eyes + large UP arrow and FWD
      glyph(53,2,'F'); glyph(59,2,'W'); glyph(65,2,'D');
      line(21,34,36,19,4); line(36,19,51,34,4);
      line(77,34,92,19,4); line(92,19,107,34,4);
      line(63,59,63,43,3); line(63,43,54,52,3); line(63,43,72,52,3); break;
    case 9: // backward: very distinct X eyes + DOWN arrow and BACK
      glyph(51,2,'B'); glyph(57,2,'A'); glyph(63,2,'C'); glyph(69,2,'K');
      for(int x: {25,77}) { line(x,19,x+24,41,4); line(x+24,19,x,41,4); }
      line(63,44,63,58,3); line(63,58,54,49,3); line(63,58,72,49,3); break;
  }
}

void readConsole(uint32_t now) {
  for (int i=0;i<32 && Serial0.available();++i) {
    char c=char(Serial0.read());
    if(c=='j' || c=='k' || c=='l' || c=='x') {
      commandServo(c, now);
    } else if(c>='0' && c<='9') {
      lastActivityAt=now; goodbyeActive=false;
      manualFace=c-'0'; manualAt=now; demo=false; voiceFace=VOICE_IDLE;
      Serial0.printf("CONSOLE face=%d\n",manualFace);
    } else if(c=='d' || c=='n') {
      lastActivityAt=now; goodbyeActive=false;
      manualFace=-1; voiceFace=VOICE_IDLE; demo=(c=='d'); demoStart=now;
    }
  }
}

void loop() {
  uint32_t now = millis();
  updateSensor(now);
  updateVoice(now);
  readConsole(now);
  updateServo(now);
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
  int face = 0;
  if (manualFace >= 0 && uint32_t(now - manualAt) >= 8000) manualFace = -1;
  if (demo) {
    uint32_t step = (now - demoStart) / 1800;
    if (step >= 10) demo = false;
    else face = int(step);
  }
  if (!demo) {
    face = behaviorFace(now);
    if (voiceFace == VOICE_HELLO) face = 5;
    if (voiceFace == VOICE_FORWARD) face = 8;
    if (voiceFace == VOICE_BACKWARD) face = 9;
    if (manualFace >= 0) face = manualFace;
  }
  drawFace(face, now);
  // Visible heartbeat: alternates bottom-left/right once each second.
  for (int x = 0; x < 5; ++x) pixel(((now / 1000) % 2 ? 123 : 0) + x, 63);
  if (!showFrame()) {
    Serial0.println("DISPLAY ERROR: I2C frame write failed");
    ready = false;
    lastRetry = now;
    lastDrawnFace = -1;
  } else {
    ++frameNumber;
    if (face != lastDrawnFace) {
      Serial0.printf("DISPLAY ACK face=%d frame=%lu (bus ACK, verify screen visually)\n", face, (unsigned long)frameNumber);
      lastDrawnFace = face;
    }
  }
  if (uint32_t(now - lastLog) >= 1000) {
    Serial0.printf("V7 face=%d IR=%d frames=%lu\n", face, digitalRead(IR_PIN), (unsigned long)frameNumber);
    lastLog = now;
  }
}
