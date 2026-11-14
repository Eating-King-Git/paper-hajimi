// DeskPetFaceGame v0.9.3. Guided cheeks; requires ASR v9.3 audio resources.
// OLED SDA41 SCL42; IR left21 right2 (viewer perspective), both at 3V3.
// ASR PB5 -> G14, ASR PA3 <- G47, 9600. Single USB on ESP TTL.
// SG90 signal G1, separate regulated 5V with common GND; 1300..1700us.
#include <Arduino.h>
#include <Wire.h>
#include <string.h>
#include <initializer_list>
#include "Interaction.h"
#include "Protocol.h"
constexpr int SDA_PIN=41, SCL_PIN=42, IR_LEFT=21, IR_RIGHT=2;
constexpr int ASR_RX_PIN=14, ASR_TX_PIN=47;
uint8_t oledAddress=0, pixels[128*64/8];
bool ready=false;
uint32_t lastRetry=0, lastFrame=0, animationStart=0, lastLog=0, frameNumber=0;
int lastDrawnFace=-1, currentFace=0, frameFace=0;
uint8_t displayPage=8, lastRaw=255, lastStable=255;
constexpr int SERVO_PIN = 1;
constexpr uint32_t SERVO_HZ = 50;
constexpr uint8_t SERVO_BITS = 14;
constexpr int SERVO_CENTER_US = 1500;
constexpr int SERVO_LOW_US = 1300;
constexpr int SERVO_HIGH_US = 1700;
constexpr bool HEAD_REVERSED = false;
constexpr uint32_t HAND_MOTION_COOLDOWN_MS = 5000;
bool servoEnabled = false;
bool automaticHead = true;
int servoPulseUs = SERVO_CENTER_US;
int servoTargetUs = SERVO_CENTER_US;
uint32_t servoCommandAt = 0;
uint32_t servoStepAt = 0;
bool motionRunning = false;
int motionKind = 0;  // 1=wake, 2=forward, 3=backward, 4=hand.
int motionTargets[3] = {1500, 1500, 1500};
uint32_t motionHolds[3] = {250, 250, 250};
int motionLength = 0;
int motionIndex = 0;
bool motionReached = false;
uint32_t motionReachedAt = 0;
bool handMotionArmed = false;
bool handMotionSent = false;
uint32_t lastHandMotionAt = 0;

int headPulse(int offset) {
  int pulse = SERVO_CENTER_US + (HEAD_REVERSED ? -offset : offset);
  return constrain(pulse, SERVO_LOW_US, SERVO_HIGH_US);
}

bool writeServoPulse(int pulseUs) {
  pulseUs = constrain(pulseUs, SERVO_LOW_US, SERVO_HIGH_US);
  const uint32_t duty = (uint32_t(pulseUs) * ((1UL << SERVO_BITS) - 1) + 10000) / 20000;
  return ledcWrite(SERVO_PIN, duty);
}

void stopServo() {
  motionRunning = false;
  if (servoEnabled) ledcDetach(SERVO_PIN);
  servoEnabled = false;
  pinMode(SERVO_PIN, OUTPUT);
  digitalWrite(SERVO_PIN, LOW);
  Serial0.println("SERVO PWM OFF (power still connected)");
}

void servoFault() {
  automaticHead = false;
  stopServo();
  Serial0.println("SERVO ERROR: auto disabled; check supply/wiring, then send a");
}

bool enableServo(uint32_t now) {
  if (servoEnabled) return true;
  if (!ledcAttach(SERVO_PIN, SERVO_HZ, SERVO_BITS)) {
    servoFault();
    return false;
  }
  servoEnabled = true;
  servoPulseUs = SERVO_CENTER_US;
  servoTargetUs = SERVO_CENTER_US;
  servoStepAt = now;
  if (!writeServoPulse(servoPulseUs)) {
    servoFault();
    return false;
  }
  return true;
}

void startHeadMotion(int kind, uint32_t now) {
  if (!automaticHead) return;
  if (motionRunning && motionKind == kind) return;  // No repeat/restart queue.
  if (!enableServo(now)) return;
  motionKind = kind;
  motionIndex = 0;
  motionReached = false;
  if (kind == 1 || kind == 4) {
    motionLength = 3;
    motionTargets[0] = headPulse(-100); motionHolds[0] = 180;
    motionTargets[1] = headPulse(100);  motionHolds[1] = 180;
    motionTargets[2] = SERVO_CENTER_US; motionHolds[2] = 300;
  } else {
    motionLength = 2;
    motionTargets[0] = headPulse(kind == 2 ? 200 : -200);
    motionHolds[0] = 600;
    motionTargets[1] = SERVO_CENTER_US; motionHolds[1] = 300;
  }
  servoTargetUs = motionTargets[0];
  servoCommandAt = now;
  servoStepAt = now;
  motionRunning = true;
  Serial0.printf("HEAD event=%d target=%d us (1=wake 2=fwd 3=back 4=hand)\n", kind, servoTargetUs);
}

void commandServo(char command, uint32_t now) {
  if (command == 'x') {
    automaticHead = false;
    stopServo();
    Serial0.println("HEAD AUTO DISABLED: voice/IR will not restart it; a=resume");
    return;
  }
  if (command == 'a') {
    stopServo();
    automaticHead = true;
    // Resume only; no old game is restarted.
    Serial0.println("HEAD AUTO ENABLED: waiting for a NEW voice/hand event");
    return;
  }
  automaticHead = false;
  motionRunning = false;
  if (!servoEnabled && command != 'k') {
    Serial0.println("MANUAL: send k to enable center first; a=return to auto");
    return;
  }
  if (!enableServo(now)) return;
  servoTargetUs = headPulse(command == 'j' ? -200 : command == 'l' ? 200 : 0);
  servoCommandAt = now;
  servoStepAt = now;
  Serial0.printf("MANUAL target=%d us; PWM off after 5 seconds; a=auto\n", servoTargetUs);
}

void updateServo(uint32_t now) {
  if (!servoEnabled) return;
  if (uint32_t(now - servoCommandAt) >= (motionRunning ? 6000U : 5000U)) {
    if (motionRunning) {
      Serial0.println("HEAD TIMEOUT: motion canceled, auto disabled");
      automaticHead = false;
    }
    stopServo();
    return;
  }
  const uint32_t elapsed = uint32_t(now - servoStepAt);
  if (elapsed < 20) return;
  servoStepAt = now;
  // OLED transfers share this loop (~100ms at 100kHz). Catch up by elapsed
  // time, but cap each adjustment so a long I2C stall cannot make a large jump.
  const int delta = int(min((elapsed / 20) * 5, uint32_t(25)));
  if (servoPulseUs < servoTargetUs) servoPulseUs = min(servoPulseUs + delta, servoTargetUs);
  else if (servoPulseUs > servoTargetUs) servoPulseUs = max(servoPulseUs - delta, servoTargetUs);
  if (!writeServoPulse(servoPulseUs)) { servoFault(); return; }
  if (!motionRunning || servoPulseUs != servoTargetUs) return;
  if (!motionReached) {
    motionReached = true;
    motionReachedAt = now;
  }
  if (uint32_t(now - motionReachedAt) < motionHolds[motionIndex]) return;
  ++motionIndex;
  if (motionIndex >= motionLength) {
    Serial0.println("HEAD PLAN COMPLETE: center command sent, releasing PWM");
    stopServo();
    return;
  }
  servoTargetUs = motionTargets[motionIndex];
  motionReached = false;
}


bool commands(const uint8_t *data, size_t length) {
  Wire.beginTransmission(oledAddress);
  Wire.write(uint8_t(0x00));
  Wire.write(data, length);
  return Wire.endTransmission() == 0;
}

// Send at most one OLED page in a loop pass (~13ms at 100kHz).
bool showPage(uint8_t page) {
  const uint8_t position[]={uint8_t(0xB0|page),0x00,0x10};
  if(!commands(position,sizeof(position))) return false;
  for(int column=0;column<128;column+=16) {
    Wire.beginTransmission(oledAddress); Wire.write(uint8_t(0x40));
    Wire.write(pixels+page*128+column,16);
    if(Wire.endTransmission()!=0) return false;
  }
  return true;
}
bool showFrame() { // Startup test only; normal animation is incremental.
  for(uint8_t page=0;page<8;++page) if(!showPage(page)) return false;
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
  glyph(2,2,'V'); number(9,2,9); number(119,2,face);
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


pet::Lines asrLines;
pet::Sensors sensors;
uint32_t sequence=0, pendingSequence=0, sentAt=0, lastHello=0;
bool acknowledged=false;
struct Hardware : pet::Port {
  void play(uint16_t id) override {
    if (++sequence == 0) ++sequence;
    pendingSequence=sequence; sentAt=millis(); acknowledged=false;
    Serial1.printf("PLAY,%lu,%u\n", (unsigned long)sequence, id);
    Serial0.printf("TX PLAY seq=%lu phrase=%u\n", (unsigned long)sequence, id);
  }
  void face(int id) override { currentFace=id; }
  void head(int kind) override { startHeadMotion(kind, millis()); }
  void stop() override { stopServo(); }
  uint32_t random(uint32_t limit) override { return uint32_t(::random(limit)); }
} hardware;
pet::Controller game(hardware);
pet::Event eventByName(const char *name) {
  const char *names[]={"", "WAKE", "PRAISE", "GAME", "AGAIN", "EXIT", "REST", "FORWARD", "BACK"};
  for(int i=1;i<=8;++i) if(!strcmp(name,names[i])) return pet::Event(i);
  return pet::NONE;
}
void linkFault(uint32_t now) {
  pendingSequence=0; game.fault(now);
  Serial0.println("ASR LINK/PLAY ERROR: game stopped; check v9 firmware, G47->PA3 and supply");
}
void receiveVoice(uint32_t now) {
  for(int n=0;n<96 && Serial1.available();++n) {
    now=millis();
    const char *line=asrLines.push(char(Serial1.read()),now);
    if(!line) continue;
    Serial0.printf("ASR %s\n",line);
    if(!strcmp(line,"READY,9")) {
      if(!game.online) { game.online=true; currentFace=0; }
      continue;
    }
    if(!game.online) continue;
    uint32_t seq;
    if(pet::reply(line,"ACK,",seq) && seq==pendingSequence) acknowledged=true;
    else if(pet::reply(line,"DONE,",seq) && seq==pendingSequence) {
      pendingSequence=0; game.done(now);
    } else if(!strncmp(line,"ERR,",4)) {
      const char *s=line+4;
      if(pet::number(s,seq) && *s==',' && seq==pendingSequence) linkFault(now);
    } else if(!strncmp(line,"EVT,",4)) {
      game.event(eventByName(line+4),now);
    }
  }
  now=millis();
  if(pendingSequence && ((!acknowledged && uint32_t(now-sentAt)>=2000) || uint32_t(now-sentAt)>=12000)) linkFault(now);
  if(!game.online && uint32_t(now-lastHello)>=1000) { Serial1.print("HELLO\n"); lastHello=now; }
}
void readConsole(uint32_t now) {
  for(int i=0;i<32 && Serial0.available();++i) {
    char c=char(Serial0.read());
    if(c=='x' || c=='j' || c=='k' || c=='l' || c=='a') {
      game.cancel(now); commandServo(c,now);
    } else if(c=='w' || c=='p' || c=='g' || c=='q') {
      Serial0.println("CONSOLE SIMULATED EVENT (not a speech-recognition test)");
      game.event(c=='w'?pet::WAKE:c=='p'?pet::PRAISE:c=='g'?pet::GAME:pet::EXIT,now);
    } else if(c>='0' && c<='9') { game.cancel(now); currentFace=c-'0'; }
  }
}
void setup() {
  Serial0.begin(115200); delay(500);
  Serial0.println("\nDeskPetFaceGame v0.9.3 GUIDED CHEEKS; needs ASR v9.3 audio resources");
  pinMode(SERVO_PIN,OUTPUT); digitalWrite(SERVO_PIN,LOW);
  pinMode(IR_LEFT,INPUT_PULLUP); pinMode(IR_RIGHT,INPUT_PULLUP);
  randomSeed(esp_random()); sequence=esp_random() | 1;
  Wire.begin(SDA_PIN,SCL_PIN,100000); Wire.setTimeOut(50);
  ready=initScreen(); lastRetry=millis();
  Serial1.begin(9600,SERIAL_8N1,ASR_RX_PIN,ASR_TX_PIN);
  Serial0.println("x=STOP head/game, a=auto, j/k/l=manual; w/p/g/q=simulate wake/praise/game/exit");
  Serial0.println("IR: 0=clear 1=left 2=right 3=both; settle=changing bits. Free touch works without voice/link.");
}

void loop() {
  uint32_t now=millis();
  readConsole(now); receiveVoice(now);
  now=millis();
  uint8_t raw=(digitalRead(IR_LEFT)==LOW?1:0)|(digitalRead(IR_RIGHT)==LOW?2:0);
  uint8_t mask=sensors.update(raw,now);
  if(raw!=lastRaw || mask!=lastStable) {
    Serial0.printf("IR edge raw=%u stable=%u settle=%u\n",raw,mask,sensors.pending);
    lastRaw=raw; lastStable=mask;
  }
  updateServo(now);
  game.tick(now,mask,motionRunning,sensors.pending);
  // Controller deadline can fault independently; invalidate its transport too.
  if(!game.online) pendingSequence=0;
  if(uint32_t(now-lastLog)>=1000) {
    Serial0.printf("V9.3 state=%d target=%s IRraw=%u IR=%u settle=%u face=%d link=%d auto=%d\n",int(game.state),game.target==1?"LEFT":"RIGHT",raw,mask,sensors.pending,currentFace,game.online,automaticHead);
    lastLog=now;
  }
  if(!ready) {
    if(uint32_t(now-lastRetry)>=3000) { ready=initScreen(); lastRetry=millis(); }
    delay(1); return;
  }
  if(displayPage>=8) {
  if(uint32_t(now-lastFrame)<100) { delay(1); return; }
  lastFrame=now; frameFace=currentFace; drawFace(frameFace,now);
  number(18,2,3); // V9 + revision digit 3, independent of the face ID at right.
  if(game.state==pet::ARM || game.state==pet::WAIT_HAND || (game.state==pet::TALK && game.afterTalk==pet::ARM)) {
    // Indicate the requested cheek on the viewer's left/right edge.
    int x=game.target==1?2:116;
    line(x,23,x+8,31,2); line(x+8,31,x,39,2);
  }
  if(!game.online) { line(1,60,126,60,1); } // Link missing indicator.
  if(game.freeActive) {
    if(game.freeSide&1) line(2,25,2,40,2);
    if(game.freeSide&2) line(123,25,123,40,2);
  }
  displayPage=0;
  }
  if(!showPage(displayPage)) { ready=false; displayPage=8; lastRetry=now; Serial0.println("OLED I2C ERROR"); }
  else if(++displayPage==8 && lastDrawnFace!=frameFace) { lastDrawnFace=frameFace; Serial0.printf("DISPLAY ACK face=%d\n",frameFace); }
}
