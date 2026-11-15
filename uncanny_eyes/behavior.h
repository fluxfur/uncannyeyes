// Uncanny Eyes – https://www.youtube.com/@fluxandfur – https://github.com/fluxfur/uncannyeyes
// Copyright (c) 2026 @fluxandfur – MIT License (see LICENSE)
//
// behavior.h – what the eyes do on their own (idle actions, sleep) and how they react to
// people the camera sees (vision behavior). Runs inside eyesTask: call behaviorTick() often.
#pragma once
#include <Arduino.h>

// ---------- idle behavior ----------
const int NUM_ACTS = 7;             // Glance, Look around, Blink, Slow blink, Drowsy, Twitch, Eye roll
struct Behave {
  int on;                           // autonomous behavior on/off (only while nobody is watched)
  int ivMin, ivMax;                 // s between actions (random in range)
  int speed;                        // max speed, degrees per second (automatic movement)
  int glMin;                        // glance: at least this % off-center
  int holdMin, holdMax;             // glance: hold time, ms
  int awake, sleepS;                // s awake before sleeping (0 = never), s asleep
  int w[NUM_ACTS];                  // chance in % per action (0 = off; otherwise nothing this time)
};
extern const Behave DEFAULT_BEH;
extern Behave beh;

// ---------- vision behavior ----------
// Reactions, by event. Each event picks at most one of its reactions by chance:
//   someone arrives   Fast follow / Delayed follow (lags behind) / Double take / Startle
//                     (both then follow fast); otherwise ignored until they leave
//   stands still      Lose interest (moving again = look back); rolled again every stillS
//   close and looking Freeze (turn away = look back) / Shy (lids shut or look away) / Twitch;
//                     otherwise hold eye contact
//   leaves            Stare after / Follow you out (otherwise: drift back right away)
enum { V_FAST, V_DELAY, V_DOUBLE, V_STARTLE, V_BORED, V_FREEZE, V_SHY, V_TWITCH, V_STARE, V_OUT,
       NUM_VACTS };
struct Watch {
  int on;                           // vision behavior active
  int multi;                        // switch between people now and then
  int wake;                         // wake up when someone comes
  int noticeMs;                     // flicker filter: someone new must be seen this long first
  int delayMs;                      // Delayed follow: the eyes look where the person was this long ago
  int stillS;                       // s without moving = standing still
  int closePct;                     // face wider than this % of the view = close (and looking)
  int lostS;                        // Stare after: s to keep staring where the person left
  int speed;                        // follow speed, degrees per second (fine tuning)
  int db;                           // deadband, degrees: smaller target changes are ignored (fine tuning)
  int smoothMs;                     // following smoothing time, ms (0 = instant) (fine tuning)
  int ch[NUM_VACTS];                // chance in % per reaction (0 = off)
  int gainX, gainY;                 // % : eyes move this much per camera position (100 = 1:1)
  int offX, offY;                   // % of the eye range added to the target (+ = right / up)
};
extern const Watch DEFAULT_WATCH;
extern Watch watch;

// action numbers for runAction(): 0..6 idle actions, VACT_BASE + V_* reactions (Run demos),
// sleep, wake
const int VACT_BASE = 20, ACT_SLEEP = 100, ACT_WAKE = 101;

extern int  curAct, stepIdx;
extern bool asleep, manualPause, followTest;
extern uint32_t awakeSince;

void behaviorStart();               // after the settings are loaded: look ahead, schedule
void behaviorTick();
const char* behaviorStatus();       // short text for the page (call with eyeLock held)
bool runAction(int n);              // Run / Sleep now / Wake up buttons
void manualTakeover();              // joystick / sliders: stop, open the eyes, pause 20 s
void endPause();                    // Resume
void setFollowTest(bool on);        // Camera card: Test: fast tracking
void holdPause();                   // keep paused while calibrating (Free ticked)
// after settings changed: tidy up what the old settings left behind
void applyChanges(bool lidsWereOn, int oldAwake, bool wasOn, bool watchWasOn, bool relaxWasOn);
