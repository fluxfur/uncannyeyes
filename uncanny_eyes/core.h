// Uncanny Eyes – https://www.youtube.com/@fluxandfur – https://github.com/fluxfur/uncannyeyes
// Copyright (c) 2026 @fluxandfur – MIT License (see LICENSE)
//
// core.h – what all parts share: the camera target handed to the eyes, and the eye lock.
#pragma once
#include <Arduino.h>
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// The camera target, written by the vision loop (core 1), read by eyesTask (core 0).
// x, y in -1..+1 as the eyes should look: +x right, +y up. size = head width / frame width.
const int MAX_OTHERS = 3;
struct TrackerState {
  int   src;                        // 0 none, 1 face, 2 head
  float x, y, size;
  float rx, ry;                      // same target, unsmoothed (for fast tracking)
  uint32_t ms;                      // millis() of the last frame that had a target
  uint32_t seq;                     // counts camera frames
  int   nOthers;                    // other people (rough head positions, same coordinates)
  float ox[MAX_OTHERS], oy[MAX_OTHERS];
};
extern TrackerState trk;
extern portMUX_TYPE trkMux;

// Eye state (motion, behavior, settings) is owned by eyesTask; the web handlers
// take this lock before touching it.
extern SemaphoreHandle_t eyeLock;
inline void lockEyes()   { xSemaphoreTake(eyeLock, portMAX_DELAY); }
inline void unlockEyes() { xSemaphoreGive(eyeLock); }
