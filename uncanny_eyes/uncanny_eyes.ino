/*
  Uncanny Eyes – camera tracking + eye servos
  https://www.youtube.com/@fluxandfur
  https://github.com/fluxfur/uncannyeyes
  Copyright (c) 2026 @fluxandfur – MIT License (see LICENSE)

  Board: Seeed XIAO ESP32S3 Sense (OV3660)

  Arduino IDE settings (esp32 core 2.0.x, e.g. 2.0.17 – NOT 3.x):
    Board:      "XIAO_ESP32S3"
    PSRAM:      "OPI PSRAM"
    Partition:  "8M with spiffs (3MB APP/1.5MB SPIFFS)"  (or "Maximum APP")
    USB CDC On Boot: "Enabled"  (for the Serial Monitor)
  Attach the WiFi antenna that came with the board.

  Files (all in this sketch folder, the IDE shows them as tabs):
    uncanny_eyes.ino   this file: setup, loop, eyes task, serial commands
    config.h           pins, hostname, serial output – the things you may want to change
    core.h             what all parts share: camera target (trk), eye lock
    vision.h/.cpp      camera: background model -> person -> head -> face, debug stream
    motion.h/.cpp      servos and eye geometry (look direction -> servo angles)
    behavior.h/.cpp    idle actions, sleep, vision behavior (reactions to people)
    settings.h/.cpp    eye settings: limits, flash storage, saved copy
    web.h/.cpp         WiFi, mDNS, web server (page, settings, stream)
    page.h             the web page
    secrets.h          your WiFi name and password (enter them before you upload)

  Wiring:
    Servo 1 signal -> D0  (eyes left/right)      Servo 2 signal -> D1  (eyes up/down)
    Power the servos from an external 5 V supply, share GND with the board.

  Web page: http://eyes.local/  (stream on port 81, only while the Camera section is open)

  How it runs (three parts that never wait for each other):
    - loop()       vision, one camera frame at a time (slow, 1–20 fps). It only publishes the
                   target (trk) and never touches the servos.
    - eyesTask     servos + behavior, every 5 ms on core 0 with a 50 Hz motion update. Reads
                   the latest target, so slow face detection never stalls or jerks the eyes.
    - web server   handlers run in the server's own task; they share the eye state with
                   eyesTask through one mutex (eyeLock).

  Serial output with LOG_TRACKING 1 in config.h (x,y in -1..+1 as the eyes should look: +x right, +y up):
    FACE x=… y=… size=…     face found: point between the eyes
    HEAD x=… y=… size=…     no face, head estimated from the silhouette
    NONE                    printed once when the target is lost
  Serial commands (line ending "Newline"):
    gray 0 / gray 1   color / grayscale mode (reboots)      relearn   learn the empty room again
    defaults          reset camera settings (reboots)       ip        print network info
    wifi              show the WiFi                         (empty line) print the web address
    wifi <name> <password>   connect to another WiFi; saved only if it works ("My Home" for spaces)
*/

#include <WiFi.h>
#include "core.h"
#include "vision.h"
#include "motion.h"
#include "behavior.h"
#include "settings.h"
#include "web.h"

TrackerState trk = {};
portMUX_TYPE trkMux = portMUX_INITIALIZER_UNLOCKED;
SemaphoreHandle_t eyeLock = nullptr;

// Servo motion + behavior + vision reactions, independent of the camera frame rate.
// Every 5 ms; updateMotion() itself limits the servo updates to 50 per second.
static void eyesTask(void* arg) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    lockEyes();
    updateMotion();
    behaviorTick();
    if (relaxOn && !relaxed && millis() - lastMoveMs >= (uint32_t)relaxMs) relaxServos();
    unlockEyes();
    vTaskDelayUntil(&last, pdMS_TO_TICKS(5));
  }
}

// Serial commands (work even when the web page can't be reached)
static void handleSerialCommands() {
  static char line[128];             // long enough for "wifi <name> <password>"
  static int len = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (len < (int)sizeof(line) - 1) line[len++] = c;
      continue;
    }
    if (len == 0) { printAddress(); continue; }
    line[len] = 0;
    len = 0;
    if (visionSerialCommand(line)) continue;
    if (wifiSerialCommand(line)) continue;
    if (!strcmp(line, "ip")) {
      Serial.printf("IP=%s  RSSI=%d dBm  mode=%s\n", WiFi.localIP().toString().c_str(),
                    (int)WiFi.RSSI(), visionModeName());
    } else {
      Serial.printf("Unknown command '%s'. Commands: gray 0, gray 1, relearn, defaults, ip, wifi\n", line);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);                       // give USB-CDC a moment
  Serial.println("\nUncanny Eyes " UE_VERSION " – portrait with moving eyes and camera tracking @fluxandfur");

  if (!visionSetup()) { while (true) delay(1000); }

  // eyes: start right away, before the WiFi wait
  eyeLock = xSemaphoreCreateMutex();
  loadCfg();
  attachServos();
  behaviorStart();
  // core 0 (with WiFi), the camera loop has core 1. Priority above the idle tasks, far below WiFi.
  xTaskCreatePinnedToCore(eyesTask, "eyes", 4096, nullptr, 3, nullptr, 0);

  wifiStart();                       // waits up to 10 s
  webStart();
  visionStartLearning();             // the WiFi wait took a while – learn the room fresh
}

void loop() {
  handleSerialCommands();
  wifiTick();
  visionLoop();
}
