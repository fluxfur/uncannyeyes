// Uncanny Eyes – https://www.youtube.com/@fluxandfur – https://github.com/fluxfur/uncannyeyes
// Copyright (c) 2026 @fluxandfur – MIT License (see LICENSE)
//
// config.h – the things you may want to change for your own build, all in one place.
// Everything else is set on the web page (http://eyes.local/).
#pragma once
#include <Arduino.h>

#define UE_VERSION "1.0.0"

// ---------- network ----------
#define HOSTNAME      "eyes"         // web page at http://eyes.local/
#define WIFI_WAIT_MS  10000          // wait this long for WiFi at boot, then start anyway (keeps trying)

// ---------- servos ----------
#define SERVO1_PIN    D0             // eyes left/right
#define SERVO2_PIN    D1             // eyes up/down
#define PULSE_MIN_US  500            // servo pulse at 0° (narrow to ~600-2400 if a servo buzzes at the ends)
#define PULSE_MAX_US  2500           // servo pulse at 180°

// ---------- camera ----------
#define MIRROR_X      0              // 1 = mirror left/right. 0 fits the XIAO Sense camera;
                                     // flip it if the eyes follow the wrong way
#define STREAM_QUAL   75             // quality of the live view on the page (0..100)
#define SHOW_TEMP     1              // 1 = read the chip temperature (shown with Advanced)

// ---------- behavior ----------
#define MANUAL_PAUSE_MS 20000        // behavior pauses this long after manual control

// ---------- serial output (115200 baud) ----------
// Start-up messages, errors and replies to serial commands are always printed.
#define LOG_TRACKING  0              // 1 = FACE / HEAD / NONE per frame + timing once a second
#define LOG_BEHAVIOR  1              // 1 = what the eyes do ("action: Blink", "person: arrived -> ...")

#define logTracking(...) do { if (LOG_TRACKING) Serial.printf(__VA_ARGS__); } while (0)
#define logBehavior(...) do { if (LOG_BEHAVIOR) Serial.printf(__VA_ARGS__); } while (0)
