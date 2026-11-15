// Uncanny Eyes – https://www.youtube.com/@fluxandfur – https://github.com/fluxfur/uncannyeyes
// Copyright (c) 2026 @fluxandfur – MIT License (see LICENSE)
//
// motion.h – servo layer and eye geometry: where the servos are, where they are going,
// and how a look direction (x, y, lid) turns into servo angles.
#pragma once
#include <Arduino.h>
#include "config.h"           // servo pins and pulse range

// ---------- axis configuration (degrees) ----------
extern int  min1, max1, min2, max2;
extern bool inv1, inv2;             // reverse (applied by the look functions and the page sliders)

// ---------- eye sets: Main (always) and Lids (optional) ----------
// center (cx, cy) in servo degrees and the radii (rx, ry) of the ellipse the eyes may move in.
// Lids: only the center is used (a fixed position); Min/Max don't apply to it.
struct EyeSet { int cx, cy, rx, ry; };
const int NUM_SETS = 2;
extern const EyeSet DEFAULT_SETS[NUM_SETS];
extern EyeSet sets[NUM_SETS];
extern bool hasLids;                // this painting has lids

// ---------- relax when idle ----------
extern bool relaxOn, relaxed;
extern int  relaxMs;
extern uint32_t lastMoveMs;

// ---------- motion state ----------
extern float pos[2], tgt[2];        // angles sent now / where the servos are heading
extern bool  easing;                // a timed, eased move (glide) is running
extern uint32_t lastDuty[2];

// ---------- look state ----------
extern int   curSet;                // eye set the look is in (0 Main, 1 Lids)
extern bool  lookMode;              // true: target comes from look + eye set (false: raw angles)
extern float lookX, lookY;          // -1..+1, +x right, +y up
extern float lid;                   // 0 = eyes open .. 1 = lids closed

// servo layer
void  attachServos();
void  writeOut();
void  relaxServos();
float clampAxis(int i, float a);
void  glideTo(float a, float b, uint32_t ms, float speed);   // eased, at least as long as 'speed' needs
void  followTo(float a, float b, float speed);              // follow at 'speed' (deg/s)
void  jumpTo(float a, float b);                             // instant (manual control)
void  updateMotion();                                       // call often; updates at 50 Hz

// geometry
void setAngles(int s, float x, float y, float& a, float& b);
void lookAngles(float& a, float& b);                        // current look, lids blended in
void snapLook();                                            // jump to the current look
void lookAt(float x, float y, float speed);                 // smooth look, main or current set
void lookFromAngles(float a, float b);                      // angles -> look (for the joystick knob)
void manualLook(float x, float y);                          // joystick: instant
void glideLook(float x, float y, float l, uint32_t ms, float speed);   // main set, eased
