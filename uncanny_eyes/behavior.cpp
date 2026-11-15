// Uncanny Eyes – https://www.youtube.com/@fluxandfur – https://github.com/fluxfur/uncannyeyes
// Copyright (c) 2026 @fluxandfur – MIT License (see LICENSE)
//
// behavior.cpp – idle actions, sleep, and the vision behavior (camera -> eyes).
#include "behavior.h"
#include "motion.h"
#include "core.h"

// ---------- idle behavior ----------
static const char* ACT_NAMES[NUM_ACTS] = { "Glance", "Look around", "Blink", "Slow blink",
                                           "Drowsy", "Twitch", "Eye roll" };
static const bool ACT_LIDS[NUM_ACTS]     = { 0, 0, 1, 1, 1, 0, 0 };   // needs lids
const Behave DEFAULT_BEH = { 1, 5, 20, 90, 40, 800, 2500, 120, 30, { 35, 15, 25, 10, 5, 5, 5 } };
Behave beh = DEFAULT_BEH;

// ---------- vision behavior ----------
static const char* VACT_NAMES[NUM_VACTS] = { "Fast follow", "Delayed follow", "Double take", "Startle",
                                             "Lose interest", "Freeze", "Shy", "Twitch",
                                             "Stare after", "Follow you out" };
// event: 0 someone arrives, 1 stands still, 2 comes close and looks, 3 leaves
static const int  VACT_EVENT[NUM_VACTS] = { 0, 0, 0, 0, 1, 2, 2, 2, 3, 3 };
//                          on multi wake notice delay still close stare speed db smooth
const Watch DEFAULT_WATCH = { 1,   0,    1,   300,   1800,  5,    12,   3,    120,  1, 300,
                              { 40, 30, 15, 15, 70, 40, 30, 15, 60, 40 }, 100, 100, 0, 0 };
Watch watch = DEFAULT_WATCH;

// Watch modes. The "present" ones (someone is being watched) come last, from W_FOLLOW on.
enum WatchMode { W_IDLE, W_NOTICING, W_IGNORE, W_BORED, W_STARE, W_OUT,
                 W_FOLLOW, W_HIDE, W_PEEK, W_FREEZE };
static const char* WMODE_NAMES[] = { "", "", "ignoring someone", "lost interest",
                                     "staring after them", "following them out",
                                     "watching someone", "hiding", "peeking", "frozen (looked at)" };
// Unseen this long = the person left. Shorter dropouts (low fps, a lost face) are the same
// person, so they never cause a new arrival reaction.
static const uint32_t LEAVE_MS = 1000;
static int  wmode = W_IDLE;
static uint32_t wSince = 0;
static uint32_t lastSeenMs = 0, lastSeq = 0, lastFrameMs = 0;
static float tX = 0, tY = 0, vX = 0;                      // smoothed target, speed (per s)
static float anchorX = 0, anchorY = 0;                    // where the person stands (still detection)
static uint32_t anchorMs = 0;
static bool  movedFlag = false;
static bool  isClose = false, closeEvent = false;         // close = a face, big enough (1 s debounced)
static uint8_t  noFaceFrames = 0;                         // camera frames in a row without a face (only a head)
static uint32_t freezeUntil = 0;                          // Freeze ends at the latest here
static uint32_t closeSince = 0;
static uint32_t hideUntil = 0, peekUntil = 0, outHoldUntil = 0;
static int   outPhase = 0;
static int   nOthers = 0;  static float ox[MAX_OTHERS], oy[MAX_OTHERS]; // other people
static bool  lookingOther = false;
static float otherX = 0, otherY = 0;
static uint32_t otherUntil = 0, nextSwitchMs = 0;
bool followTest = false;                                  // Camera card: "Test: fast tracking"
// How the eyes follow the current person (chosen on arrival, kept until they leave):
// FAST = right away; DELAYED = toward where the person was delayMs ago. Both use the
// Fine tuning settings (follow speed, smoothing, deadband).
enum { FS_FAST, FS_DELAYED };
static int followStyle = FS_FAST;
// target history for Delayed follow: one entry per camera frame
struct Sample { float x, y; uint32_t ms; };
static const int HIST = 256;                              // > 10 s at 25 fps
static Sample hist[HIST];
static int histHead = 0, histCount = 0;

// ---------- action player ----------
// sweep != 0: an arc (eye roll) instead of a glide to (x, y): radius x, starting at angle a0,
// turning by sweep (radians, + = counterclockwise), curling into the center at the end
struct Step { float x, y, lid; int set; uint32_t ms, hold; float a0, sweep; };
static const int MAX_STEPS = 24;
static Step steps[MAX_STEPS];
static int  nSteps = 0;
int  stepIdx = -1;
static bool holding = false;
static uint32_t holdStart = 0;
static float actSpeed = 90;                               // deg/s limit of the running action
static uint32_t arcStart = 0;                             // when the running arc step started
int  curAct = -1;
bool asleep = false;
// timers as "since + duration": overflow-safe, a stale timer just means "long ago"
static uint32_t waitSince = 0, waitMs = 0;                // until the next idle action
uint32_t awakeSince = 0;
static uint32_t asleepSince = 0;
bool manualPause = false;
static uint32_t manualAt = 0;

// =====================================================================
//  Helpers
// =====================================================================

static float frand() { return random(10000) / 10000.0f; }
static int   rnd(int a, int b) { return b <= a ? a : (int)random(a, b + 1); }

static bool actAvailable(int i)  { return !ACT_LIDS[i] || hasLids; }

static const char* actName(int i) {
  if (i == ACT_SLEEP) return "falling asleep";
  if (i == ACT_WAKE)  return "waking up";
  if (i >= VACT_BASE) return VACT_NAMES[i - VACT_BASE];
  return ACT_NAMES[i];
}

static void setWatch(int m) { wmode = m; wSince = millis(); }

static void scheduleNext() { waitSince = millis(); waitMs = rnd(beh.ivMin, beh.ivMax) * 1000UL; }

// Re-apply the current look after settings changed (instant, so calibration changes
// show right away). A running action or glide keeps its smooth move; only the target
// is re-clamped to the new min/max.
static void refreshAngles() {
  if (stepIdx >= 0 || easing) {               // an action or glide is moving: keep it smooth
    tgt[0] = clampAxis(0, tgt[0]);
    tgt[1] = clampAxis(1, tgt[1]);
    return;
  }
  if (wmode == W_FREEZE) return;              // frozen: stay exactly where the eyes are
  if (lookMode && wmode >= W_FOLLOW) {        // following someone: move there smoothly, no jump
    float a, b;
    lookAngles(a, b);
    followTo(a, b, watch.speed);
  } else if (lookMode) snapLook();
  else jumpTo(tgt[0], tgt[1]);
}

// =====================================================================
//  Action player: an action is a list of steps (look at x/y with lid 0..1 in an eye set,
//  move in 'ms' or longer if the speed limit needs it, then hold)
// =====================================================================

static void addStep(float x, float y, float l, int set, uint32_t ms, uint32_t hold) {
  if (nSteps < MAX_STEPS) steps[nSteps++] = { x, y, l, set, ms, hold, 0, 0 };
}

// an arc on a circle of radius r (look units) from angle a0, turning by sweep (radians)
static void addArc(float r, float a0, float sweep, uint32_t ms) {
  if (nSteps < MAX_STEPS) steps[nSteps++] = { r, 0, 0, 0, ms, 0, a0, sweep };
}

// Eye roll size: 95 % of the eye range, but small enough that the circle never touches
// Min/Max (clamped there it would get flat sides and look square)
static float rollRadius() {
  const EyeSet& e = sets[0];
  float r = 0.95f;
  if (e.rx > 0) r = min(r, min(e.cx - min1, max1 - e.cx) / (float)e.rx);
  if (e.ry > 0) r = min(r, min(e.cy - min2, max2 - e.cy) / (float)e.ry);
  return max(r, 0.0f);
}

// Progress 0..1 along the arc for time t (0..1): speeds up during the first 20 %,
// constant speed in the middle, slows down during the last 20 %
static const float ARC_RAMP = 0.2f;
static const float ARC_CURL = 0.7f;                       // from here on the arc curls into the center
static float arcProgress(float t) {
  const float v = 1.0f / (1.0f - ARC_RAMP);               // top speed (average = 1)
  if (t < ARC_RAMP)        return 0.5f * v * t * t / ARC_RAMP;
  if (t > 1.0f - ARC_RAMP) return 1.0f - 0.5f * v * (1.0f - t) * (1.0f - t) / ARC_RAMP;
  return v * (t - 0.5f * ARC_RAMP);
}

static void buildAction(int i) {
  nSteps = 0;
  float x0 = lookX, y0 = lookY;               // blinks happen where the eyes look
  // reactions aim at the person; without one (Run) at a random side point, so the demo shows
  bool live = millis() - lastSeenMs < 1500;
  float px = tX, py = tY;
  if (!live) { px = random(2) ? 0.7f : -0.7f; py = (frand() - 0.5f) * 0.6f; }
  switch (i) {
    case 0: {                                 // Glance: random point, hold, back
      float m = beh.glMin / 100.0f;
      float r = m + frand() * (1.0f - m), an = frand() * TWO_PI;
      bool slow = random(100) < 30;           // sometimes an uncanny slow drift
      addStep(r * cosf(an), r * sinf(an), 0, 0, slow ? rnd(1500, 3000) : rnd(400, 1000),
              rnd(beh.holdMin, beh.holdMax));
      addStep(0, 0, 0, 0, rnd(600, 1500), 0);
      break;
    }
    case 1:                                   // Look around: slow sweep
      addStep(-0.9f, 0, 0, 0, 1500, rnd(400, 900));
      addStep( 0.9f, 0, 0, 0, 3000, rnd(400, 900));
      addStep( 0,    0, 0, 0, 1500, 0);
      break;
    case 2: {                                 // Blink (sometimes double)
      int n = random(100) < 25 ? 2 : 1;
      for (int k = 0; k < n; k++) {
        addStep(x0, y0, 1, 0, 150, 60);
        addStep(x0, y0, 0, 0, 200, 150);
      }
      break;
    }
    case 3:                                   // Slow blink (cat style)
      addStep(x0, y0, 1, 0, 1000, 1200);
      addStep(x0, y0, 0, 0, 1000, 0);
      break;
    case 4:                                   // Drowsy: lids droop, reopen
      addStep(x0, y0, 0.5f, 0, 1500, 800);
      addStep(x0, y0, 0.2f, 0, 600, 500);
      addStep(x0, y0, 0.6f, 0, 1500, 1200);
      addStep(x0, y0, 0,    0, 800, 0);
      break;
    case 5:                                   // Twitch: small nervous jumps
      for (int k = 0; k < 3; k++)
        addStep(x0 + (frand() - 0.5f) * 0.45f, y0 + (frand() - 0.5f) * 0.45f, 0, 0, 80, 120);
      addStep(x0, y0, 0, 0, 150, 0);
      break;
    case 6: {                                 // Eye roll: about one round, curling into the center
      float r = rollRadius();
      float a0 = frand() * TWO_PI;                          // random start point on the circle
      float sweep = (300 + frand() * 120) * DEG_TO_RAD;     // 300..420 degrees around
      if (random(2)) sweep = -sweep;                        // either direction
      // duration: the top speed stays at the max speed (at least 1.5 s)
      const EyeSet& e = sets[0];
      float A = r * e.rx, B = r * e.ry;                      // circle radii in degrees
      float R = sqrtf((A * A + B * B) * 0.5f);
      float len = fabsf(sweep) * R * (ARC_CURL + (1 - ARC_CURL) * 0.5f);  // path length (about)
      uint32_t ms = (uint32_t)(len / beh.speed * 1000.0f / (1.0f - ARC_RAMP));
      addStep(r * cosf(a0), r * sinf(a0), 0, 0, 600, 0);    // to the start point
      addArc(r, a0, sweep, max(ms, (uint32_t)1500));        // ends in the center
      break;
    }
    case VACT_BASE + V_FAST:                  // demo: there at once, at full servo speed
      addStep(px, py, 0, 0, 0, 1500);
      break;
    case VACT_BASE + V_DELAY:                 // demo: nothing ... then there, late
      addStep(x0, y0, 0, 0, 0, (uint32_t)watch.delayMs);
      addStep(px, py, 0, 0, 600, 1500);
      break;
    case VACT_BASE + V_DOUBLE:                // look, look away, snap back
      addStep(px, py, 0, 0, 250, 300);
      addStep(px * 0.3f, -0.3f, 0, 0, 350, 500);
      addStep(px, py, 0, 0, 150, 700);
      break;
    case VACT_BASE + V_STARTLE: {             // jump well past them, bounce back, settle
      float dx = px - x0, dy = py - y0, d = sqrtf(dx * dx + dy * dy);
      if (d < 0.2f) { dx = px >= 0 ? 1 : -1; dy = 0.3f; d = sqrtf(dx * dx + dy * dy); }
      dx /= d; dy /= d;
      addStep(px + dx * 0.6f, py + dy * 0.6f + 0.15f, 0, 0, 90, 250);
      addStep(px - dx * 0.15f, py - dy * 0.15f, 0, 0, 180, 80);
      addStep(px, py, 0, 0, 250, 300);
      break;
    }
    case VACT_BASE + V_BORED:                 // demo: look away, then look back
      addStep(px > 0 ? -0.7f : 0.7f, -0.3f, 0, 0, 2500, 2500);
      addStep(px, py, 0, 0, 200, 0);
      break;
    case VACT_BASE + V_FREEZE:                // demo: stop dead, then look back at them
      addStep(x0, y0, 0, 0, 0, 3000);
      addStep(px, py, 0, 0, 200, 0);
      break;
    case VACT_BASE + V_SHY:                   // lids: shut for a moment; no lids: look away, back
      if (hasLids) {
        addStep(px, py, 1, 0, 600, 3000);
        addStep(px, py, 0, 0, 800, 0);
      } else {
        addStep(px > 0 ? -0.7f : 0.7f, -0.4f, 0, 0, 500, rnd(1000, 2000));
        addStep(px, py, 0, 0, 800, 0);
      }
      break;
    case VACT_BASE + V_TWITCH:                // big nervous jumps around the person
      for (int k = 0; k < 4; k++)
        addStep(px + (frand() - 0.5f) * 0.55f, py + (frand() - 0.5f) * 0.55f, 0, 0, 70, 110);
      addStep(px, py, 0, 0, 150, 0);
      break;
    case VACT_BASE + V_STARE:                 // demo: stare, then drift back
      addStep(px, py, 0, 0, 600, 2500);
      addStep(0, 0, 0, 0, 2500, 0);
      break;
    case VACT_BASE + V_OUT:                   // demo: follow past the edge, hold, turn back slowly
      addStep(px, py, 0, 0, 500, 300);
      addStep(px >= 0 ? 0.95f : -0.95f, py, 0, 0, 700, 2000);
      addStep(0, 0, 0, 0, 4000, 0);
      break;
    case ACT_SLEEP:                           // fall asleep: droop, close
      addStep(x0, y0, 0.5f, 0, 1500, 600);
      addStep(x0, y0, 0.3f, 0, 500, 400);
      addStep(x0, y0, 1,    0, 2000, 0);
      break;
    case ACT_WAKE:                            // wake with a startle
      addStep(x0,   y0, 0, 0, 250, 150);
      addStep(-0.6f, 0, 0, 0, 300, 150);
      addStep( 0.6f, 0, 0, 0, 450, 150);
      addStep( 0,    0, 0, 0, 300, 0);
      break;
  }
}

static void finishAction() {
  uint32_t now = millis();
  if (curAct == ACT_SLEEP) { asleep = true;  asleepSince = now; }
  if (curAct == ACT_WAKE)  { asleep = false; awakeSince  = now; }
  curAct = -1;
  stepIdx = -1;
  scheduleNext();
}

static void startStep() {
  if (stepIdx >= nSteps) { finishAction(); return; }
  const Step& s = steps[stepIdx];
  curSet = (s.set == 1 && !hasLids) ? 0 : s.set;
  lookX = s.x;  lookY = s.y;  lid = s.lid;
  lookMode = true;
  if (s.sweep != 0) { arcStart = millis(); holding = false; return; }   // moved by playSteps
  float a, b;
  lookAngles(a, b);
  glideTo(a, b, s.ms, actSpeed);
  holding = false;
}

static void startAction(int i) {
  if (i < 0) { scheduleNext(); return; }
  if (i < NUM_ACTS && !actAvailable(i)) return;
  if ((i == ACT_SLEEP || i == ACT_WAKE) && !hasLids) return;
  curAct = i;
  // the sudden reactions may be faster than the max speed (that's the point)
  bool fast = i == VACT_BASE + V_DOUBLE || i == VACT_BASE + V_STARTLE || i == VACT_BASE + V_TWITCH ||
              i == VACT_BASE + V_FREEZE;
  bool snap = i == VACT_BASE + V_FAST;       // as fast as the servos go
  actSpeed = snap ? 600.0f : fast ? max((float)beh.speed, 400.0f) : (float)beh.speed;
  buildAction(i);
  stepIdx = 0;
  startStep();
  logBehavior("action: %s\n", actName(i));
}

static void cancelAction() { curAct = -1; stepIdx = -1; }

// stop a running glide where the eyes are now
static void stopGlide() { easing = false; tgt[0] = pos[0]; tgt[1] = pos[1]; }

static void playSteps(uint32_t now) {
  const Step& s = steps[stepIdx];
  if (s.sweep != 0 && !holding) {             // eye roll: the target runs along the arc,
    float t = (float)(now - arcStart) / s.ms; // the servos follow it closely
    if (t >= 1.0f) { t = 1.0f; holding = true; holdStart = now; }
    float p = arcProgress(t);
    float r = s.x;
    if (p > ARC_CURL) {                         // last part: the radius shrinks smoothly to 0
      float k = (p - ARC_CURL) / (1.0f - ARC_CURL);
      r *= 1.0f - k * k * (3.0f - 2.0f * k);
    }
    float an = s.a0 + s.sweep * p;
    lookX = r * cosf(an);
    lookY = r * sinf(an);
    float a, b;
    lookAngles(a, b);
    followTo(a, b, 600.0f);
    return;
  }
  if (!holding) {
    if (!easing) { holding = true; holdStart = now; }
  } else if (now - holdStart >= steps[stepIdx].hold) {
    stepIdx++;
    startStep();
  }
}

// One action by chance (like the vision reactions); the rest of the chance = nothing this time.
static void pickAction() {
  int r = random(100), acc = 0;
  for (int i = 0; i < NUM_ACTS; i++) {
    if (!actAvailable(i)) continue;
    acc += beh.w[i];
    if (r < acc) { startAction(i); return; }
  }
  scheduleNext();
}

// =====================================================================
//  Manual control, pause
// =====================================================================

void manualTakeover() {
  cancelAction();
  asleep = false;
  if (lid > 0) {                              // interrupted mid-blink / asleep: open the eyes
    lid = 0;
    if (lookMode) snapLook();
  }
  manualPause = true;
  manualAt = millis();
  awakeSince = millis();
}

void setFollowTest(bool on) {
  followTest = on;
  if (!on) return;
  cancelAction();                             // also ends sleep: the test only follows
  asleep = false;
  awakeSince = millis();
  endPause();
}

void endPause() {
  manualPause = false;
  waitSince = millis();
  waitMs = 500;                               // first action right away
  awakeSince = millis();
}

// A running action (Run / Sleep now while calibrating) may finish; the
// joystick still stops it.
void holdPause() {
  if (curAct >= 0 || asleep) { manualPause = true; manualAt = millis(); }
  else if (!manualPause) manualTakeover();
  else manualAt = millis();
}

static void startFollowing(uint32_t now, int style, int reaction, bool busy);
static int  arrivalStyle(int r) { return r == V_DELAY ? FS_DELAYED : FS_FAST; }

bool runAction(int n) {
  if (!((n >= 0 && n < NUM_ACTS) || (n >= VACT_BASE && n < VACT_BASE + NUM_VACTS) ||
        n == ACT_SLEEP || n == ACT_WAKE)) return false;
  cancelAction();
  manualPause = false;
  uint32_t now = millis();
  if (n != ACT_SLEEP && n != ACT_WAKE) { asleep = false; awakeSince = now; }
  // arrival reactions with someone in view (and vision active): the real thing, so the
  // following that comes after it shows too
  int v = n - VACT_BASE;
  if (watch.on && !followTest && now - lastSeenMs < LEAVE_MS &&
      (v == V_FAST || v == V_DELAY || v == V_DOUBLE || v == V_STARTLE)) {
    logBehavior("run: %s\n", VACT_NAMES[v]);
    startFollowing(now, arrivalStyle(v), v, false);
    return true;
  }
  startAction(n);
  return true;
}

void behaviorStart() {
  lookAt(0, 0, beh.speed);                    // main eyes, straight ahead (no ramp: position unknown)
  pos[0] = tgt[0];  pos[1] = tgt[1];
  writeOut();
  scheduleNext();
  awakeSince = millis();
}

// =====================================================================
//  Vision behavior: camera -> eyes
//
//  Someone arrives (seen longer than "Ignore flickers shorter than"): one arrival reaction by
//  chance, all of them end up following; no reaction = the person is ignored until they leave.
//  While followed: standing still -> maybe lose interest (moving again = look back), coming
//  close and looking at the painting (a big enough face) -> maybe freeze, shy or twitch,
//  leaving -> maybe stare after them or follow them out.
// =====================================================================

// Picks a reaction for an event (0 arrives, 1 stands still, 2 comes close, 3 leaves) by the
// chances set on the page; -1 = no reaction this time.
static int rollReaction(int ev) {
  int r = random(100), acc = 0;
  for (int i = 0; i < NUM_VACTS; i++) {
    if (VACT_EVENT[i] != ev) continue;
    acc += watch.ch[i];
    if (r < acc) return i;
  }
  return -1;
}

// Smooth following. The camera delivers a new target only a few times per second; steering
// straight at each one makes the eyes stop and go (choppy). Instead an aim point glides
// toward the target every tick (exponential, time constant watch.smoothMs) and the servos follow
// the aim point, limited to the follow speed. Target changes smaller than the deadband
// (degrees) don't move the aim point at all (no jitter, no humming).
static float aimX = 0, aimY = 0, goalX = 0, goalY = 0;
static uint32_t aimMs = 0;

static void smoothFollowEx(float x, float y, float l, float speed, float deadband, int tauMs) {
  uint32_t now = millis();
  x = constrain(x, -1.0f, 1.0f);
  y = constrain(y, -1.0f, 1.0f);
  if (now - aimMs > 200 || curSet != 0 || !lookMode) {    // not following just now: start from the eyes
    aimX = goalX = lookX;
    aimY = goalY = lookY;
    aimMs = now;
  }
  float dt = min((now - aimMs) / 1000.0f, 0.1f);
  aimMs = now;
  if (fabsf(x - goalX) * sets[0].rx >= deadband || fabsf(y - goalY) * sets[0].ry >= deadband) {
    goalX = x;
    goalY = y;
  }
  float k = tauMs > 0 ? 1.0f - expf(-dt * 1000.0f / tauMs) : 1.0f;   // 0 = instant
  aimX += (goalX - aimX) * k;
  aimY += (goalY - aimY) * k;
  curSet = 0;
  lookMode = true;
  lookX = aimX;
  lookY = aimY;
  lid = l;
  float a, b;
  lookAngles(a, b);
  followTo(a, b, speed);
}

// following with the Fine tuning settings
static void smoothFollow(float x, float y, float l) {
  smoothFollowEx(x, y, l, watch.speed, watch.db, watch.smoothMs);
}

static void startHide(uint32_t now) {
  logBehavior("person: close and looking -> hide\n");
  setWatch(W_HIDE);
  glideLook(tX, tY, 1, 600, beh.speed);
  hideUntil = now + 600 + rnd(2000, 4000);
}

// Close and looking -> Freeze: stop dead where the eyes are; they move again when the face
// is gone (turned away) -> look back, or after 15 s at the latest
static void startFreeze(uint32_t now) {
  logBehavior("person: close and looking -> freeze\n");
  stopGlide();
  freezeUntil = now + 15000;
  setWatch(W_FREEZE);
}

// Close and looking -> Shy: with lids hide behind them (peek, maybe hide again),
// without lids look away for a moment (an action; following goes on after it)
static void startShy(uint32_t now) {
  if (hasLids) { startHide(now); return; }
  logBehavior("person: close and looking -> look away\n");
  startAction(VACT_BASE + V_SHY);
}

// back to following after Freeze / Hide / Peek: they usually stood still meanwhile,
// so the stillness timer starts again (no "Lose interest" right away)
static void backToFollow(uint32_t now) {
  setWatch(W_FOLLOW);
  anchorX = tX; anchorY = tY; anchorMs = now;
  movedFlag = false;
}

// nobody there any more: drift back to the middle, normal behavior again
static void gone(bool busy) {
  logBehavior("person: gone\n");
  setWatch(W_IDLE);
  lookingOther = false;
  if (!busy) glideLook(0, 0, 0, 2500, beh.speed);
  scheduleNext();
}

// Test: fast tracking: the raw target, full servo speed, no deadband, a little smoothing (80 ms)
static void directFollow(float x, float y) { smoothFollowEx(x, y, 0, 600, 0, 80); }

// Where the person was delayMs ago (false = no sample that old yet: hold still)
static bool delayedTarget(uint32_t now, float& x, float& y) {
  for (int k = 0; k < histCount; k++) {
    const Sample& s = hist[(histHead - 1 - k + HIST) % HIST];   // newest first
    if (now - s.ms >= (uint32_t)watch.delayMs) { x = s.x; y = s.y; return true; }
  }
  if (histCount == HIST) {                            // delay longer than the history: oldest
    const Sample& s = hist[histHead];
    x = s.x; y = s.y;
    return true;
  }
  return false;
}

// One follow step for the current person in the chosen style
static void followPerson(uint32_t now) {
  if (followStyle == FS_DELAYED) {
    float x, y;
    if (delayedTarget(now, x, y)) smoothFollow(x, y, 0);
    return;
  }
  smoothFollow(tX, tY, 0);                          // fast: the newest (lightly smoothed) target
}

// The eyes turn to someone and follow them in 'style' (fast or delayed; after Double take /
// Startle if 'reaction' is one of them). style -1 = keep the current style (look back).
static void startFollowing(uint32_t now, int style, int reaction, bool busy) {
  if (style >= 0) followStyle = style;
  anchorX = tX; anchorY = tY; anchorMs = now;
  movedFlag = false;
  awakeSince = now;
  lookingOther = false;
  nextSwitchMs = now + rnd(3000, 8000);
  bool react = reaction == V_DOUBLE || reaction == V_STARTLE;
  if (easing && stepIdx < 0) stopGlide();     // a glide back (after someone left, lost interest): stop it
  if (busy && curAct != ACT_WAKE) {          // someone to watch: idle actions stop right away ...
    cancelAction();
    stopGlide();                              // ... including their glide
    if (lid > 0) glideLook(lookX, lookY, 0, 300, beh.speed);   // interrupted mid-blink: open the eyes
  }
  setWatch(W_FOLLOW);
  if (react && curAct < 0) startAction(VACT_BASE + reaction);   // its steps also reopen the lids
}

// Reads the latest target, detects the events and steers the eyes. Returns true while the
// eyes attend to someone.
static bool watchTick(uint32_t now) {
  TrackerState t;
  portENTER_CRITICAL(&trkMux);
  t = trk;
  portEXIT_CRITICAL(&trkMux);
  // Aim: eye position = camera position x gain + offset, per axis (limited by the eye range anyway)
  float gx = watch.gainX / 100.0f, gy = watch.gainY / 100.0f;
  float offX = watch.offX / 100.0f, offY = watch.offY / 100.0f;
  t.x  = t.x  * gx + offX;  t.y  = t.y  * gy + offY;
  t.rx = t.rx * gx + offX;  t.ry = t.ry * gy + offY;
  for (int k = 0; k < MAX_OTHERS; k++) { t.ox[k] = t.ox[k] * gx + offX; t.oy[k] = t.oy[k] * gy + offY; }
  // older = the vision loop stalled. Signed: loop() on the other core may have stamped t.ms
  // a moment after 'now' was read, which unsigned would turn into "very old".
  bool seen = t.src != 0 && (int32_t)(now - t.ms) < 1500;

  if (seen && t.seq != lastSeq) {                   // a new camera frame with a target
    lastSeq = t.seq;
    if (now - lastSeenMs >= LEAVE_MS) {             // (new) person: start fresh
      tX = t.x; tY = t.y; vX = 0;
      histCount = 0;
      anchorX = tX; anchorY = tY; anchorMs = now;
      movedFlag = false;
      isClose = closeEvent = false;
      closeSince = now;
    } else {                                        // smooth the jitter a little
      float nx = tX + 0.5f * (t.x - tX);
      float dt = (now - lastFrameMs) / 1000.0f;
      if (dt > 0.01f && dt < 1.0f) vX += 0.3f * ((nx - tX) / dt - vX);
      tX = nx;
      tY += 0.5f * (t.y - tY);
    }
    lastFrameMs = now;
    hist[histHead] = { tX, tY, now };               // for Delayed follow
    histHead = (histHead + 1) % HIST;
    if (histCount < HIST) histCount++;
    // moved more than ~6 % of the view: not standing still (any more)
    float dx = (tX - anchorX) / gx, dy = (tY - anchorY) / gy;   // in camera terms (Aim left out)
    if (dx * dx + dy * dy > 0.12f * 0.12f) {
      anchorX = tX; anchorY = tY; anchorMs = now;
      movedFlag = true;
    }
    // close = a face (they look at the painting) wider than closePct of the view, debounced by 1 s
    if (t.src == 1) noFaceFrames = 0;
    else if (noFaceFrames < 255) noFaceFrames++;
    bool closeNow = t.src == 1 && t.size * 100.0f >= watch.closePct;
    if (closeNow == isClose) closeSince = now;
    else if (now - closeSince >= 1000) { isClose = closeNow; if (isClose) closeEvent = true; }
    nOthers = min(t.nOthers, MAX_OTHERS);
    for (int k = 0; k < nOthers; k++) { ox[k] = t.ox[k]; oy[k] = t.oy[k]; }
  }
  if (seen) lastSeenMs = now;
  else nOthers = 0;

  if (manualPause) return false;                    // paused: keep the mode, the person isn't "new" afterwards
  if (followTest) {                                 // test: fast tracking, nothing else
    if (seen && stepIdx < 0 && !asleep) directFollow(t.rx, t.ry);
    return true;
  }
  if (!watch.on) { if (wmode != W_IDLE) setWatch(W_IDLE); return false; }

  // Sleeping (or falling asleep): only a newcomer wakes the eyes. Someone who was already
  // there (e.g. you, pressing "Sleep now") doesn't.
  if (asleep || curAct == ACT_SLEEP) {
    switch (wmode) {
      case W_IDLE:
        if (seen) setWatch(W_NOTICING);
        break;
      case W_NOTICING:
        if (!seen) { if (now - lastSeenMs > 500) setWatch(W_IDLE); }
        else if (curAct == ACT_WAKE) {
          // waking up anyway: stay noticing, the normal arrival roll runs once awake
        } else if (now - wSince >= (uint32_t)watch.noticeMs) {
          if (watch.wake) {
            logBehavior("person: arrived -> wake up\n");
            cancelAction();
            startAction(ACT_WAKE);                  // the wake-up startle is the arrival reaction
            startFollowing(now, FS_FAST, -1, true);
          } else {
            setWatch(W_IGNORE);                     // noticed in its sleep
          }
        }
        break;
      default:                                      // they leave while the eyes sleep
        if (!seen && now - lastSeenMs >= LEAVE_MS) setWatch(W_IDLE);
        break;
    }
    return false;
  }

  bool busy = stepIdx >= 0;
  switch (wmode) {
    case W_IDLE:
      if (seen) setWatch(W_NOTICING);
      return false;

    case W_NOTICING: {                              // must be seen a moment (filters flickers)
      if (!seen) { if (now - lastSeenMs > 500) setWatch(W_IDLE); return false; }
      awakeSince = now;
      if (now - wSince < (uint32_t)watch.noticeMs) return false;
      int r = rollReaction(0);
      if (r < 0) {
        logBehavior("person: arrived -> ignored\n");
        setWatch(W_IGNORE);
        return false;
      }
      logBehavior("person: arrived -> %s\n", VACT_NAMES[r]);
      startFollowing(now, arrivalStyle(r), r, busy);
      return true;
    }

    case W_IGNORE:                                  // not interesting: ignored until they leave
      if (!seen && now - lastSeenMs >= LEAVE_MS) setWatch(W_IDLE);
      return false;

    case W_BORED:                                   // lost interest; moving again = look back
      if (!seen) { if (now - lastSeenMs >= LEAVE_MS) setWatch(W_IDLE); return false; }
      if (!movedFlag) return false;                 // idle behavior goes on meanwhile
      logBehavior("person: moved -> look back\n");
      if (busy) cancelAction();
      startFollowing(now, -1, -1, false);             // same style as before ...
      glideLook(tX, tY, 0, 200, max((float)beh.speed, 400.0f));   // ... after a snap back to them
      return true;

    case W_STARE:                                   // they left: stare where they were
      if (seen) { setWatch(W_FOLLOW); return true; }   // back again: no new arrival, same style
      if (now - lastSeenMs < (uint32_t)watch.lostS * 1000) return true;
      gone(busy);
      return false;

    case W_OUT:                                     // they left: follow past the edge, then turn back
      if (seen) { setWatch(W_FOLLOW); return true; }
      if (busy || easing) return true;
      if (outPhase == 0) { outPhase = 1; outHoldUntil = now + rnd(1500, 2500); return true; }
      if ((int32_t)(now - outHoldUntil) < 0) return true;
      logBehavior("person: gone\n");
      setWatch(W_IDLE);
      glideLook(0, 0, 0, 4000, beh.speed);          // slow turn back, as if listening
      scheduleNext();
      return false;
  }

  // FOLLOW, HIDE, PEEK, FREEZE: someone is being watched
  if (!seen) {
    if (now - lastSeenMs < LEAVE_MS) return true;
    lookingOther = false;
    int r = rollReaction(3);
    if (busy && r >= 0) r = V_STARE;                // can't move now: just stare
    if (r == V_OUT) {
      // keep going in their walking direction, past the spot where they were lost
      float ex = fabsf(vX) > 0.2f ? tX + vX * 0.8f : tX + (tX >= 0 ? 0.3f : -0.3f);
      logBehavior("person: left -> follow you out\n");
      glideLook(constrain(ex, -0.95f, 0.95f), tY, 0, 700, beh.speed);
      outPhase = 0;
      setWatch(W_OUT);
      return true;
    }
    if (r == V_STARE) {
      logBehavior("person: left -> stare after\n");
      if (wmode != W_FOLLOW && !busy) glideLook(tX, tY, 0, 1000, beh.speed);   // open, look where they were
      setWatch(W_STARE);
      return true;
    }
    gone(busy);
    return false;
  }
  awakeSince = now;                                 // nobody falls asleep while watched

  switch (wmode) {
    case W_FOLLOW: {
      if (busy || easing) return true;              // let a reaction / blink / glide finish
      if (closeEvent && !isClose) closeEvent = false;   // stale: they backed off meanwhile
      if (closeEvent) {
        closeEvent = false;
        int r = rollReaction(2);
        if (r == V_FREEZE) { startFreeze(now); return true; }
        if (r == V_SHY) { startShy(now); return true; }
        if (r == V_TWITCH) { logBehavior("person: close and looking -> twitch\n"); startAction(VACT_BASE + V_TWITCH); return true; }
      }
      if (now - anchorMs >= (uint32_t)watch.stillS * 1000) {
        anchorMs = now;                             // still: roll again after another stillS
        if (rollReaction(1) == V_BORED) {
          logBehavior("person: still -> lost interest\n");
          movedFlag = false;
          lookingOther = false;
          glideLook(0, 0, 0, 2500, beh.speed);      // slowly back to the center ...
          setWatch(W_BORED);
          waitSince = now;                          // ... then (with Autonomous on) an idle action soon after
          waitMs = 2500 + rnd(500, 1500);
          return false;
        }
      }
      if (watch.multi) {                            // switch between people now and then
        if (lookingOther) {
          if (nOthers == 0 || (int32_t)(now - otherUntil) >= 0) {
            lookingOther = false;
            nextSwitchMs = now + rnd(3000, 9000);
          } else {
            int best = 0;                           // the same other person: nearest to the last spot
            float bd = 99;
            for (int k = 0; k < nOthers; k++) {
              float d = fabsf(ox[k] - otherX) + fabsf(oy[k] - otherY);
              if (d < bd) { bd = d; best = k; }
            }
            otherX += 0.5f * (ox[best] - otherX);
            otherY += 0.5f * (oy[best] - otherY);
            smoothFollow(otherX, otherY, 0);
            return true;
          }
        } else if (nOthers > 0 && (int32_t)(now - nextSwitchMs) >= 0) {
          int k = random(nOthers);
          otherX = ox[k]; otherY = oy[k];
          lookingOther = true;
          otherUntil = now + rnd(1500, 4000);
          logBehavior("person: looking at someone else\n");
          smoothFollow(otherX, otherY, 0);
          return true;
        }
      }
      followPerson(now);
      return true;
    }

    case W_HIDE:                                    // eyes shut for a moment
      if (busy || easing || (int32_t)(now - hideUntil) < 0) return true;
      glideLook(tX, tY, 0, 800, beh.speed);         // peek
      setWatch(W_PEEK);
      peekUntil = now + 800 + rnd(1500, 2500);
      return true;

    case W_PEEK:                                    // open again: still very close? maybe hide again
      if (busy || easing) return true;
      if ((int32_t)(now - peekUntil) < 0) { followPerson(now); return true; }
      if (isClose && hasLids && random(100) < watch.ch[V_SHY]) { startHide(now); return true; }
      backToFollow(now);
      return true;

    case W_FREEZE: {                                // frozen while they look; turned away = look back
      if (busy) return true;
      bool timeUp = (int32_t)(now - freezeUntil) >= 0;
      if (noFaceFrames < 2 && !timeUp) return true; // still looked at (2 frames without a face = away)
      logBehavior(timeUp ? "person: frozen long enough -> follow\n" : "person: looked away -> look back\n");
      glideLook(tX, tY, 0, 200, max((float)beh.speed, 400.0f));
      backToFollow(now);
      return true;
    }
  }
  return true;
}

// =====================================================================
//  Tick + status
// =====================================================================

void behaviorTick() {
  uint32_t now = millis();
  bool attending = watchTick(now);
  if (stepIdx >= 0) { playSteps(now); return; }
  if (manualPause && now - manualAt >= MANUAL_PAUSE_MS) { manualPause = false; awakeSince = now; scheduleNext(); }
  if (asleep && !manualPause) {                         // also with Autonomous off ("Sleep now")
    if (now - asleepSince >= beh.sleepS * 1000UL) startAction(ACT_WAKE);
    return;
  }
  if (followTest) return;
  if (!beh.on || manualPause) return;
  if (attending) return;                                // someone is watched: no idle actions, no sleep
  if (beh.awake > 0 && hasLids && now - awakeSince >= beh.awake * 1000UL) { startAction(ACT_SLEEP); return; }
  if (now - waitSince >= waitMs) pickAction();
}

const char* behaviorStatus() {
  static char buf[48];
  if (curAct >= 0) return actName(curAct);
  if (asleep) return "asleep";
  if (manualPause) {
    uint32_t el = millis() - manualAt;
    if (el > MANUAL_PAUSE_MS) el = MANUAL_PAUSE_MS;
    snprintf(buf, sizeof(buf), "paused (manual, %u s)", (unsigned)((MANUAL_PAUSE_MS - el + 999) / 1000));
    return buf;
  }
  if (followTest) return "test: fast tracking";
  if (wmode == W_FOLLOW && lookingOther) return "looking at someone else";
  if (wmode == W_FOLLOW) {
    return followStyle == FS_DELAYED ? "following (delayed)" : "following (fast)";
  }
  if (wmode >= W_IGNORE) return WMODE_NAMES[wmode];
  if (!beh.on && !watch.on) return "all off";
  if (!beh.on) return "waiting for people";
  return "idle";
}

// =====================================================================
//  After settings changed (page preview, Save, or reverting to the saved ones)
// =====================================================================

void applyChanges(bool lidsWereOn, int oldAwake, bool wasOn, bool watchWasOn, bool relaxWasOn) {
  if (beh.awake != oldAwake || !lidsWereOn) awakeSince = millis();   // restart the awake time
  if (!wasOn && beh.on) endPause();           // switched on: start right away
  if (!hasLids && (asleep || lid > 0 || curAct == ACT_SLEEP || curAct == ACT_WAKE)) {
    asleep = false; cancelAction();           // lids removed: eyes open, no lid moods
    glideLook(lookX, lookY, 0, 500, beh.speed);
    if (wmode == W_HIDE || wmode == W_PEEK) setWatch(W_FOLLOW);
  }
  if (watchWasOn && !watch.on && wmode != W_IDLE) {   // vision switched off: open the eyes
    setWatch(W_IDLE);
    lookingOther = false;
    if (stepIdx < 0 && (lid > 0 || easing)) glideLook(lookX, lookY, 0, 500, beh.speed);
  }
  if (wasOn && !beh.on) {                     // autonomous switched off: stop, open, back to the center
    cancelAction();
    asleep = false;
    if (wmode == W_HIDE || wmode == W_PEEK) setWatch(W_FOLLOW);
    glideLook(0, 0, 0, 800, beh.speed);       // smooth, eased (refreshAngles below keeps it)
  }
  if (relaxWasOn && !relaxOn && relaxed) {    // relax switched off: hold the position again
    lastDuty[0] = lastDuty[1] = 0xFFFFFFFF;
    writeOut();
  }
  refreshAngles();
}
