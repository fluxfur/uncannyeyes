// Uncanny Eyes – https://www.youtube.com/@fluxandfur – https://github.com/fluxfur/uncannyeyes
// Copyright (c) 2026 @fluxandfur – MIT License (see LICENSE)
//
// vision.h – camera tracking. Publishes the target in trk (core.h); never touches the servos.
#pragma once
#include <Arduino.h>

bool visionSetup();                 // settings, camera, buffers: first thing in setup()
void visionStartLearning();         // learn the empty room (after the WiFi wait)
void visionLoop();                  // one camera frame: call from loop()
bool visionSerialCommand(const char* line);   // gray 0 / gray 1 / relearn / defaults
const char* visionModeName();

// for the web page
int  visionSettingsJson(char* buf, size_t len);
void visionSetFromQuery(const char* q);       // "key=value&..." camera settings
void visionRequestRelearn();
struct VisionStatus {
  int   src;                        // 0 none, 1 face, 2 head
  float x, y, size, fps, detMs, temp;
  int   bg;                         // 0 settling, 1 learning, 2 ready
  bool  wait;                       // learning waits for the room to be still
  int   people;
  float tCam, tConv, tRoom, tFace, tStream;   // ms per frame step
};
void visionStatus(VisionStatus& v);

// MJPEG stream: register viewers (frames are only annotated + encoded while someone
// watches) and copy the newest frame: >0 length, 0 nothing new, -1 out of memory
void visionStreamClient(int delta);
int  visionCopyJpeg(uint32_t& lastSeq, uint8_t*& buf, size_t& cap);
