#pragma once
#include <stdint.h>

// Hardware-independent controller. All times are unsigned millis(), wrap safe.
namespace pet {
enum Event { NONE, WAKE, PRAISE, GAME, AGAIN, EXIT, REST, FORWARD, BACK };
enum State { IDLE, TALK, LISTEN, ARM, WAIT_HAND, RELEASE, BETWEEN, ASK_AGAIN, SLEEP };
struct Port {
  virtual void play(uint16_t id) = 0;
  virtual void face(int id) = 0;
  virtual void head(int kind) = 0;
  virtual void stop() = 0;
  virtual uint32_t random(uint32_t limit) = 0;
};
struct Controller {
  Port &io;
  State state = IDLE, afterTalk = IDLE;
  Event queued = NONE;
  uint32_t since = 0, clearSince = 0;
  uint8_t mask = 0, target = 1, remaining = 0, prompts = 0;
  bool clearTracking = false, corrected = false, online = false;
  bool inputKnown = false, promptClearSeen = false;
  uint8_t settling = 0, freePrevious = 0, freeSide = 0, freePending = 0;
  bool freeActive = false, freeMoved = false;
  uint32_t freeSeenAt = 0, freeMotionAt = 0;
  int lastPraise = -1, lastReward = -1;
  explicit Controller(Port &p) : io(p) {}
  int choose(int &previous, int count) {
    int n = int(io.random(previous < 0 ? count : count - 1));
    if (previous >= 0 && n >= previous) ++n;
    previous = n; return n;
  }
  void enter(State s, uint32_t now) {
    state = s; since = now; clearTracking = false;
  }
  void say(uint16_t id, State next, uint32_t now) {
    if (next == ARM) promptClearSeen = inputKnown && mask == 0 && settling == 0;
    afterTalk = next; enter(TALK, now); io.play(id);
  }
  void cancel(uint32_t now) {
    freeActive = false; freePending = 0; freePrevious = mask;
    remaining = 0; queued = NONE; afterTalk = IDLE;
    // Keep a playing request in TALK until DONE; never overlap audio requests.
    if (state != TALK) enter(IDLE, now);
    io.stop(); io.face(0);
  }
  void fault(uint32_t now) {
    freeActive = false; freePending = 0; freePrevious = mask;
    online = false; queued = NONE; remaining = 0;
    enter(IDLE, now); io.stop(); io.face(2);
  }
  void event(Event e, uint32_t now) {
    if (!online || e == NONE) return;
    if (state == TALK) {
      // One bounded latest-intent slot, with exit/rest taking priority.
      if (e == EXIT || e == REST) { queued = e; remaining = 0; io.stop(); }
      else if (queued != EXIT && queued != REST) queued = e;
      return;
    }
    remaining = 0; prompts = 0; corrected = false;
    switch (e) {
      case WAKE: remaining = 2; io.head(1); prompt(now, true); break;
      case PRAISE: io.face(6); io.head(4); remaining = 1;
        say(uint16_t(1052 + choose(lastPraise, 3)), BETWEEN, now); break;
      case GAME: case AGAIN: remaining = 3; prompt(now); break;
      case EXIT: io.stop(); io.face(7); say(1024, IDLE, now); break;
      case REST: io.stop(); io.face(4); say(1032, SLEEP, now); break;
      case FORWARD: io.face(8); io.head(2); say(1055, LISTEN, now); break;
      case BACK: io.face(9); io.head(3); say(1056, LISTEN, now); break;
      default: break;
    }
  }
  void prompt(uint32_t now, bool welcome = false) {
    // Start on either cheek, then explicitly guide to the opposite cheek.
    target = prompts ? uint8_t(3 - target) : uint8_t(io.random(2) + 1);
    ++prompts; corrected = false;
    io.face(5);
    say(welcome ? (target == 1 ? 1060 : 1061) : (target == 1 ? 1001 : 1002), ARM, now);
  }
  void done(uint32_t now) {
    if (state != TALK) return;
    enter(afterTalk, now);
    // A fresh approach during the prompt is valid; don't demand another release
    // once the voice finishes. Scoring still happens only after DONE.
    if (state == ARM && promptClearSeen) enter(WAIT_HAND, now);
    if (queued != NONE) { Event e = queued; queued = NONE; event(e, now); }
    else if (state == IDLE) io.face(0);
  }
  bool released(uint32_t now, bool moving) {
    if (mask != 0 || settling != 0 || moving) { clearTracking = false; return false; }
    if (!clearTracking) { clearTracking = true; clearSince = now; }
    return uint32_t(now - clearSince) >= 80;
  }
  void timeout(uint32_t now) {
    remaining = 0; io.face(7); io.stop(); say(1023, IDLE, now);
  }
  void freeTouch(uint32_t now, bool moving) {
    bool allowed = state == IDLE || state == LISTEN || state == ASK_AGAIN || state == SLEEP;
    uint8_t previous = freePrevious; freePrevious = mask;
    if (!allowed) { freeActive = false; freePending = 0; return; }
    if (mask && mask != previous && mask <= 3) {
      if (state == SLEEP) enter(IDLE, now);
      freeSide = mask; freeActive = true; freePending = mask; freeSeenAt = now;
    }
    if (freeActive && mask) {
      freeSeenAt = now;
      if (state == LISTEN) since = now; // Petting also counts as interaction.
      io.face(freeSide == 1 ? 7 : freeSide == 2 ? 6 : 1);
    }
    if (!mask) freePending = 0;
    // At most one latest gesture is retained. Never repeatedly sweep on a hold.
    if (freePending && !moving && (!freeMoved || uint32_t(now-freeMotionAt) >= 900)) {
      io.head(freePending == 1 ? 3 : freePending == 2 ? 2 : 4);
      freePending = 0; freeMoved = true; freeMotionAt = now;
    }
    if (freeActive && !mask && uint32_t(now-freeSeenAt) >= 700) {
      freeActive = false; freeSide = 0; io.face(0);
    }
  }
  void tick(uint32_t now, uint8_t stableMask, bool moving, uint8_t pendingMask = 0) {
    mask = stableMask;
    settling = pendingMask; inputKnown = true;
    freeTouch(now, moving);
    switch (state) {
      case TALK:
        if (afterTalk == ARM && mask == 0 && settling == 0) promptClearSeen = true;
        if (uint32_t(now - since) >= 12000) fault(now);
        break;
      case LISTEN:
        if (uint32_t(now - since) >= 8000) say(1051, IDLE, now);
        break;
      case BETWEEN:
        if (released(now, moving)) prompt(now);
        else if (uint32_t(now - since) >= 8000) timeout(now);
        break;
      case ARM:
        if (released(now, moving)) enter(WAIT_HAND, now);
        else if (uint32_t(now - since) >= 8000) timeout(now);
        break;
      case WAIT_HAND:
        if (uint32_t(now - since) >= 12000) { timeout(now); break; }
        // In the game only, defer judgment while either input is changing.
        if (!mask || mask == 4 || settling) break;
        if (mask == target) {
          io.face(1); io.head(target == 1 ? 3 : 2);
          if (remaining) --remaining;
          say(uint16_t(1010 + choose(lastReward, 3)), RELEASE, now);
        } else if (!corrected) {
          corrected = true; io.face(5);
          say(mask == 3 ? 1021 : 1020, ARM, now);
        } else {
          // A second wrong attempt ends this round. No infinite correction loop.
          timeout(now);
        }
        break;
      case RELEASE:
        if (released(now, moving)) {
          if (remaining) prompt(now);
          else { io.face(7); say(1026, ASK_AGAIN, now); }
        } else if (uint32_t(now - since) >= 8000) timeout(now);
        break;
      case ASK_AGAIN:
        if (uint32_t(now - since) >= 8000) { enter(IDLE, now); io.face(0); }
        break;
      default: break;
    }
  }
};
// Independent inputs: one noisy sensor must not suppress the opposite cheek.
// Fast approach (40ms), slower release (120ms) tolerates brief signal dropouts.
struct Sensors {
  uint8_t candidate = 0, stable = 0, pending = 0;
  uint32_t changed[2] = {0, 0};
  uint8_t update(uint8_t raw, uint32_t now) {
    for (int side=0; side<2; ++side) {
      uint8_t bit = uint8_t(1 << side);
      if ((raw ^ candidate) & bit) { candidate ^= bit; changed[side] = now; }
      uint32_t wait = (candidate & bit) ? 40 : 120;
      if (uint32_t(now-changed[side]) >= wait)
        stable = uint8_t((stable & ~bit) | (candidate & bit));
    }
    pending = uint8_t(raw ^ stable);
    return stable;
  }
};
}
