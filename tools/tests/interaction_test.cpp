#include "../../firmware/DeskPetFaceGame/Interaction.h"
#include "../../firmware/DeskPetFaceGame/Protocol.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <string>
using namespace pet;
struct Fake : Port {
  std::vector<int> phrases;
  int expression=0, moves=0, stops=0, lastKind=0, choice=0;
  void play(uint16_t id) override { phrases.push_back(id); }
  void face(int id) override { expression=id; }
  void head(int kind) override { ++moves; lastKind=kind; }
  void stop() override { ++stops; }
  uint32_t random(uint32_t limit) override { return uint32_t(choice) % limit; }
};
static int tests=0;
#define CHECK(x) do { ++tests; assert(x); } while(0)
void arm(Controller &c,uint32_t now) {
  c.done(now); CHECK(c.state==ARM || c.state==WAIT_HAND);
  c.tick(now,0,false); c.tick(now+300,0,false); CHECK(c.state==WAIT_HAND);
}
int main() {
  { Fake f; Controller c(f); c.event(GAME,0); CHECK(f.phrases.empty());
    c.online=true; c.event(FORWARD,0); CHECK(c.state==TALK && f.phrases.back()==1055);
    c.tick(6000,0,false); CHECK(c.state==TALK); c.done(6000);
    c.tick(13999,0,false); CHECK(f.phrases.size()==1);
    c.tick(14000,0,false); CHECK(f.phrases.back()==1051);
    c.done(15000); c.tick(90000,0,false); CHECK(f.phrases.size()==2 && c.state==IDLE);
  }
  { Fake f; Controller c(f); c.online=true;
    c.event(GAME,0); CHECK(f.phrases.back()==1001); arm(c,1000);
    c.tick(1400,4,false); CHECK(c.state==WAIT_HAND);
    c.tick(1600,1,false); CHECK(f.phrases.back()==1010 && c.remaining==2);
    c.done(2000); c.tick(2200,1,false); CHECK(c.state==RELEASE);
    c.tick(2300,0,false);c.tick(2600,0,false); CHECK(f.phrases.back()==1002);
    arm(c,3000); c.tick(3400,3,false); CHECK(f.phrases.back()==1021);
    arm(c,4000); c.tick(4400,1,false); CHECK(f.phrases.back()==1023 && !c.remaining);
  }
  { Fake f; Controller c(f);c.online=true;c.event(GAME,0);c.done(1000);
    c.tick(1100,1,false);c.tick(9000,1,false);CHECK(f.phrases.back()==1023);
    c.done(9500);c.tick(30000,1,false);CHECK(c.state==IDLE && f.phrases.size()==2);
  }
  { Fake f;Controller c(f);c.online=true;c.event(GAME,0);arm(c,1000);
    c.tick(13300,0,false);CHECK(f.phrases.back()==1023);
  }
  { Fake f;Controller c(f);c.online=true;c.event(PRAISE,0);CHECK(f.phrases.back()==1052);
    c.event(EXIT,10);c.event(PRAISE,20);c.done(1000);CHECK(f.phrases.back()==1024);
    c.done(2000);CHECK(c.state==IDLE);
    c.event(PRAISE,3000);CHECK(f.phrases.back()==1053);
    c.cancel(3100);c.done(3500);CHECK(c.state==IDLE);
  }
  { Fake f;Controller c(f);c.online=true;c.event(GAME,0);c.tick(12000,0,false);
    CHECK(!c.online && c.state==IDLE);c.done(13000);CHECK(c.state==IDLE);
  }
  { Fake f;Controller c(f);c.online=true;c.event(FORWARD,UINT32_MAX-1000);
    c.done(UINT32_MAX-500);c.tick(7499,0,false);CHECK(f.phrases.back()==1051);
    c.done(8000);c.event(REST,9000);c.done(9500);CHECK(c.state==SLEEP);
    c.tick(30000,0,false);CHECK(c.state==SLEEP);
  }
  for(int first=1;first<=2;++first) {
    Fake f;f.choice=first-1;Controller c(f);c.online=true;c.tick(0,0,false);
    c.event(WAKE,10);
    CHECK(c.target==first && c.remaining==2 && f.phrases.back()==1059+first);
    c.tick(100,uint8_t(first),true);CHECK(f.phrases.size()==1);
    c.done(1000);c.tick(1010,uint8_t(first),false);
    CHECK(c.state==TALK && c.remaining==1 && f.lastKind==(first==1?3:2));
    int reward=f.phrases.back();c.done(1800);
    c.tick(1900,uint8_t(first),false);CHECK(c.state==RELEASE); // held hand not double counted
    c.tick(2000,0,true);c.tick(2300,0,false);c.tick(2380,0,false);
    CHECK(c.target==3-first && f.phrases.back()==1000+3-first);
    c.done(3000);c.tick(3010,uint8_t(3-first),false);
    CHECK(c.remaining==0 && f.phrases.back()!=reward);
    c.done(4000);c.tick(4010,0,false);c.tick(4090,0,false);
    CHECK(f.phrases.back()==1026 && c.afterTalk==ASK_AGAIN);
    c.done(5000);c.event(PRAISE,5100);
    CHECK(c.afterTalk==BETWEEN && c.remaining==1);
    c.done(6000);c.tick(6010,0,false);c.tick(6090,0,false);
    CHECK(c.state==TALK && c.afterTalk==ARM && c.prompts==1);
    c.event(EXIT,6100);c.done(6500);CHECK(f.phrases.back()==1024);
    c.done(7000);CHECK(c.state==IDLE && c.remaining==0);
  }
  { Sensors s; CHECK(s.update(1,10)==0 && s.pending==1);
    CHECK(s.update(1,49)==0);CHECK(s.update(1,50)==1);
    CHECK(s.update(0,60)==1);CHECK(s.update(1,100)==1); // brief dropout held
    CHECK(s.update(0,200)==1);CHECK(s.update(0,319)==1);CHECK(s.update(0,320)==0);
    s.update(1,400);s.update(3,410);s.update(1,420);s.update(3,430);
    CHECK(s.update(1,440)==1 && s.pending==0); // right chatter did not delay left
    s.update(3,450);CHECK(s.update(3,490)==3);
    s.update(0,UINT32_MAX-50);CHECK(s.update(0,69)==0); // wraparound release
  }
  { Fake f; Controller c(f); // free interaction needs neither wake nor ASR handshake
    c.tick(0,0,false);c.tick(50,1,false);
    CHECK(!c.online && f.expression==7 && f.lastKind==3 && f.phrases.empty());
    c.tick(100,1,true);c.tick(5000,1,false);CHECK(f.moves==1);
    c.tick(5100,0,false);c.tick(5200,2,false);
    CHECK(f.expression==6 && f.lastKind==2 && f.moves==2);
    c.tick(5300,3,true);CHECK(f.expression==1 && f.moves==2);
    c.tick(6200,3,false);CHECK(f.lastKind==4 && f.moves==3);
    c.tick(6300,0,false);c.tick(7000,0,false);CHECK(f.expression==0);
    CHECK(f.phrases.empty());
  }
  { Fake f;Controller c(f);c.online=true;c.tick(0,0,false);c.event(GAME,10);
    c.tick(20,0,false);c.tick(100,1,false);CHECK(f.phrases.size()==1 && f.moves==0);
    c.done(1000);CHECK(c.state==WAIT_HAND);c.tick(1010,1,false);
    CHECK(f.phrases.back()==1010 && f.moves==1); // approach during prompt accepted
    c.tick(1100,2,false);CHECK(f.moves==1); // no free motion over game reward
  }
  { Fake f;Controller c(f);c.online=true;c.tick(0,1,false);c.event(GAME,10);
    c.tick(50,1,false);c.done(1000);CHECK(c.state==ARM); // preheld not scored
    c.tick(1100,0,false,1);c.tick(1200,0,false);c.tick(1280,0,false);
    CHECK(c.state==WAIT_HAND);c.tick(1300,1,false,2);CHECK(c.state==WAIT_HAND);
    c.tick(1350,3,false);CHECK(f.phrases.back()==1021); // no single-side false award
  }
  { Fake f;Controller c(f);c.online=true;c.event(REST,0);c.tick(10,0,false);c.done(1000);
    c.tick(1050,2,false);CHECK(c.state==IDLE && f.lastKind==2); // touch wakes resting pet
  }
  { uint32_t seq;uint16_t id;
    CHECK(playRequest("PLAY,4294967295,1001",seq,id) && seq==UINT32_MAX && id==1001);
    CHECK(!playRequest("PLAY,4294967296,1001",seq,id));CHECK(!playRequest("PLAY,-1,1001",seq,id));
    CHECK(!playRequest("PLAY,0,1001",seq,id));CHECK(!playRequest("PLAY,1,65536",seq,id));
    CHECK(!playRequest("PLAY,1,1001junk",seq,id));CHECK(!playRequest("PLAY,1",seq,id));
    CHECK(reply("DONE,42","DONE,",seq) && seq==42);CHECK(!reply("DONE,42,extra","DONE,",seq));
    Lines r;const char *v=nullptr;for(char x:std::string("EVT,PRAISE\r\n")) if(auto p=r.push(x,1)) v=p;
    CHECK(v && !strcmp(v,"EVT,PRAISE"));
    for(int n=0;n<100;++n)r.push('a',2);CHECK(!r.push('\n',3));
    r.push('D',10);r.push('O',600);CHECK(!r.push('\n',601));
    v=nullptr;for(char x:std::string("READY,9\n"))if(auto p=r.push(x,700))v=p;
    CHECK(v && !strcmp(v,"READY,9"));
  }
  std::printf("PASS %d checks: game timing, sensors, nonrepeat, cancellation, rollover, framing\n",tests);
}
