"""Rebuild only the independent v9 ESP32 sketch, reusing v8 hardware routines."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
base = (ROOT / 'firmware/DeskPetHeadMotion/DeskPetHeadMotion.ino').read_text(encoding='utf-8')
servo = base[base.index('constexpr int SERVO_PIN'):base.index('void setVoiceFace')]
servo = servo[:servo.index('void onHandApproach')] + servo[servo.index('void commandServo'):]
servo = servo.replace('    handMotionArmed = !handNear;', '    // Resume only; no old game is restarted.')
# Widen only this stage; preserve the confirmed v8 source and gradual motion.
servo = servo.replace('SERVO_LOW_US = 1400', 'SERVO_LOW_US = 1300').replace('SERVO_HIGH_US = 1600', 'SERVO_HIGH_US = 1700')
servo = servo.replace('headPulse(-50)', 'headPulse(-100)').replace('headPulse(50)', 'headPulse(100)')
servo = servo.replace('kind == 2 ? 100 : -100', 'kind == 2 ? 200 : -200')
servo = servo.replace("command == 'j' ? -100 : command == 'l' ? 100 : 0", "command == 'j' ? -200 : command == 'l' ? 200 : 0")
display = base[base.index('bool commands'):base.index('void setup()')]
start=display.index('bool showFrame()')
end=display.index('void pixel(')
display=display[:start]+r'''// Send at most one OLED page in a loop pass (~13ms at 100kHz).
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

'''+display[end:]
drawing = base[base.index('void line('):base.index('void readConsole')].replace('number(9,2,8)', 'number(9,2,9)')
header = r'''// DeskPetFaceGame v0.9.3. Guided cheeks; requires ASR v9.3 audio resources.
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
'''
controller = r'''
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
'''
loop = r'''
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
'''
out=ROOT/'firmware/DeskPetFaceGame/DeskPetFaceGame.ino'
out.write_text(header+servo+display+drawing+controller+loop,encoding='utf-8')
print(out)
