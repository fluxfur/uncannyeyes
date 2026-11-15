// Uncanny Eyes – https://www.youtube.com/@fluxandfur – https://github.com/fluxfur/uncannyeyes
// Copyright (c) 2026 @fluxandfur – MIT License (see LICENSE)
//
// motion.cpp – servo layer and eye geometry.
#include "motion.h"

// LEDC channels 4/5 run on LEDC timer 2, clear of the camera clock (timer 0)
static const int SERVO1_CH = 4;
static const int SERVO2_CH = 5;

// PWM: 50 Hz, 14-bit (1.2 us steps -> smooth slow moves)
static const int PWM_FREQ = 50;
static const int PWM_RES  = 14;

int  min1 = 0, max1 = 180, min2 = 0, max2 = 180;
bool inv1 = true, inv2 = true;            // default: both reversed

const EyeSet DEFAULT_SETS[NUM_SETS] = {
  { 90,  90, 30, 20 },   // Main
  { 90, 130,  0,  0 },   // Lids (adjust the Y center to where the lids sit)
};
EyeSet sets[NUM_SETS] = { DEFAULT_SETS[0], DEFAULT_SETS[1] };
bool hasLids = true;

bool relaxOn = true, relaxed = false;
int  relaxMs = 1500;
uint32_t lastMoveMs = 0;

float pos[2] = { 90, 90 };
float tgt[2] = { 90, 90 };
static float from[2] = { 90, 90 };
bool  easing = false;
static float curSpeed = 90;               // deg/s for follow moves
static uint32_t mvStart = 0, mvDur = 0, lastTick = 0;
uint32_t lastDuty[2] = { 0xFFFFFFFF, 0xFFFFFFFF };

int   curSet = 0;
bool  lookMode = true;
float lookX = 0, lookY = 0;
float lid = 0;

// =====================================================================
//  Servo layer
// =====================================================================

void attachServos() {
  ledcSetup(SERVO1_CH, PWM_FREQ, PWM_RES);
  ledcAttachPin(SERVO1_PIN, SERVO1_CH);
  ledcSetup(SERVO2_CH, PWM_FREQ, PWM_RES);
  ledcAttachPin(SERVO2_PIN, SERVO2_CH);
}

// n = 1 or 2; duty 0 = no pulses
static void writeDuty(int n, uint32_t duty) {
  ledcWrite(n == 1 ? SERVO1_CH : SERVO2_CH, duty);
}

// Sends pos[] when it changed. Only real changes count as movement: they
// restart the relax timer and wake relaxed servos (both get pulses again).
void writeOut() {
  uint32_t d[2];
  for (int i = 0; i < 2; i++) {
    float a = constrain(pos[i], 0.0f, 180.0f);
    float us = PULSE_MIN_US + a * (PULSE_MAX_US - PULSE_MIN_US) / 180.0f;
    d[i] = (uint32_t)lroundf(us * (1UL << PWM_RES) / 20000.0f);   // 20 ms period
  }
  if (d[0] == lastDuty[0] && d[1] == lastDuty[1]) return;
  for (int i = 0; i < 2; i++) { writeDuty(i + 1, d[i]); lastDuty[i] = d[i]; }
  relaxed = false;
  lastMoveMs = millis();
}

void relaxServos() {
  writeDuty(1, 0);
  writeDuty(2, 0);
  relaxed = true;
}

// Min/Max limit the Main eyes only. The Lids position (and the way there while the lids
// close) may lie outside that range; it is set once in calibration, so only 0..180 applies.
static bool mainLimits() { return !(hasLids && (curSet == 1 || lid > 0)); }

float clampAxis(int i, float a) {
  if (!mainLimits()) return constrain(a, 0.0f, 180.0f);
  return i == 0 ? constrain(a, (float)min1, (float)max1)
                : constrain(a, (float)min2, (float)max2);
}

void glideTo(float a, float b, uint32_t ms, float speed) {
  float t[2] = { clampAxis(0, a), clampAxis(1, b) };
  float dist = max(fabsf(t[0] - pos[0]), fabsf(t[1] - pos[1]));
  // cosine easing peaks at pi/2 x the average speed
  uint32_t minMs = (uint32_t)(dist / speed * 1000.0f * 1.5708f);
  for (int i = 0; i < 2; i++) { from[i] = pos[i]; tgt[i] = t[i]; }
  mvStart = millis();
  mvDur = max(ms, minMs);
  easing = true;
}

void followTo(float a, float b, float speed) {
  tgt[0] = clampAxis(0, a);
  tgt[1] = clampAxis(1, b);
  curSpeed = speed;
  easing = false;
}

// Manual control: go there right away, no speed limit, no waiting for the
// next 20 ms motion update.
void jumpTo(float a, float b) {
  tgt[0] = pos[0] = clampAxis(0, a);
  tgt[1] = pos[1] = clampAxis(1, b);
  easing = false;
  writeOut();
}

void updateMotion() {
  uint32_t now = millis();
  if (now - lastTick < 20) return;                     // 50 updates per second
  float dt = min((now - lastTick) / 1000.0f, 0.1f);
  lastTick = now;
  if (easing) {
    float t = mvDur ? (float)(now - mvStart) / mvDur : 1.0f;
    if (t >= 1.0f) {
      pos[0] = tgt[0]; pos[1] = tgt[1];
      easing = false;
    } else {
      float e = 0.5f - 0.5f * cosf(PI * t);
      for (int i = 0; i < 2; i++) pos[i] = from[i] + (tgt[i] - from[i]) * e;
    }
  } else {
    float dx = tgt[0] - pos[0], dy = tgt[1] - pos[1];
    float d = sqrtf(dx * dx + dy * dy), step = curSpeed * dt;
    if (d > step) { pos[0] += dx * step / d; pos[1] += dy * step / d; }
    else          { pos[0] = tgt[0];         pos[1] = tgt[1]; }
  }
  writeOut();
}

// =====================================================================
//  Eye geometry
// =====================================================================

// Angles for eye set s looking at (x, y): limited to the unit circle,
// which the set's radii stretch into an ellipse around its center.
// The Lids set is one fixed position: its center, whatever the look direction.
void setAngles(int s, float x, float y, float& a, float& b) {
  const EyeSet& e = sets[s];
  if (s == 1) { a = e.cx; b = e.cy; return; }
  float r = sqrtf(x * x + y * y);
  if (r > 1.0f) { x /= r; y /= r; }
  if (inv1) x = -x;
  if (inv2) y = -y;
  a = e.cx + x * e.rx;
  b = e.cy + y * e.ry;
}

void lookAngles(float& a, float& b) {
  setAngles(curSet, lookX, lookY, a, b);
  if (lid > 0 && hasLids) {
    if (curSet == 0) {                       // the Main part stays inside Min/Max;
      a = constrain(a, (float)min1, (float)max1);   // only the way to the lids may leave it
      b = constrain(b, (float)min2, (float)max2);
    }
    float la, lb;
    setAngles(1, lookX, lookY, la, lb);
    a += (la - a) * lid;
    b += (lb - b) * lid;
  }
}

void snapLook() {
  float a, b;
  lookAngles(a, b);
  jumpTo(a, b);
}

void lookAt(float x, float y, float speed) {
  lookMode = true;
  lookX = constrain(x, -1.0f, 1.0f);
  lookY = constrain(y, -1.0f, 1.0f);
  float a, b;
  lookAngles(a, b);
  followTo(a, b, speed);
}

// Inverse of setAngles: the look direction that gives angles a/b in the
// current eye set (so the joystick knob can follow the sliders)
void lookFromAngles(float a, float b) {
  if (curSet == 1) return;                   // Lids: a fixed position, no look direction
  const EyeSet& e = sets[curSet];
  float x = e.rx ? (a - e.cx) / e.rx : 0;
  float y = e.ry ? (b - e.cy) / e.ry : 0;
  if (inv1) x = -x;
  if (inv2) y = -y;
  float r = sqrtf(x * x + y * y);
  if (r > 1.0f) { x /= r; y /= r; }
  lookX = x;
  lookY = y;
}

void manualLook(float x, float y) {
  lookMode = true;
  lookX = constrain(x, -1.0f, 1.0f);
  lookY = constrain(y, -1.0f, 1.0f);
  snapLook();
}

void glideLook(float x, float y, float l, uint32_t ms, float speed) {
  curSet = 0;
  lookMode = true;
  lookX = constrain(x, -1.0f, 1.0f);
  lookY = constrain(y, -1.0f, 1.0f);
  lid = l;
  float a, b;
  lookAngles(a, b);
  glideTo(a, b, ms, speed);
}
