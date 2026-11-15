// Uncanny Eyes – https://www.youtube.com/@fluxandfur – https://github.com/fluxfur/uncannyeyes
// Copyright (c) 2026 @fluxandfur – MIT License (see LICENSE)
//
// vision.cpp – the camera part: background model -> people -> head -> face, and the
// annotated debug stream. Runs in loop() (core 1); it only publishes the target (trk).
//
//  1. The board learns what the empty room looks like (background model on an 80x60 grid).
//     It waits until nothing moves, so keep the room empty for a moment after boot / relearn.
//  2. Everything that differs from it is foreground. Blobs that are big and tall enough and
//     move at least a little are people; the largest one (closest person) is tracked.
//  3. The head is estimated from the top of that person  -> fast, works at any distance.
//  4. Face detection runs only on a small square around the head. If it finds the face,
//     the target moves to the point between the eyes.
//  5. Things that stop moving completely (a moved chair) fade into the background within
//     seconds; people (who always move a little) only very slowly. Leftover shapes where
//     someone stood snap back to the remembered empty room.
#include <Arduino.h>

#if ESP_ARDUINO_VERSION_MAJOR >= 3
#error "Needs esp32 Arduino core 2.0.x (e.g. 2.0.17). ESP-DL face detection was removed in 3.x."
#endif
#ifndef BOARD_HAS_PSRAM
#error "PSRAM is off. Tools > PSRAM > OPI PSRAM"
#endif

#include <Preferences.h>
#include <atomic>
#include <list>
#include "esp_camera.h"
#include "esp_http_server.h"   // only for httpd_query_key_value()
#include "esp_timer.h"
#include "img_converters.h"
#include "fb_gfx.h"
#include "human_face_detect_msr01.hpp"
#include "human_face_detect_mnp01.hpp"
#include "vision.h"
#include "core.h"

// ---------------- fixed settings (the ones you may change are in config.h) ----------------
#define JPEG_QUALITY  10     // camera JPEG quality (lower = better)
#define SETTLE_MS     2500   // let auto exposure settle before locking it
#define LEARN_FRAMES  15     // frames used to learn the empty room
#define LEARN_MAX_MOVING 40  // learn only while fewer cells than this (of 4800) are moving
#define LEARN_MAX_WAIT_MS 30000   // ...but don't wait longer than this

// ---------------- XIAO ESP32S3 Sense camera pins ----------------
#define PWDN_GPIO_NUM   -1
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM   10
#define SIOD_GPIO_NUM   40
#define SIOC_GPIO_NUM   39
#define Y9_GPIO_NUM     48
#define Y8_GPIO_NUM     11
#define Y7_GPIO_NUM     12
#define Y6_GPIO_NUM     14
#define Y5_GPIO_NUM     16
#define Y4_GPIO_NUM     18
#define Y3_GPIO_NUM     17
#define Y2_GPIO_NUM     15
#define VSYNC_GPIO_NUM  38
#define HREF_GPIO_NUM   47
#define PCLK_GPIO_NUM   13

// fb_gfx colors for the BGR888 buffer produced by fmt2rgb888()
#define COL_RED      0x000000FF
#define COL_GREEN    0x0000FF00
#define COL_BLUE     0x00FF0000
#define COL_YELLOW   (COL_RED | COL_GREEN)
#define COL_CYAN     (COL_BLUE | COL_GREEN)
#define COL_MAGENTA  (COL_RED | COL_BLUE)
#define COL_WHITE    0x00FFFFFF
#define COL_ORANGE   0x000090FF

static const int MAX_W = 640, MAX_H = 480;     // VGA is the largest mode we allow
static const int MAX_DETS = 8;
static const int GRID_W = 80, GRID_H = 60;     // background grid
static const int GRID_N = GRID_W * GRID_H;
static const int MAX_BLOBS = 32;
static const int KEEP_BLOB_MIN = 6;            // smaller pieces are noise; bigger ones are tracked
static const int MAX_ROI = 240;

// ---------------- types ----------------
struct Settings {
  uint16_t version;
  uint8_t  mode;          // 0 = silhouette + face, 1 = silhouette only, 2 = faces only (full frame)
  uint8_t  frameSize;     // 0 = QVGA 320x240, 1 = VGA 640x480
  float    s1Scale;       // MSR01 resize_scale
  float    s1Score;       // MSR01 score threshold
  float    s2Score;       // MNP01 score threshold
  uint16_t roiSize;       // face search crop is resampled to roiSize x roiSize
  uint8_t  confirmHits;   // frames a face must be seen before it counts
  uint8_t  holdFrames;    // frames to hold a lost face / head
  uint8_t  fgThresh;      // brightness difference from background that counts as foreground
  uint16_t minBlob;       // smallest person blob in grid cells (of 4800)
  uint16_t absorbSec;     // seconds until something that stops moving becomes background
  uint8_t  lockExp;       // lock auto exposure after learning the room
  uint8_t  showMask;      // tint foreground in the debug stream
  uint8_t  gainCeiling;   // 0..6 = 2x..128x
  uint8_t  vflip, hmirror;
  uint8_t  grayscale;     // 1 = raw grayscale capture (no JPEG artifacts), 0 = color JPEG. Needs reboot.
  int8_t   aeLevel;       // -2..2: auto exposure target (brighter = higher)
  int8_t   brightness;    // -2..2: digital brightness
  int8_t   contrast;      // -2..2
  uint8_t  aec2;          // extended / night exposure: brighter in dim rooms, but more blur and lower FPS
  uint16_t manualExp;     // 0 = auto exposure, 1..1200 = fixed exposure time
  uint8_t  manualGain;    // 0..30 fixed gain (only used with manual exposure)
  uint16_t stillSec;      // a blob without any movement for this long counts as an object, not a person
  uint8_t  stillAbsorbSec;// such objects fade into the background within roughly this time
  uint8_t  motionThr;     // frame-to-frame change per cell that counts as movement
  uint8_t  faceBoost;     // stretch the contrast of the face search square before detection
  uint8_t  showCrop;      // show what the face detector sees, in the corner of the live view
  uint8_t  minHeight;     // a person must be at least this many cells tall (of 60)
  uint8_t  faceEvery;     // face detection only every n-th frame (head tracking runs every frame)
};
static const uint16_t SETTINGS_VERSION = 1;    // bump when the struct changes (resets to defaults)

struct Det {
  int x1, y1, x2, y2;      // box in frame pixels
  float cx, cy, size;      // box center normalized -1..1, size = width / frame width
  float ax, ay;            // aim point (between the eyes) normalized -1..1
  bool eyes;
  int lex, ley, rex, rey;
};

struct Track {
  bool active = false;
  float x = 0, y = 0, size = 0;
  float ax = 0, ay = 0;
  int hits = 0, misses = 0;
};

struct Blob {
  int area;
  int minx, maxx, miny, maxy;
};

enum BgState { BG_SETTLE = 0, BG_LEARN = 1, BG_READY = 2 };
static volatile bool bgWaiting = false;   // learning paused: too much movement
static uint32_t learnWaitStartMs = 0;

// ---------------- settings ----------------
static Settings defaultSettings() {
  Settings s;
  s.version = SETTINGS_VERSION;
  s.mode = 0;
  s.frameSize = 0;
  s.s1Scale = 1.0f;        // the crop is small, so stage 1 looks at it at full detail
  s.s1Score = 0.1f;
  s.s2Score = 0.3f;
  s.roiSize = 160;
  s.confirmHits = 1;
  s.holdFrames = 3;
  s.fgThresh = 20;
  s.minBlob = 40;          // cells of 4800 to count as a person
  s.absorbSec = 300;       // people (who always move a little) are absorbed only very slowly
  s.lockExp = 1;
  s.showMask = 1;
  s.gainCeiling = 3;
  s.vflip = 0;
  s.hmirror = 0;
  s.grayscale = 1;
  s.aeLevel = 1;
  s.brightness = 0;
  s.contrast = 0;
  s.aec2 = 0;
  s.manualExp = 0;
  s.manualGain = 0;
  s.stillSec = 10;
  s.stillAbsorbSec = 4;
  s.motionThr = 5;
  s.faceBoost = 1;
  s.showCrop = 1;
  s.minHeight = 8;
  s.faceEvery = 1;
  return s;
}

static Settings cfg;                 // active, owned by loop()
static Settings pendingCfg;          // written by the web server
static volatile bool cfgDirty = false;
static volatile bool relearnReq = false;
static portMUX_TYPE cfgMux = portMUX_INITIALIZER_UNLOCKED;
static Preferences camPrefs;         // camera settings (namespace "camera"; the eye settings are in settings.cpp)
RTC_NOINIT_ATTR static int crashCount;   // survives a crash-reboot, not a power cycle

// ---------------- globals ----------------
static HumanFaceDetectMSR01* detStage1 = nullptr;
static HumanFaceDetectMNP01* detStage2 = nullptr;
static uint8_t* rgbBuf = nullptr;              // decoded frame, PSRAM
static uint8_t* cropView = nullptr;            // copy of the last main crop for the live view, PSRAM
static bool     cropViewValid = false;

static SemaphoreHandle_t jpgMutex;             // annotated JPEG for the stream
static uint8_t* jpgBuf = nullptr;
static size_t jpgLen = 0;
static std::atomic<uint32_t> jpgSeq{0};
static std::atomic<int> streamClients{0};

// status for the web page
static volatile float statFps = 0, statDetMs = 0;
static volatile int   statBlobs = 0, statBg = 0;
static volatile int   statSrc = 0;             // 0 none, 1 face, 2 head
static volatile float statX = 0, statY = 0, statSize = 0;
static volatile float statTemp = 0;            // chip temperature in °C (die, not room temperature)
// time per frame step, ms (smoothed): camera wait, image conversion, room model + people + head,
// face square + target, live stream (annotate + JPEG)
static volatile float tCam = 0, tConv = 0, tRoom = 0, tFace = 0, tStream = 0;

// background model
static float*   bgModel;
static uint8_t* gray;
static uint8_t *chromaU, *chromaV;   // color per cell (128 = neutral); stays 128 in grayscale
static float   *bgU, *bgV;
static uint8_t* diffMap;                    // difference from background per cell (0..255)
#define COLOR_WEIGHT  2.0f   // color difference counts this much relative to brightness difference
#define HEAD_THR_FACTOR 0.55f // above a body, accept differences down to this fraction of the threshold
static uint8_t* fgRaw;
static uint8_t* fgMask;
static uint8_t* prevBgMask;
// movement: is something here actually moving, or just sitting there?
static uint8_t  *prevGray, *prevU, *prevV;
static uint8_t*  movingNow;     // changed since the last frame
static uint32_t* lastMotionMs;  // last time this cell belonged to something that moved
static uint8_t*  stillCell;     // foreground that is being absorbed (for the debug view)
static bool     havePrevGrid = false;
static bool     blobAlive[MAX_BLOBS];  // blob moved within the last stillSec seconds
static bool     blobMature[MAX_BLOBS]; // blob has existed for a few frames (not a flicker)
static uint32_t nowMs = 0;
static int      headRegion[4] = {0, 0, -1, -1};   // grid cells searched for the head (x0, y0, x1, y1)
static int16_t* labels;
static int16_t* bfsQueue;
static uint8_t *refGray, *refU, *refV, *nearLive, *tmpMask;   // more grid buffers (PSRAM)
static uint8_t *fgAge;          // how many frames in a row each cell has been foreground
#define MATURE_FRAMES 4         // a blob must exist this long (on average) to count as a person
static float   *emptyY, *emptyU, *emptyV;   // the empty room as learned (follows only real light changes)
#define GHOST_FACTOR 0.5f   // a cell this close to the empty room is a leftover shape, not a person
static Blob    blobs[MAX_BLOBS];
static int     nBlobs = 0;
static int     bgState = BG_SETTLE;
static int64_t bgStateUs = 0;
static int     bgLearnFrames = 0;
static int     globalChangeFrames = 0;
static int     fgCount = 0;

// head (from silhouette)
static bool  headSeen = false;
static int   headMiss = 1000;
static float headX = 0, headY = 0, headSize = 0;   // normalized; size = head width / frame width
static int   headLowFrames = 0;                    // frames in a row the head estimate was lower
static int   chosenBlob = -1;
static int   headBox[4] = {0, 0, 0, 0};             // pixels, for drawing

// face tracking
static Track mainT, pendT;

// ---------------- face task (core 0) ----------------
// Face detection is the slow step. It runs in its own task on core 0, in parallel with the
// rest of the vision on core 1: loop() prepares the search square (faceBuf) and hands it
// over; the result comes back a frame or more later. While a job runs, loop() goes on.
struct FaceJob { int x0, y0, S, R, w, h; float headX, headY; };
static SemaphoreHandle_t detMutex = nullptr;   // detStage1/2: face task + loop() (mode 2, settings)
static SemaphoreHandle_t faceSem = nullptr;    // "new job"
static uint8_t* faceBuf = nullptr;             // the job's search square (PSRAM)
static FaceJob  faceJob;
static Det      faceRes[MAX_DETS];
static int      faceResN = 0;
static float    faceResMs = 0;
static std::atomic<bool> faceBusy{false}, faceDone{false};

// ---------------- drawing helpers (clipped) ----------------
static void hline(fb_data_t* fb, int x, int y, int w, uint32_t c) {
  if (y < 0 || y >= fb->height) return;
  if (x < 0) { w += x; x = 0; }
  if (x + w > fb->width) w = fb->width - x;
  if (w > 0) fb_gfx_drawFastHLine(fb, x, y, w, c);
}
static void vline(fb_data_t* fb, int x, int y, int h, uint32_t c) {
  if (x < 0 || x >= fb->width) return;
  if (y < 0) { h += y; y = 0; }
  if (y + h > fb->height) h = fb->height - y;
  if (h > 0) fb_gfx_drawFastVLine(fb, x, y, h, c);
}
static void rect(fb_data_t* fb, int x1, int y1, int x2, int y2, uint32_t c, int thick = 2) {
  for (int t = 0; t < thick; t++) {
    hline(fb, x1, y1 + t, x2 - x1 + 1, c);
    hline(fb, x1, y2 - t, x2 - x1 + 1, c);
    vline(fb, x1 + t, y1, y2 - y1 + 1, c);
    vline(fb, x2 - t, y1, y2 - y1 + 1, c);
  }
}
static void dot(fb_data_t* fb, int x, int y, uint32_t c) {
  for (int i = -1; i <= 1; i++) hline(fb, x - 1, y + i, 3, c);
}
static void crosshair(fb_data_t* fb, int x, int y, int r, uint32_t c) {
  for (int t = -1; t <= 0; t++) {
    hline(fb, x - r, y + t, 2 * r + 1, c);
    vline(fb, x + t, y - r, 2 * r + 1, c);
  }
}

// ---------------- background: restart / learn ----------------
// Manual exposure: fixed values. Auto: let the camera adjust (it gets locked later if "Lock exposure").
static void applyExposure() {
  sensor_t* s = esp_camera_sensor_get();
  if (cfg.manualExp > 0) {
    s->set_exposure_ctrl(s, 0);
    s->set_aec_value(s, cfg.manualExp);
    s->set_gain_ctrl(s, 0);
    s->set_agc_gain(s, cfg.manualGain);
  } else {
    s->set_exposure_ctrl(s, 1);
    s->set_gain_ctrl(s, 1);
  }
  s->set_whitebal(s, 1);           // auto white balance on while settling (locked later in color mode)
  s->set_awb_gain(s, 1);
}

static void restartBackground() {
  applyExposure();                // auto exposure on while settling (unless manual)
  bgState = BG_SETTLE;
  bgStateUs = esp_timer_get_time();
  bgLearnFrames = 0;
  bgWaiting = false;
  learnWaitStartMs = 0;
  globalChangeFrames = 0;
  headSeen = false;
  chosenBlob = -1;
  nBlobs = 0;
  fgCount = 0;
  memset(fgMask, 0, GRID_N);
  memset(stillCell, 0, GRID_N);
  if (fgAge) memset(fgAge, 0, GRID_N);
  headRegion[2] = -1;
  Serial.println("Learning background – keep the room empty for a moment");
}

// ---------------- settings: load / save / apply ----------------
static void loadSettings() {
  Settings s;
  camPrefs.begin("camera", false);
  Settings def = defaultSettings();
  size_t len = camPrefs.getBytesLength("cfg");
  memset(&s, 0, sizeof(s));
  if (len == sizeof(s) && camPrefs.getBytes("cfg", &s, len) == len && s.version == SETTINGS_VERSION) {
    cfg = s;
  } else {
    cfg = def;
  }
  if (cfg.faceEvery < 1 || cfg.faceEvery > 10) cfg.faceEvery = def.faceEvery;
}

static void saveSettings() {
  camPrefs.putBytes("cfg", &cfg, sizeof(cfg));
}

static void applySettings(const Settings& n, bool force) {
  // capture format and (in grayscale) frame size are fixed at camera init: save and reboot
  if (!force && (n.grayscale != cfg.grayscale || (n.grayscale && n.frameSize != cfg.frameSize))) {
    cfg = n;
    saveSettings();
    Serial.println("Camera format changed – rebooting");
    delay(300);
    ESP.restart();
  }

  sensor_t* s = esp_camera_sensor_get();
  bool relearn = force;
  if (force || n.frameSize != cfg.frameSize) {
    s->set_framesize(s, n.frameSize ? FRAMESIZE_VGA : FRAMESIZE_QVGA);
    mainT.active = pendT.active = false;
    relearn = true;
  }
  if (force || n.vflip != cfg.vflip)       { s->set_vflip(s, n.vflip); relearn = true; }
  if (force || n.hmirror != cfg.hmirror)   { s->set_hmirror(s, n.hmirror); relearn = true; }
  if (force || n.gainCeiling != cfg.gainCeiling) { s->set_gainceiling(s, (gainceiling_t)n.gainCeiling); relearn = true; }
  if (force || n.aeLevel != cfg.aeLevel)       { s->set_ae_level(s, n.aeLevel); relearn = true; }
  if (force || n.brightness != cfg.brightness) { s->set_brightness(s, n.brightness); relearn = true; }
  if (force || n.contrast != cfg.contrast)     { s->set_contrast(s, n.contrast); relearn = true; }
  if (force || n.aec2 != cfg.aec2)             { s->set_aec2(s, n.aec2); relearn = true; }
  if (n.lockExp != cfg.lockExp || n.manualExp != cfg.manualExp || n.manualGain != cfg.manualGain) relearn = true;
  if (n.mode != cfg.mode) {
    mainT.active = pendT.active = false;
    if (n.mode == 2 || cfg.mode == 2) relearn = true;   // background wasn't updated in 'faces only'
  }

  if (force || !detStage1 || !detStage2 ||
      n.s1Scale != cfg.s1Scale || n.s1Score != cfg.s1Score || n.s2Score != cfg.s2Score) {
    xSemaphoreTake(detMutex, portMAX_DELAY);  // the face task may be using them
    delete detStage1;
    delete detStage2;
    detStage1 = new HumanFaceDetectMSR01(n.s1Score, 0.5F, 10, n.s1Scale);
    detStage2 = new HumanFaceDetectMNP01(n.s2Score, 0.3F, 5);
    xSemaphoreGive(detMutex);
  }
  cfg = n;
  saveSettings();
  if (force) applyExposure();               // at boot: setup() starts the learning once WiFi is done
  else if (relearn) restartBackground();
  Serial.printf("Settings: mode=%d %s s1scale=%.2f s2score=%.2f roi=%d fgthr=%d minblob=%d absorb=%ds lock=%d\n",
                cfg.mode, cfg.frameSize ? "VGA" : "QVGA", cfg.s1Scale, cfg.s2Score, cfg.roiSize,
                cfg.fgThresh, cfg.minBlob, cfg.absorbSec, cfg.lockExp);
}

static int settingsJson(char* buf, size_t len, const Settings& s) {
  return snprintf(buf, len,
    "{\"mode\":%d,\"fs\":%d,\"s1scale\":%.2f,\"s1score\":%.2f,\"s2score\":%.2f,\"roi\":%d,"
    "\"confirm\":%d,\"hold\":%d,\"fgthr\":%d,\"minblob\":%d,\"absorb\":%d,\"lock\":%d,\"mask\":%d,"
    "\"gain\":%d,\"vflip\":%d,\"hmirror\":%d,\"gray\":%d,\"ae\":%d,\"bright\":%d,\"contrast\":%d,"
    "\"aec2\":%d,\"mexp\":%d,\"mgain\":%d,\"still\":%d,\"stillabs\":%d,\"mthr\":%d,"
    "\"boost\":%d,\"crop\":%d,\"minh\":%d,\"fevery\":%d}",
    s.mode, s.frameSize, s.s1Scale, s.s1Score, s.s2Score, s.roiSize,
    s.confirmHits, s.holdFrames, s.fgThresh, s.minBlob, s.absorbSec, s.lockExp, s.showMask,
    s.gainCeiling, s.vflip, s.hmirror, s.grayscale, s.aeLevel, s.brightness, s.contrast,
    s.aec2, s.manualExp, s.manualGain, s.stillSec, s.stillAbsorbSec, s.motionThr,
    s.faceBoost, s.showCrop, s.minHeight, s.faceEvery);
}

// ---------------- camera ----------------
static bool initCamera(bool grayscale, bool vga) {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk  = XCLK_GPIO_NUM;
  config.pin_pclk  = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href  = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn  = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  // Grayscale = raw pixels, no JPEG artifacts and no decoding. fmt2rgb888() expands it to 3 channels.
  // Raw frames are much more data than JPEG: a slower clock and a buffer of exactly the needed size
  // keep the camera driver from overflowing (its task has a tiny stack in the Arduino core).
  config.xclk_freq_hz = grayscale ? 10000000 : 20000000;
  config.pixel_format = grayscale ? PIXFORMAT_GRAYSCALE : PIXFORMAT_JPEG;
  config.frame_size   = grayscale ? (vga ? FRAMESIZE_VGA : FRAMESIZE_QVGA)
                                  : FRAMESIZE_VGA;   // JPEG: init large so both sizes fit
  config.jpeg_quality = JPEG_QUALITY;
  config.fb_count     = 2;
  config.fb_location  = CAMERA_FB_IN_PSRAM;
  config.grab_mode    = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }
  sensor_t* s = esp_camera_sensor_get();
  Serial.printf("Camera sensor PID: 0x%x, %s\n", s->id.PID, grayscale ? "grayscale" : "color JPEG");
  s->set_lenc(s, 1);
  s->set_whitebal(s, 1);
  s->set_awb_gain(s, 1);
  return true;
}

// ---------------- gray grid from the decoded frame ----------------
static void computeGrayGrid(int w, int h) {
  const int bw = w / GRID_W, bh = h / GRID_H;
  const int step = bw >= 8 ? 2 : 1;                 // subsample inside big blocks (VGA)
  for (int gy = 0; gy < GRID_H; gy++) {
    for (int gx = 0; gx < GRID_W; gx++) {
      uint32_t sb = 0, sg = 0, sr = 0, n = 0;
      for (int y = gy * bh; y < (gy + 1) * bh; y += step) {
        const uint8_t* p = rgbBuf + (y * w + gx * bw) * 3;
        for (int x = 0; x < bw; x += step, p += 3 * step) {
          sb += p[0]; sg += p[1]; sr += p[2];                 // buffer is BGR
          n++;
        }
      }
      int i = gy * GRID_W + gx;
      if (!n) { gray[i] = 0; chromaU[i] = chromaV[i] = 128; continue; }
      float b = (float)sb / n, g = (float)sg / n, r = (float)sr / n;
      float yv = 0.114f * b + 0.587f * g + 0.299f * r;
      gray[i] = (uint8_t)yv;
      chromaU[i] = (uint8_t)constrain(128.0f + 0.564f * (b - yv), 0.0f, 255.0f);
      chromaV[i] = (uint8_t)constrain(128.0f + 0.713f * (r - yv), 0.0f, 255.0f);
    }
  }
}

// ---------------- foreground segmentation + blobs ----------------
static void segmentForeground() {
  // global brightness compensation using cells that were background last frame
  float sg = 0, sb = 0;
  for (int i = 0; i < GRID_N; i++) {
    if (prevBgMask[i]) { sg += gray[i]; sb += bgModel[i]; }
  }
  float ratio = (sb > 100.0f) ? sg / sb : 1.0f;
  ratio = constrain(ratio, 0.7f, 1.4f);

  // global color-tint compensation (like the brightness ratio): if the whole image shifts tint,
  // that shift is not a person
  float su = 0, sv = 0; int nBg = 0;
  for (int i = 0; i < GRID_N; i++) {
    if (prevBgMask[i]) { su += chromaU[i] - bgU[i]; sv += chromaV[i] - bgV[i]; nBg++; }
  }
  float offU = nBg > 100 ? su / nBg : 0, offV = nBg > 100 ? sv / nBg : 0;

  for (int i = 0; i < GRID_N; i++) {
    float d  = fabsf(gray[i] - bgModel[i] * ratio);
    // color is unreliable in dark areas (noise): fade its weight out below brightness ~50
    float cw = COLOR_WEIGHT * fminf(1.0f, gray[i] / 50.0f);
    float du = fabsf(chromaU[i] - bgU[i] - offU) * cw;
    float dv = fabsf(chromaV[i] - bgV[i] - offV) * cw;
    float m = fmaxf(d, fmaxf(du, dv));
    if (m > cfg.fgThresh * GHOST_FACTOR) {
      // Leftover shape? If the cell looks like the empty room again, the background model was
      // polluted (someone stood here a while). Snap it back instead of reporting a fake person.
      float e  = fabsf(gray[i] - emptyY[i] * ratio);
      float eu = fabsf(chromaU[i] - emptyU[i] - offU) * cw;
      float ev = fabsf(chromaV[i] - emptyV[i] - offV) * cw;
      float me = fmaxf(e, fmaxf(eu, ev));
      if (me < cfg.fgThresh * GHOST_FACTOR) {
        bgModel[i] = gray[i] / ratio; bgU[i] = chromaU[i]; bgV[i] = chromaV[i];
        m = me;
      }
    }
    diffMap[i] = (uint8_t)fminf(m, 255.0f);
    fgRaw[i] = m > cfg.fgThresh;
  }

  // clean up: drop isolated cells, fill small holes
  fgCount = 0;
  for (int y = 0; y < GRID_H; y++) {
    for (int x = 0; x < GRID_W; x++) {
      int nb = 0;
      for (int dy = -1; dy <= 1; dy++) {
        int yy = y + dy;
        if (yy < 0 || yy >= GRID_H) continue;
        for (int dx = -1; dx <= 1; dx++) {
          int xx = x + dx;
          if ((dx || dy) && xx >= 0 && xx < GRID_W && fgRaw[yy * GRID_W + xx]) nb++;
        }
      }
      int i = y * GRID_W + x;
      fgMask[i] = (nb >= 5) || (fgRaw[i] && nb >= 2);
    }
  }

  // bridge short vertical gaps (thin neck, head above a gap) so the head stays attached to the body
  for (int x = 0; x < GRID_W; x++) {
    int lastFg = -100;
    for (int y = 0; y < GRID_H; y++) {
      if (fgMask[y * GRID_W + x]) {
        int gap = y - lastFg - 1;
        if (gap > 0 && gap <= 3) {
          for (int yy = lastFg + 1; yy < y; yy++) fgMask[yy * GRID_W + x] = 1;
        }
        lastFg = y;
      }
    }
  }
  // Age of the foreground per cell, with spatial tolerance: a cell continues the age of its
  // neighborhood (2 cells), so someone walking quickly keeps "aging" as one person, while a
  // flicker that pops up for a frame or two never gets old. (tmpMask/nearLive used as scratch.)
  for (int y = 0; y < GRID_H; y++) {                  // max filter horizontally
    for (int x = 0; x < GRID_W; x++) {
      uint8_t m = 0;
      for (int dx = -2; dx <= 2; dx++) {
        int xx = x + dx;
        if (xx >= 0 && xx < GRID_W && fgAge[y * GRID_W + xx] > m) m = fgAge[y * GRID_W + xx];
      }
      tmpMask[y * GRID_W + x] = m;
    }
  }
  for (int y = 0; y < GRID_H; y++) {                  // ...and vertically
    for (int x = 0; x < GRID_W; x++) {
      uint8_t m = 0;
      for (int dy = -2; dy <= 2; dy++) {
        int yy = y + dy;
        if (yy >= 0 && yy < GRID_H && tmpMask[yy * GRID_W + x] > m) m = tmpMask[yy * GRID_W + x];
      }
      nearLive[y * GRID_W + x] = m;
    }
  }
  for (int i = 0; i < GRID_N; i++) {
    fgCount += fgMask[i];
    fgAge[i] = fgMask[i] ? (nearLive[i] < 255 ? nearLive[i] + 1 : 255) : 0;
  }

  // connected components (4-connected)
  nBlobs = 0;
  for (int i = 0; i < GRID_N; i++) labels[i] = -1;
  for (int start = 0; start < GRID_N; start++) {
    if (!fgMask[start] || labels[start] != -1) continue;
    int id = nBlobs < MAX_BLOBS ? nBlobs : -2;
    int head = 0, tail = 0;
    bfsQueue[tail++] = start;
    labels[start] = id;
    Blob b = { 0, GRID_W, -1, GRID_H, -1 };
    while (head < tail) {
      int c = bfsQueue[head++];
      int x = c % GRID_W, y = c / GRID_W;
      b.area++;
      if (x < b.minx) b.minx = x;
      if (x > b.maxx) b.maxx = x;
      if (y < b.miny) b.miny = y;
      if (y > b.maxy) b.maxy = y;
      const int nx[4] = { x - 1, x + 1, x, x };
      const int ny[4] = { y, y, y - 1, y + 1 };
      for (int k = 0; k < 4; k++) {
        if (nx[k] < 0 || nx[k] >= GRID_W || ny[k] < 0 || ny[k] >= GRID_H) continue;
        int n = ny[k] * GRID_W + nx[k];
        if (fgMask[n] && labels[n] == -1) { labels[n] = id; bfsQueue[tail++] = n; }
      }
    }
    if (id >= 0 && b.area >= KEEP_BLOB_MIN) {
      blobs[nBlobs++] = b;
    } else {
      for (int k = 0; k < tail; k++) labels[bfsQueue[k]] = -2;   // too small / too many
    }
  }
}

static bool trackConfirmed(const Track& t);   // defined further down

// Movement per cell (runs every frame, before segmentation).
// Two comparisons: against the previous frame (fast movement) and against a snapshot from about
// one second ago. A person "standing still" sways and breathes - too little per frame, but clearly
// visible over a second. A real object doesn't change at all.
static void detectMovement() {
  static uint32_t refMs = 0;
  bool refReady = havePrevGrid && refMs != 0;
  for (int i = 0; i < GRID_N; i++) {
    int d  = abs((int)gray[i] - (int)prevGray[i]);
    int du = abs((int)chromaU[i] - (int)prevU[i]) * 2;
    int dv = abs((int)chromaV[i] - (int)prevV[i]) * 2;
    bool fast = havePrevGrid && max(d, max(du, dv)) > cfg.motionThr;
    bool slow = false;
    if (refReady) {
      int rd  = abs((int)gray[i] - (int)refGray[i]);
      int rdu = abs((int)chromaU[i] - (int)refU[i]) * 2;
      int rdv = abs((int)chromaV[i] - (int)refV[i]) * 2;
      slow = max(rd, max(rdu, rdv)) > cfg.motionThr;
    }
    movingNow[i] = fast || slow;
    prevGray[i] = gray[i]; prevU[i] = chromaU[i]; prevV[i] = chromaV[i];
  }
  if (!havePrevGrid || nowMs - refMs >= 1000) {        // new snapshot every second
    memcpy(refGray, gray, GRID_N); memcpy(refU, chromaU, GRID_N); memcpy(refV, chromaV, GRID_N);
    refMs = nowMs ? nowMs : 1;
  }
  havePrevGrid = true;
}

// Decides per blob whether it is alive (moved recently) or a still object.
// A blob "moves" when enough of its cells changed this frame - single noisy cells don't count.
// The timestamp is stored in the cells, so it carries over between frames without blob IDs.
static void updateLiveness() {
  int movingCells[MAX_BLOBS] = {0};
  for (int i = 0; i < GRID_N; i++) {
    int id = labels[i];
    if (id >= 0) { if (movingNow[i]) movingCells[id]++; }
    else if (movingNow[i]) lastMotionMs[i] = nowMs;          // cells outside person blobs: per cell
  }

  // a confirmed face means "human" - never treat that blob as an object
  int faceGX = -1, faceGY = -1;
  if (trackConfirmed(mainT)) {
    faceGX = (int)((mainT.x + 1.0f) * GRID_W / 2.0f);
    faceGY = (int)((mainT.y + 1.0f) * GRID_H / 2.0f);
  }

  // the person we are currently following gets 1.5x more patience before counting as an object
  // (more would let a static object that once became the target keep it for too long)
  int headGX = -1, headGY = -1;
  if (headSeen) {
    headGX = (int)((headX + 1.0f) * GRID_W / 2.0f);
    headGY = (int)((headY + 1.0f) * GRID_H / 2.0f);
  }

  bool moved[MAX_BLOBS], tracked[MAX_BLOBS];
  for (int b = 0; b < nBlobs; b++) {
    const Blob& bl = blobs[b];
    int need = max(2, bl.area / 80);                 // ~1% of the blob changing is enough
    bool hasFace = faceGX >= bl.minx - 2 && faceGX <= bl.maxx + 2 &&
                   faceGY >= bl.miny - 3 && faceGY <= bl.maxy;
    moved[b] = movingCells[b] >= need || hasFace;
    tracked[b] = headGX >= bl.minx - 2 && headGX <= bl.maxx + 2 &&
                 headGY >= bl.miny - 4 && headGY <= bl.maxy;
  }

  uint32_t newest[MAX_BLOBS] = {0};
  for (int i = 0; i < GRID_N; i++) {
    int id = labels[i];
    if (id < 0) continue;
    if (moved[id]) lastMotionMs[i] = nowMs;
    if (lastMotionMs[i] > newest[id]) newest[id] = lastMotionMs[i];
  }
  const uint32_t stillMs = (uint32_t)cfg.stillSec * 1000;
  for (int b = 0; b < nBlobs; b++) {
    uint32_t limit = tracked[b] ? stillMs * 3 / 2 : stillMs;
    blobAlive[b] = (nowMs - newest[b]) < limit;
  }

  // Flickering noise blobs (a few frames only) are not people. Count the average age of a blob's
  // cells: a real person's body has been there for many frames, even when they move.
  // (Only decides whether a blob may count as a person - a newcomer is not treated as an object.)
  {
    uint32_t ageSum[MAX_BLOBS] = {0};
    for (int i = 0; i < GRID_N; i++) if (labels[i] >= 0) ageSum[labels[i]] += fgAge[i];
    for (int b = 0; b < nBlobs; b++)
      blobMature[b] = ageSum[b] >= (uint32_t)blobs[b].area * MATURE_FRAMES;
  }

  // A person often falls apart into several pieces (legs, arm, head). Pieces close to a live
  // person belong to that person and must not fade out on their own.
  for (int pass = 0; pass < 2; pass++) {
    for (int b = 0; b < nBlobs; b++) {
      if (blobAlive[b]) continue;
      for (int a = 0; a < nBlobs; a++) {
        if (!blobAlive[a]) continue;
        const Blob& A = blobs[a]; const Blob& B = blobs[b];
        const int m = 4;   // cells
        if (B.minx <= A.maxx + m && B.maxx >= A.minx - m && B.miny <= A.maxy + m && B.maxy >= A.miny - m) {
          blobAlive[b] = true;
          break;
        }
      }
    }
  }
}

static void updateBackground() {
  float fps = statFps > 2.0f ? statFps : 2.0f;
  const float aMoving = 1.0f / (cfg.absorbSec * fps);              // people: very slowly
  const float aStill  = 1.5f / (cfg.stillAbsorbSec * fps);         // still objects: quickly
  const float aBg     = 0.05f;                                     // empty room: follow light changes
  const uint8_t faintThr = (uint8_t)(cfg.fgThresh * HEAD_THR_FACTOR);
  const uint32_t stillMs = (uint32_t)cfg.stillSec * 1000;
  const bool headAlive = chosenBlob >= 0 && blobAlive[chosenBlob];

  // protect a 2-cell margin around live people: their low-contrast edges (below the threshold)
  // must not soak into the background, or they leave a "ghost" when the person walks away
  // Every row of a live person is protected from its left to its right edge, including the inside.
  // Uniform areas (a plain shirt) don't change when the person moves slightly, and parts of them are
  // often below the threshold - without this they would slowly soak into the background.
  uint8_t* tmp = tmpMask;
  memset(tmp, 0, GRID_N);
  for (int b = 0; b < nBlobs; b++) {
    if (!blobAlive[b]) continue;
    const Blob& bl = blobs[b];
    for (int y = bl.miny; y <= bl.maxy; y++) {
      int l = -1, r = -1;
      for (int x = bl.minx; x <= bl.maxx; x++) {
        if (labels[y * GRID_W + x] == b) { if (l < 0) l = x; r = x; }
      }
      if (l >= 0) memset(tmp + y * GRID_W + l, 1, r - l + 1);
    }
  }
  if (headAlive && headRegion[2] >= headRegion[0]) {      // the (faint) head above the body as well
    for (int y = headRegion[1]; y <= headRegion[3]; y++)
      memset(tmp + y * GRID_W + headRegion[0], 1, headRegion[2] - headRegion[0] + 1);
  }
  for (int y = 0; y < GRID_H; y++) {                  // dilate horizontally
    for (int x = 0; x < GRID_W; x++) {
      uint8_t v = 0;
      for (int dx = -2; dx <= 2 && !v; dx++) {
        int xx = x + dx;
        if (xx >= 0 && xx < GRID_W) v = tmp[y * GRID_W + xx];
      }
      nearLive[y * GRID_W + x] = v;
    }
  }
  memcpy(tmp, nearLive, GRID_N);
  for (int y = 0; y < GRID_H; y++) {                  // dilate vertically
    for (int x = 0; x < GRID_W; x++) {
      uint8_t v = 0;
      for (int dy = -2; dy <= 2 && !v; dy++) {
        int yy = y + dy;
        if (yy >= 0 && yy < GRID_H) v = tmp[yy * GRID_W + x];
      }
      nearLive[y * GRID_W + x] = v;
    }
  }

  for (int i = 0; i < GRID_N; i++) {
    float a = nearLive[i] ? aMoving : aBg;
    stillCell[i] = 0;
    bool faint = diffMap[i] > faintThr;              // weak difference, e.g. a low-contrast head
    if (fgMask[i] || faint) {
      int x = i % GRID_W, y = i / GRID_W;
      bool inHead = headAlive && x >= headRegion[0] && x <= headRegion[2] &&
                    y >= headRegion[1] && y <= headRegion[3];
      bool still;
      if (fgMask[i] && labels[i] >= 0) still = !blobAlive[labels[i]];
      else if (inHead || nearLive[i])  still = false;                    // faint parts of a live person
      else                             still = (nowMs - lastMotionMs[i]) > stillMs;
      a = still ? aStill : aMoving;
      stillCell[i] = still && fgMask[i];
    }
    bgModel[i] += a * (gray[i] - bgModel[i]);
    bgU[i] += a * (chromaU[i] - bgU[i]);
    bgV[i] += a * (chromaV[i] - bgV[i]);
    if (!fgMask[i] && !nearLive[i] && diffMap[i] <= faintThr) {   // clearly empty here: follow the light
      emptyY[i] += aBg * (gray[i] - emptyY[i]);
      emptyU[i] += aBg * (chromaU[i] - emptyU[i]);
      emptyV[i] += aBg * (chromaV[i] - emptyV[i]);
    }
    prevBgMask[i] = !fgMask[i];
  }
}

// Big and tall enough to be a person (smaller live pieces are still tracked, just not counted).
static bool isPerson(int i) {
  const Blob& b = blobs[i];
  return blobAlive[i] && blobMature[i] && b.area >= cfg.minBlob && (b.maxy - b.miny + 1) >= cfg.minHeight;
}

// Picks the person blob and estimates the head. Returns true if a head was found this frame.
static bool estimateHead(int w, int h, float& hx, float& hy, float& hsize) {
  chosenBlob = -1;
  headRegion[2] = -1;
  if (nBlobs == 0) return false;

  // only blobs that moved recently count as people; still objects are ignored while they fade out
  int largest = -1;
  for (int i = 0; i < nBlobs; i++) {
    if (!isPerson(i)) continue;
    if (largest < 0 || blobs[i].area > blobs[largest].area) largest = i;
  }
  if (largest < 0) return false;
  chosenBlob = largest;

  // stick with the person we followed last frame unless someone much bigger (closer) shows up
  if (headSeen) {
    int px = (int)((headX + 1.0f) * GRID_W / 2.0f);
    int py = (int)((headY + 1.0f) * GRID_H / 2.0f);
    for (int i = 0; i < nBlobs; i++) {
      const Blob& b = blobs[i];
      if (!isPerson(i)) continue;
      if (px >= b.minx - 2 && px <= b.maxx + 2 && py >= b.miny - 2 && py <= b.maxy + 2 &&
          b.area * 2 >= blobs[largest].area) {
        chosenBlob = i;
        break;
      }
    }
  }

  const Blob& b = blobs[chosenBlob];

  // x-range of the top of the blob (first 3 rows)
  int tminx = GRID_W, tmaxx = -1;
  for (int y = b.miny; y < b.miny + 3 && y <= b.maxy; y++) {
    for (int x = b.minx; x <= b.maxx; x++) {
      if (labels[y * GRID_W + x] == chosenBlob) {
        if (x < tminx) tminx = x;
        if (x > tmaxx) tmaxx = x;
      }
    }
  }
  if (tmaxx < 0) return false;
  int margin = max(2, (tmaxx - tminx + 1) / 2);
  int x0 = max(0, tminx - margin), x1 = min(GRID_W - 1, tmaxx + margin);

  // look above the blob for the head. Here a much fainter difference counts (low-contrast head),
  // the rest of the room keeps the normal threshold.
  const uint8_t headThr = (uint8_t)(cfg.fgThresh * HEAD_THR_FACTOR);
  auto headCell = [&](int x, int y) {
    int i = y * GRID_W + x;
    return fgMask[i] || diffMap[i] > headThr;
  };
  int top = b.miny, gap = 0;
  for (int y = b.miny - 1; y >= 0 && gap <= 3; y--) {
    int hits = 0;
    for (int x = x0; x <= x1; x++) hits += headCell(x, y);
    if (hits >= 2) { top = y; gap = 0; } else gap++;     // 2+ cells: ignore single noisy cells
  }

  // Remember how far a (faint) head reaches above the body. In frames where it can't be seen,
  // use that remembered height instead of dropping to the shoulders.
  static float headExtra = 0;                            // rows above the body top
  if (!headSeen) headExtra = 0;                          // new person: start fresh
  int extra = b.miny - top;
  // If the top of the silhouette is clearly narrower than the body, the head is already part of it
  // (good contrast there) - then nothing must be added on top.
  bool headInBlob = (tmaxx - tminx + 1) < 0.6f * (b.maxx - b.minx + 1);
  if (extra > 0) {
    headExtra += 0.3f * (extra - headExtra);
  } else if (headInBlob) {
    headExtra += 0.3f * (0.0f - headExtra);
  } else if (headExtra >= 1.0f) {
    top = max(0, b.miny - (int)lroundf(headExtra));
    headExtra *= 0.995f;                                 // forget slowly if it never shows up again
  }

  int height = b.maxy - top + 1;
  int headRows = max(2, (int)roundf(height * 0.18f));
  // remember where the head was searched, so faint head cells there are not absorbed
  headRegion[0] = x0; headRegion[1] = max(0, top - 2);
  headRegion[2] = x1; headRegion[3] = min(GRID_H - 1, top + headRows);
  uint32_t sx = 0, cnt = 0;
  int hminx = GRID_W, hmaxx = -1;
  for (int y = top; y < top + headRows && y < GRID_H; y++) {
    for (int x = x0; x <= x1; x++) {
      if (headCell(x, y)) {
        sx += x; cnt++;
        if (x < hminx) hminx = x;
        if (x > hmaxx) hmaxx = x;
      }
    }
  }
  if (!cnt) return false;

  float gx = (float)sx / cnt + 0.5f;
  float gy = top + headRows * 0.5f;
  hx = (gx - GRID_W / 2.0f) / (GRID_W / 2.0f);
  hy = (gy - GRID_H / 2.0f) / (GRID_H / 2.0f);
  hsize = (hmaxx - hminx + 1) / (float)GRID_W;

  const int bw = w / GRID_W, bh = h / GRID_H;
  headBox[0] = hminx * bw;
  headBox[1] = top * bh;
  headBox[2] = (hmaxx + 1) * bw - 1;
  headBox[3] = (top + headRows) * bh - 1;
  return true;
}

// ---------------- face tracking with temporal filtering ----------------
static float trackGate(const Track& t) {
  return fmaxf(0.25f, t.size * 3.0f);
}
static float trackDist(const Track& t, const Det& d) {
  float dx = d.cx - t.x, dy = d.cy - t.y;
  return sqrtf(dx * dx + dy * dy);
}
static void trackSet(Track& t, const Det& d, int hits) {
  t.active = true;
  t.x = d.cx; t.y = d.cy; t.size = d.size;
  t.ax = d.ax; t.ay = d.ay;
  t.hits = hits; t.misses = 0;
}
static void trackUpdate(Track& t, const Det& d) {
  t.x = d.cx; t.y = d.cy;
  t.ax = d.ax; t.ay = d.ay;
  t.size = t.size * 0.5f + d.size * 0.5f;
  if (t.hits < 255) t.hits++;
  t.misses = 0;
}
static bool trackConfirmed(const Track& t) {
  return t.active && t.hits >= cfg.confirmHits;
}

// Updates mainT/pendT from this frame's detections. Returns index of the det matched to mainT (or -1).
static int updateTracks(const Det* dets, int n) {
  int matched = -1;

  if (mainT.active) {
    float best = 1e9f;
    for (int i = 0; i < n; i++) {
      float d = trackDist(mainT, dets[i]);
      if (d < trackGate(mainT) && d < best) { best = d; matched = i; }
    }
  }
  if (matched >= 0) {
    trackUpdate(mainT, dets[matched]);
    pendT.active = false;
    return matched;
  }

  if (mainT.active) mainT.misses++;

  int largest = -1;
  float bestSize = 0;
  for (int i = 0; i < n; i++) {
    if (dets[i].size > bestSize) { bestSize = dets[i].size; largest = i; }
  }

  if (largest >= 0) {
    if (!trackConfirmed(mainT)) {
      trackSet(mainT, dets[largest], 1);
      if (trackConfirmed(mainT)) matched = largest;
      return matched;
    }
    if (pendT.active && trackDist(pendT, dets[largest]) < trackGate(pendT)) {
      trackUpdate(pendT, dets[largest]);
    } else {
      trackSet(pendT, dets[largest], 1);
    }
    if (pendT.hits >= cfg.confirmHits) {
      mainT = pendT;
      pendT.active = false;
      return largest;
    }
  } else {
    pendT.active = false;
  }

  if (mainT.active) {
    int limit = trackConfirmed(mainT) ? cfg.holdFrames : 1;
    if (mainT.misses > limit) mainT.active = false;
  }
  return -1;
}

// ---------------- face detection (full frame or crop) ----------------
// Converts detector results (in an image of size iw x ih that maps to the frame rect
// x0,y0 with scale sc frame-px per image-px) into Dets in frame coordinates.
static int collectDets(std::list<dl::detect::result_t>& results, int x0, int y0, float sc,
                       int w, int h, Det* dets) {
  int n = 0;
  for (auto& r : results) {
    if (n >= MAX_DETS) break;
    Det& d = dets[n++];
    d.x1 = x0 + (int)(r.box[0] * sc); d.y1 = y0 + (int)(r.box[1] * sc);
    d.x2 = x0 + (int)(r.box[2] * sc); d.y2 = y0 + (int)(r.box[3] * sc);
    d.cx = (((d.x1 + d.x2) / 2.0f) - w / 2.0f) / (w / 2.0f);
    d.cy = (((d.y1 + d.y2) / 2.0f) - h / 2.0f) / (h / 2.0f);
    d.size = (d.x2 - d.x1) / (float)w;
    d.eyes = r.keypoint.size() == 10;
    float axp, ayp;
    if (d.eyes) {
      d.lex = x0 + (int)(r.keypoint[0] * sc); d.ley = y0 + (int)(r.keypoint[1] * sc);
      d.rex = x0 + (int)(r.keypoint[6] * sc); d.rey = y0 + (int)(r.keypoint[7] * sc);
      axp = (d.lex + d.rex) / 2.0f;
      ayp = (d.ley + d.rey) / 2.0f;
    } else {
      axp = (d.x1 + d.x2) / 2.0f;
      ayp = d.y1 + 0.4f * (d.y2 - d.y1);
    }
    d.ax = (axp - w / 2.0f) / (w / 2.0f);
    d.ay = (ayp - h / 2.0f) / (h / 2.0f);
  }
  return n;
}

static int detectFull(int w, int h, Det* dets) {
  xSemaphoreTake(detMutex, portMAX_DELAY);
  std::list<dl::detect::result_t>& cand = detStage1->infer(rgbBuf, {h, w, 3});
  std::list<dl::detect::result_t>& res  = detStage2->infer(rgbBuf, {h, w, 3}, cand);
  int n = collectDets(res, 0, 0, 1.0f, w, h, dets);
  xSemaphoreGive(detMutex);
  return n;
}

// Stretches the contrast of an image so its darkest 2 % become black and its brightest 2 % white.
// A face in shadow or against a bright window often uses only a narrow band of gray values;
// spreading it out makes eyes, nose and mouth much easier for the detector to see.
static void boostContrast(uint8_t* img, int n) {
  uint32_t hist[256] = {0};
  for (int i = 0; i < n; i++) {
    const uint8_t* p = img + i * 3;
    hist[(p[0] * 29 + p[1] * 150 + p[2] * 77) >> 8]++;   // BGR -> luma
  }
  int cut = n / 50, acc = 0, lo = 0, hi = 255;
  for (lo = 0; lo < 255; lo++) { acc += hist[lo]; if (acc > cut) break; }
  acc = 0;
  for (hi = 255; hi > 0; hi--) { acc += hist[hi]; if (acc > cut) break; }
  int range = hi - lo;
  if (range < 12) return;                       // flat area: nothing to stretch
  int gain = (255 << 8) / range;                // 8.8 fixed point
  if (gain > (4 << 8)) gain = 4 << 8;           // max 4x, so noise doesn't explode
  uint8_t lut[256];
  for (int v = 0; v < 256; v++) {
    int o = ((v - lo) * gain) >> 8;
    lut[v] = (uint8_t)(o < 0 ? 0 : (o > 255 ? 255 : o));
  }
  for (int i = 0; i < n * 3; i++) img[i] = lut[img[i]];
}

// Crops a square of side S around (cxp, cyp) from the frame, resamples it to R x R into dst
// (and boosts the contrast). The face task then runs the detector on it.
static void prepareCrop(int w, int h, int cxp, int cyp, int S, uint8_t* dst, FaceJob& job, int roi[4]) {
  const int R = cfg.roiSize;
  S = constrain(S, 32, min(w, h));
  int x0 = constrain(cxp - S / 2, 0, w - S);
  int y0 = constrain(cyp - S / 2, 0, h - S);
  roi[0] = x0; roi[1] = y0; roi[2] = x0 + S - 1; roi[3] = y0 + S - 1;
  job.x0 = x0; job.y0 = y0; job.S = S; job.R = R; job.w = w; job.h = h;
  uint8_t* img = dst;                   // dst is advanced while filling

  // bilinear resampling: small distant faces get enlarged smoothly instead of blocky,
  // which the detector handles much better
  const int step = (S << 16) / R;                       // 16.16 fixed point
  for (int ry = 0; ry < R; ry++) {
    int sy = ry * step;
    int y = y0 + (sy >> 16), fy = (sy >> 8) & 0xFF;
    int y2 = min(y + 1, h - 1);
    const uint8_t* r1 = rgbBuf + (size_t)y * w * 3;
    const uint8_t* r2 = rgbBuf + (size_t)y2 * w * 3;
    for (int rx = 0; rx < R; rx++) {
      int sx = rx * step;
      int x = x0 + (sx >> 16), fx = (sx >> 8) & 0xFF;
      int x2 = min(x + 1, w - 1);
      const uint8_t* a = r1 + x * 3;  const uint8_t* b = r1 + x2 * 3;
      const uint8_t* c = r2 + x * 3;  const uint8_t* d = r2 + x2 * 3;
      for (int k = 0; k < 3; k++) {
        int top = a[k] * (256 - fx) + b[k] * fx;
        int bot = c[k] * (256 - fx) + d[k] * fx;
        *dst++ = (uint8_t)((top * (256 - fy) + bot * fy) >> 16);
      }
    }
  }
  if (cfg.faceBoost) boostContrast(img, R * R);
}

// The face task: waits for a job, runs the two detector stages, hands the result back.
static void faceTask(void* arg) {
  for (;;) {
    xSemaphoreTake(faceSem, portMAX_DELAY);
    int64_t t0 = esp_timer_get_time();
    const FaceJob& j = faceJob;
    xSemaphoreTake(detMutex, portMAX_DELAY);
    std::list<dl::detect::result_t>& cand = detStage1->infer(faceBuf, {j.R, j.R, 3});
    std::list<dl::detect::result_t>& res  = detStage2->infer(faceBuf, {j.R, j.R, 3}, cand);
    faceResN = collectDets(res, j.x0, j.y0, (float)j.S / j.R, j.w, j.h, faceRes);
    xSemaphoreGive(detMutex);
    faceResMs = (esp_timer_get_time() - t0) / 1000.0f;
    faceDone = true;
    faceBusy = false;
    vTaskDelay(1);                   // let core 0's idle task run (task watchdog)
  }
}

// ---------------- debug overlay ----------------
static void tintForeground(int w, int h) {
  const int bw = w / GRID_W, bh = h / GRID_H;
  for (int gy = 0; gy < GRID_H; gy++) {
    for (int gx = 0; gx < GRID_W; gx++) {
      int i = gy * GRID_W + gx;
      if (!fgMask[i]) continue;
      bool still = stillCell[i];      // blue = still object being absorbed, magenta = moving / person
      for (int y = gy * bh; y < (gy + 1) * bh; y++) {
        uint8_t* p = rgbBuf + (y * w + gx * bw) * 3;
        for (int x = 0; x < bw; x++, p += 3) {
          p[0] = (p[0] + 255) >> 1;                       // B
          p[1] = p[1] >> 1;                               // G
          p[2] = still ? (p[2] >> 1) : ((p[2] + 255) >> 1); // R
        }
      }
    }
  }
}


// Rough head positions of the other people (not the tracked one), in the same coordinates
// as the target: the top center of each person blob.
// tgx/tgy: the target in grid cells (-1 = none); the blob around it is the tracked person.
static int otherHeads(float* oX, float* oY, int tgx, int tgy) {
  int n = 0;
  for (int i = 0; i < nBlobs && n < MAX_OTHERS; i++) {
    if (i == chosenBlob || !isPerson(i)) continue;
    const Blob& b = blobs[i];
    if (tgx >= b.minx - 2 && tgx <= b.maxx + 2 && tgy >= b.miny - 6 && tgy <= b.maxy) continue;
    int sx = 0, cnt = 0;
    for (int y = b.miny; y < b.miny + 3 && y <= b.maxy; y++)
      for (int x = b.minx; x <= b.maxx; x++)
        if (labels[y * GRID_W + x] == i) { sx += x; cnt++; }
    if (!cnt) continue;
    float gx = (float)sx / cnt + 0.5f;
    float gy = b.miny + 0.08f * (b.maxy - b.miny + 1);
    float nx = (gx - GRID_W / 2.0f) / (GRID_W / 2.0f);
    float ny = (gy - GRID_H / 2.0f) / (GRID_H / 2.0f);
    oX[n] = constrain(MIRROR_X ? -nx : nx, -1.0f, 1.0f);
    oY[n] = constrain(-ny, -1.0f, 1.0f);
    n++;
  }
  return n;
}

// =====================================================================
//  API for the web part and the main sketch
// =====================================================================

int visionSettingsJson(char* buf, size_t len) {
  Settings s;
  portENTER_CRITICAL(&cfgMux);
  s = pendingCfg;
  portEXIT_CRITICAL(&cfgMux);
  return settingsJson(buf, len, s);
}

void visionRequestRelearn() { relearnReq = true; }

static bool qFloat(const char* q, const char* key, float& out) {
  char v[16];
  if (httpd_query_key_value(q, key, v, sizeof(v)) != ESP_OK) return false;
  out = atof(v);
  return true;
}
static bool qInt(const char* q, const char* key, int& out) {
  char v[16];
  if (httpd_query_key_value(q, key, v, sizeof(v)) != ESP_OK) return false;
  out = atoi(v);
  return true;
}

// Applies "key=value&..." camera settings (taken over by loop() before the next frame)
void visionSetFromQuery(const char* q) {
  Settings s;
  portENTER_CRITICAL(&cfgMux);
  s = pendingCfg;
  portEXIT_CRITICAL(&cfgMux);

  float f; int i;
  if (qInt(q, "mode", i))      s.mode = constrain(i, 0, 2);
  if (qInt(q, "fs", i))        s.frameSize = constrain(i, 0, 1);
  if (qFloat(q, "s1scale", f)) s.s1Scale = constrain(f, 0.1f, 1.0f);
  if (qFloat(q, "s1score", f)) s.s1Score = constrain(f, 0.05f, 0.9f);
  if (qFloat(q, "s2score", f)) s.s2Score = constrain(f, 0.1f, 0.9f);
  if (qInt(q, "roi", i))       s.roiSize = constrain(i, 96, MAX_ROI);
  if (qInt(q, "confirm", i))   s.confirmHits = constrain(i, 1, 5);
  if (qInt(q, "hold", i))      s.holdFrames = constrain(i, 0, 30);
  if (qInt(q, "fgthr", i))     s.fgThresh = constrain(i, 5, 80);
  if (qInt(q, "minblob", i))   s.minBlob = constrain(i, 10, 1000);
  if (qInt(q, "absorb", i))    s.absorbSec = constrain(i, 5, 900);
  if (qInt(q, "lock", i))      s.lockExp = i ? 1 : 0;
  if (qInt(q, "mask", i))      s.showMask = i ? 1 : 0;
  if (qInt(q, "gain", i))      s.gainCeiling = constrain(i, 0, 6);
  if (qInt(q, "vflip", i))     s.vflip = i ? 1 : 0;
  if (qInt(q, "hmirror", i))   s.hmirror = i ? 1 : 0;
  if (qInt(q, "gray", i))      s.grayscale = i ? 1 : 0;
  if (qInt(q, "ae", i))        s.aeLevel = constrain(i, -2, 2);
  if (qInt(q, "bright", i))    s.brightness = constrain(i, -2, 2);
  if (qInt(q, "contrast", i))  s.contrast = constrain(i, -2, 2);
  if (qInt(q, "aec2", i))      s.aec2 = i ? 1 : 0;
  if (qInt(q, "mexp", i))      s.manualExp = constrain(i, 0, 1200);
  if (qInt(q, "mgain", i))     s.manualGain = constrain(i, 0, 30);
  if (qInt(q, "still", i))     s.stillSec = constrain(i, 2, 120);
  if (qInt(q, "stillabs", i))  s.stillAbsorbSec = constrain(i, 1, 60);
  if (qInt(q, "mthr", i))      s.motionThr = constrain(i, 2, 40);
  if (qInt(q, "boost", i))     s.faceBoost = i ? 1 : 0;
  if (qInt(q, "crop", i))      s.showCrop = i ? 1 : 0;
  if (qInt(q, "minh", i))      s.minHeight = constrain(i, 2, 50);
  if (qInt(q, "fevery", i))    s.faceEvery = constrain(i, 1, 10);

  portENTER_CRITICAL(&cfgMux);
  pendingCfg = s;
  cfgDirty = true;
  portEXIT_CRITICAL(&cfgMux);
}

void visionStatus(VisionStatus& v) {
  v.src = statSrc; v.x = statX; v.y = statY; v.size = statSize;
  v.tCam = tCam; v.tConv = tConv; v.tRoom = tRoom; v.tFace = tFace; v.tStream = tStream;
  v.fps = statFps; v.detMs = statDetMs; v.temp = statTemp;
  v.bg = statBg; v.wait = bgWaiting; v.people = statBlobs;
}

void visionStreamClient(int delta) { streamClients += delta; }

// Copies the newest annotated JPEG into 'buf' (grown as needed, in PSRAM) if it is newer
// than lastSeq. Returns its length, 0 if there is nothing new, -1 if out of memory.
int visionCopyJpeg(uint32_t& lastSeq, uint8_t*& buf, size_t& cap) {
  if (jpgSeq.load() == lastSeq) return 0;
  xSemaphoreTake(jpgMutex, portMAX_DELAY);
  size_t len = jpgLen;
  if (len > cap) {
    free(buf);
    buf = (uint8_t*)ps_malloc(len);
    cap = buf ? len : 0;
  }
  if (buf && jpgBuf) memcpy(buf, jpgBuf, len);
  lastSeq = jpgSeq.load();
  xSemaphoreGive(jpgMutex);
  return buf ? (int)len : -1;
}

const char* visionModeName() { return cfg.grayscale ? "grayscale" : "color JPEG"; }

// Serial commands: gray 0 / gray 1 (reboots), relearn, defaults (reboots). false = unknown.
bool visionSerialCommand(const char* line) {
  if (!strcmp(line, "gray 0") || !strcmp(line, "gray 1")) {
    Settings n = cfg;
    n.grayscale = line[5] == '1';
    if (n.grayscale == cfg.grayscale) {
      Serial.printf("Already in %s mode\n", visionModeName());
      return true;
    }
    Serial.printf("Switching to %s\n", n.grayscale ? "grayscale" : "color JPEG");
    portENTER_CRITICAL(&cfgMux);
    pendingCfg = n;
    portEXIT_CRITICAL(&cfgMux);
    applySettings(n, false);                   // saves and reboots
    return true;
  }
  if (!strcmp(line, "relearn")) { restartBackground(); return true; }
  if (!strcmp(line, "defaults")) {
    cfg = defaultSettings();
    saveSettings();
    Serial.println("Camera settings reset – rebooting");
    delay(300);
    ESP.restart();
  }
  return false;
}

// Settings, camera, buffers. Call first in setup(), before WiFi.
bool visionSetup() {
  if (!psramFound()) {
    Serial.println("No PSRAM! Tools > PSRAM > OPI PSRAM");
    return false;
  }
  loadSettings();                    // first: the color mode decides how the camera starts

  // boot-loop guard: two crashes in a row right after boot -> fall back to color JPEG
  if (esp_reset_reason() == ESP_RST_PANIC) crashCount++;
  else crashCount = 0;
  if (crashCount >= 2 && cfg.grayscale) {
    Serial.println("Crashed twice after boot – falling back to color JPEG mode");
    cfg.grayscale = 0;
    saveSettings();
    crashCount = 0;
  }

  if (!initCamera(cfg.grayscale, cfg.frameSize == 1)) return false;

  rgbBuf = (uint8_t*)ps_malloc(MAX_W * MAX_H * 3);
  cropView = (uint8_t*)ps_malloc(MAX_ROI * MAX_ROI * 3);
  faceBuf = (uint8_t*)ps_malloc(MAX_ROI * MAX_ROI * 3);
  jpgMutex = xSemaphoreCreateMutex();
  detMutex = xSemaphoreCreateMutex();
  faceSem = xSemaphoreCreateBinary();

  // All 80x60 grid tables live in PSRAM (~180 KB). Kept in internal RAM they starved WiFi of
  // buffer memory: the board stayed "connected" but stopped answering after a few seconds.
  bool gridOk = true;
  auto grid8  = [&]() { uint8_t* p = (uint8_t*)ps_calloc(GRID_N, 1); gridOk &= p != nullptr; return p; };
  auto gridF  = [&]() { float* p = (float*)ps_calloc(GRID_N, sizeof(float)); gridOk &= p != nullptr; return p; };
  auto grid16 = [&]() { int16_t* p = (int16_t*)ps_calloc(GRID_N, sizeof(int16_t)); gridOk &= p != nullptr; return p; };
  bgModel = gridF(); bgU = gridF(); bgV = gridF();
  emptyY = gridF(); emptyU = gridF(); emptyV = gridF();
  gray = grid8(); chromaU = grid8(); chromaV = grid8(); diffMap = grid8();
  fgRaw = grid8(); fgMask = grid8(); prevBgMask = grid8();
  prevGray = grid8(); prevU = grid8(); prevV = grid8(); movingNow = grid8(); stillCell = grid8();
  refGray = grid8(); refU = grid8(); refV = grid8(); nearLive = grid8(); tmpMask = grid8();
  fgAge = grid8();
  labels = grid16(); bfsQueue = grid16();
  lastMotionMs = (uint32_t*)ps_calloc(GRID_N, sizeof(uint32_t));
  gridOk &= lastMotionMs != nullptr;

  if (!rgbBuf || !cropView || !faceBuf || !jpgMutex || !detMutex || !faceSem || !gridOk) {
    Serial.println("Out of memory");
    return false;
  }

  pendingCfg = cfg;
  applySettings(cfg, true);
  // face detection on core 0, below the eyes task (3) and WiFi; it only fills idle time there
  xTaskCreatePinnedToCore(faceTask, "face", 12288, nullptr, 1, nullptr, 0);

  return true;
}

// Learn the empty room (fresh, after the WiFi wait took a while)
void visionStartLearning() { restartBackground(); }

// ---------------- one camera frame (called from loop()) ----------------
void visionLoop() {
  static int64_t lastLoopUs = 0;
  static int lastSrc = 0;

  if (cfgDirty) {
    Settings n;
    portENTER_CRITICAL(&cfgMux);
    n = pendingCfg;
    cfgDirty = false;
    portEXIT_CRITICAL(&cfgMux);
    applySettings(n, false);
  }
  if (relearnReq) {
    relearnReq = false;
    restartBackground();
  }

  int64_t tA = esp_timer_get_time();            // timing: start of the frame
  camera_fb_t* fb = esp_camera_fb_get();
  int64_t tB = esp_timer_get_time();
  if (!fb) { delay(10); return; }
  const int w = fb->width, h = fb->height;
  bool ok = (w <= MAX_W && h <= MAX_H && w >= GRID_W && h >= GRID_H) &&
            fmt2rgb888(fb->buf, fb->len, fb->format, rgbBuf);
  esp_camera_fb_return(fb);
  if (!ok) { delay(10); return; }
  int64_t tC = esp_timer_get_time();

  // --- 1. background model + silhouette (modes 0 and 1)
  bool useSilhouette = cfg.mode != 2;
  bool headNow = false;
  float hx = 0, hy = 0, hs = 0;

  if (useSilhouette) {
    computeGrayGrid(w, h);
    nowMs = millis();
    detectMovement();
    int64_t now = esp_timer_get_time();

    if (bgState == BG_SETTLE) {
      bool autoLock = cfg.lockExp && cfg.manualExp == 0;
      bool lockWb = cfg.lockExp && !cfg.grayscale;
      if (now - bgStateUs > ((autoLock || lockWb) ? SETTLE_MS : 300) * 1000LL) {
        sensor_t* s = esp_camera_sensor_get();
        if (autoLock) {
          s->set_exposure_ctrl(s, 0);     // freeze exposure + gain at the settled values
          s->set_gain_ctrl(s, 0);
        }
        if (lockWb) {
          // freeze white balance too: otherwise the whole image changes tint when someone in
          // colorful clothes walks in, and the background looks 'changed' everywhere
          s->set_whitebal(s, 0);
        }
        bgState = BG_LEARN;
        bgLearnFrames = 0;
        havePrevGrid = false;             // compare movement only against frames after the lock
      }
    } else if (bgState == BG_LEARN) {
      // Only learn while the room is still: if something moves, start the learning over.
      // (Gives up waiting after LEARN_MAX_WAIT_MS so it can't hang forever, e.g. with a TV in view.)
      int moving = 0;
      for (int i = 0; i < GRID_N; i++) moving += movingNow[i];
      static uint32_t lastWaitMsgMs = 0;
      if (learnWaitStartMs == 0) learnWaitStartMs = nowMs ? nowMs : 1;
      bool tooMuch = moving > LEARN_MAX_MOVING && nowMs - learnWaitStartMs < LEARN_MAX_WAIT_MS;
      bgWaiting = tooMuch;
      if (tooMuch) {
        bgLearnFrames = 0;
        if (nowMs - lastWaitMsgMs > 2000) {
          lastWaitMsgMs = nowMs;
          Serial.printf("Waiting for the room to be still (%d moving cells)\n", moving);
        }
      } else {
      for (int i = 0; i < GRID_N; i++) {
        if (bgLearnFrames == 0) {
          bgModel[i] = gray[i]; bgU[i] = chromaU[i]; bgV[i] = chromaV[i];
        } else {
          bgModel[i] += 0.3f * (gray[i] - bgModel[i]);
          bgU[i] += 0.3f * (chromaU[i] - bgU[i]);
          bgV[i] += 0.3f * (chromaV[i] - bgV[i]);
        }
        emptyY[i] = bgModel[i]; emptyU[i] = bgU[i]; emptyV[i] = bgV[i];
        prevBgMask[i] = 1;
        lastMotionMs[i] = nowMs;
      }
      if (++bgLearnFrames >= LEARN_FRAMES) {
        bgState = BG_READY;
        learnWaitStartMs = 0;
        bgWaiting = false;
        crashCount = 0;                   // ran long enough: not a boot loop
        Serial.println("Background learned");
      }
      }   // end: room still enough to learn
    } else {
      segmentForeground();
      if (fgCount > GRID_N / 2) {
        // more than half the image changed: lights switched or camera moved
        nBlobs = 0;
        if (++globalChangeFrames >= 5) restartBackground();
      } else {
        globalChangeFrames = 0;
        updateLiveness();                         // which blobs moved recently = people
        headNow = estimateHead(w, h, hx, hy, hs);
        updateBackground();                       // still objects fade fast, people slowly
      }
    }

    if (headNow) {
      if (!headSeen || headMiss > cfg.holdFrames) { headX = hx; headY = hy; headSize = hs; }
      else {
        headX += 0.5f * (hx - headX);
        // Heights: jump UP quickly, sink DOWN slowly. The typical error is a faint head being missed
        // for a frame, which makes the estimate drop to the shoulders - that must not shake the target.
        // But lower for 3+ frames in a row = a real move down (bending, sitting): follow it quickly,
        // so the face search square comes along.
        headLowFrames = hy > headY + 0.02f ? headLowFrames + 1 : 0;
        headY += (hy < headY || headLowFrames >= 3 ? 0.3f : 0.08f) * (hy - headY);
        headSize += 0.1f * (hs - headSize);       // size changes slowly: steadier face search box
      }
      headSeen = true;
      headMiss = 0;
    } else if (headMiss < 1000) {
      headMiss++;
    }
  } else {
    headSeen = false;
    chosenBlob = -1;
    nBlobs = 0;
    fgCount = 0;
  }
  bool headOk = useSilhouette && headSeen && headMiss <= cfg.holdFrames;

  int64_t tD = esp_timer_get_time();
  // --- 2. face detection: on a crop around the head (mode 0) or full frame (mode 2)
  Det dets[MAX_DETS];
  int nDets = 0;
  int roi[4] = {0, 0, -1, -1};
  bool ranFace = false;                          // a face result is applied this frame
  float detMs = 0;
  int64_t t0 = esp_timer_get_time();
  static float faceHeadX = 0, faceHeadY = 0;     // head estimate when the tracked face was last found
  float jobHX = headX, jobHY = headY;            // head estimate of the result applied this frame

  static float dispX = 0, dispY = 0, dispS = 0;   // calm face-search box for the live view
  static int   dispMiss = 100;
  bool tightTry = false;
  if (dispMiss < 100) dispMiss++;
  if (faceDone) {                                // a result from the face task (an earlier frame)
    faceDone = false;
    if (cfg.mode == 0) {
      nDets = faceResN;
      memcpy(dets, faceRes, sizeof(Det) * nDets);
      detMs = faceResMs;
      jobHX = faceJob.headX; jobHY = faceJob.headY;   // head when this square was taken
      ranFace = true;
    }
  }
  if (cfg.mode == 2) {
    nDets = detectFull(w, h, dets);
    ranFace = true;
    detMs = (esp_timer_get_time() - t0) / 1000.0f;
    dispS = 0; dispMiss = 100;
  } else if (cfg.mode == 0 && !faceBusy) {      // the face task is free: give it the next square
    bool faceTracked = trackConfirmed(mainT);
    // Face detection is the slow part: run it only every faceEvery-th frame. The head estimate
    // runs every frame; in between, the face target moves along with the head (see below).
    static uint8_t faceCycle = 0;
    faceCycle = (faceCycle + 1) % max((int)cfg.faceEvery, 1);
    if ((faceTracked || headOk) && faceCycle == 0) {
      int cxp, cyp, S;
      if (faceTracked) {
        // where the face is now: last found position + how far the head moved since then
        float fx = mainT.x, fy = mainT.y;
        if (headOk) { fx += headX - faceHeadX; fy += headY - faceHeadY; }
        cxp = (int)((fx + 1.0f) * w / 2.0f);
        cyp = (int)((fy + 1.0f) * h / 2.0f);
        S = (int)(mainT.size * w * (mainT.misses > 0 ? 3.5f : 3.0f));   // a bit larger after a miss
      } else {
        // Larger square, shifted up: if the estimate sits on the shoulders, the face is still inside.
        // Every 3rd frame a tighter square is tried: the silhouette often overestimates head width
        // (shoulders, hair), which makes a distant face too small for the detector.
        static uint8_t cycle = 0;
        cycle = (cycle + 1) % 3;
        tightTry = (cycle == 0);
        S = (int)(headSize * w * (tightTry ? 1.8f : 3.0f));
        cxp = (int)((headX + 1.0f) * w / 2.0f);
        cyp = (int)((headY + 1.0f) * h / 2.0f) - S / (tightTry ? 6 : 4);
      }
      prepareCrop(w, h, cxp, cyp, S, faceBuf, faceJob, roi);
      faceJob.headX = headX; faceJob.headY = headY;
      faceBusy = true;
      xSemaphoreGive(faceSem);                    // the face task takes it from here
      if (!tightTry && cfg.showCrop && streamClients.load() > 0) {
        memcpy(cropView, faceBuf, cfg.roiSize * cfg.roiSize * 3);   // what the detector sees
        cropViewValid = true;
      }
      if (!tightTry) {
        // the live view shows the square where it really is
        dispX = (roi[0] + roi[2]) * 0.5f;
        dispY = (roi[1] + roi[3]) * 0.5f;
        dispS = roi[2] - roi[0] + 1.0f;
        dispMiss = 0;
      }
    }
  }
  int matched = -1;
  if (cfg.mode == 1) {
    mainT.active = pendT.active = false;
  } else if (ranFace) {                          // frames without a face result keep the face track
    matched = updateTracks(dets, nDets);
    if (mainT.active && mainT.misses == 0) { faceHeadX = jobHX; faceHeadY = jobHY; }   // found this time
  }
  bool faceOk = cfg.mode != 1 && trackConfirmed(mainT);

  // Face and head are different points (between the eyes / top of the silhouette). While a face
  // is found, learn the difference; the head target then keeps using it for a while after the
  // face is lost (fading out), so switching face <-> head doesn't make the target jump.
  static float offX = 0, offY = 0;               // face - head (normalized)
  static uint32_t faceSeenMs = 0;
  static float shiftX = 0, shiftY = 0;           // head movement since the face was found
  if (faceOk && ranFace && mainT.misses == 0 && headOk && cfg.mode == 0) {
    float ox = mainT.ax - faceHeadX, oy = mainT.ay - faceHeadY;
    if (millis() - faceSeenMs > 3000) { offX = ox; offY = oy; }           // first time: take it
    else { offX += 0.3f * (ox - offX); offY += 0.3f * (oy - offY); }
    faceSeenMs = millis();
  }
  uint32_t sinceFace = millis() - faceSeenMs;
  float offFade = sinceFace < 2000 ? 1.0f : sinceFace < 5000 ? (5000 - sinceFace) / 3000.0f : 0.0f;

  // --- 3. choose the target: face (between the eyes) > head estimate > nothing
  int src = 0;
  float nx = 0, ny = 0, size = 0;
  if (faceOk) {
    src = 1; nx = mainT.ax; ny = mainT.ay; size = mainT.size;
    // the face was found in an earlier frame: move it along with the head since then
    // (if the head estimate drops out for a moment, keep the last shift instead of jumping)
    if (cfg.mode == 0) {
      if (headOk) { shiftX = headX - faceHeadX; shiftY = headY - faceHeadY; }
      nx += shiftX; ny += shiftY;
    }
  }
  else if (headOk) {
    src = 2; nx = headX + offX * offFade; ny = headY + offY * offFade; size = headSize;
    shiftX = shiftY = 0;
  }

  float tx = constrain(MIRROR_X ? -nx : nx, -1.0f, 1.0f);
  float ty = constrain(-ny, -1.0f, 1.0f);                     // +y = up
  // the same target without the head smoothing (this frame's estimate), for fast tracking
  float rnx = (src == 2 && headNow) ? hx + offX * offFade : nx;
  float rny = (src == 2 && headNow) ? hy + offY * offFade : ny;
  float rtx = constrain(MIRROR_X ? -rnx : rnx, -1.0f, 1.0f);
  float rty = constrain(-rny, -1.0f, 1.0f);

  if (src == 1)      logTracking("FACE x=%.2f y=%.2f size=%.2f\n", tx, ty, size);
  else if (src == 2) logTracking("HEAD x=%.2f y=%.2f size=%.2f\n", tx, ty, size);
  else if (lastSrc != 0) logTracking("NONE\n");
  lastSrc = src;

  // hand the target to eyesTask (a tiny copy; the vision loop never waits for the servos)
  float oX[MAX_OTHERS], oY[MAX_OTHERS];
  int tgx = src ? (int)((nx + 1.0f) * GRID_W / 2.0f) : -1;   // nx/ny: target before mirroring
  int tgy = src ? (int)((ny + 1.0f) * GRID_H / 2.0f) : -100;
  int nOth = (useSilhouette && bgState == BG_READY) ? otherHeads(oX, oY, tgx, tgy) : 0;
  portENTER_CRITICAL(&trkMux);
  trk.src = src;
  trk.seq++;
  trk.x = tx; trk.y = ty; trk.size = size;
  trk.rx = rtx; trk.ry = rty;
  if (src) trk.ms = millis();
  trk.nOthers = nOth;
  for (int k = 0; k < nOth; k++) { trk.ox[k] = oX[k]; trk.oy[k] = oY[k]; }
  portEXIT_CRITICAL(&trkMux);

  statSrc = src;
  statX = tx; statY = ty; statSize = size;
  { int n = 0; for (int i = 0; i < nBlobs; i++) n += isPerson(i); statBlobs = n; }
  statBg = useSilhouette ? bgState : BG_READY;

  // chip temperature, read every 2 s (the sensor is slow and it changes slowly)
  static uint32_t lastTempMs = 0;
  if (millis() - lastTempMs > 2000 || lastTempMs == 0) {
    lastTempMs = millis();
#if SHOW_TEMP
    statTemp = temperatureRead();
#endif
  }

  if (ranFace) statDetMs = statDetMs * 0.8f + detMs * 0.2f;

  int64_t tE = esp_timer_get_time();
  // --- 4. debug stream: only annotate + encode while a browser is watching
  if (streamClients.load() > 0) {
    fb_data_t img;
    img.width = w; img.height = h; img.data = rgbBuf;
    img.bytes_per_pixel = 3; img.format = FB_BGR888;

    if (useSilhouette && bgState == BG_READY) {
      if (cfg.showMask) tintForeground(w, h);
      // a box around every detected person: magenta = the one being tracked, orange = other people.
      // Still objects that are fading out get no box (they are tinted blue).
      const int bw = w / GRID_W, bh = h / GRID_H;
      for (int i = 0; i < nBlobs; i++) {
        if (!isPerson(i)) continue;
        const Blob& b = blobs[i];
        bool main = (i == chosenBlob);
        rect(&img, b.minx * bw, b.miny * bh, (b.maxx + 1) * bw - 1, (b.maxy + 1) * bh - 1,
             main ? COL_MAGENTA : COL_ORANGE, main ? 2 : 1);
      }
      if (chosenBlob >= 0 && headNow) rect(&img, headBox[0], headBox[1], headBox[2], headBox[3], COL_WHITE, 1);
    }
    if (dispS > 0 && dispMiss <= 10) {              // hide it once no search ran for a moment
      int half = (int)(dispS * 0.5f);
      rect(&img, (int)dispX - half, (int)dispY - half, (int)dispX + half, (int)dispY + half, COL_BLUE, 1);
    } else if (dispMiss > 10) {
      dispS = 0;                                    // next appearance starts fresh
      cropViewValid = false;
    }
    // detector view: the (contrast-boosted) square the face detector looked at, top-left corner
    if (cfg.showCrop && cropViewValid && cfg.mode == 0) {
      const int R = cfg.roiSize;
      const int ins = min(R, h / 2);
      for (int y = 0; y < ins; y++) {
        const uint8_t* srcRow = cropView + (size_t)(y * R / ins) * R * 3;
        uint8_t* dst = rgbBuf + (size_t)y * w * 3;
        for (int x = 0; x < ins; x++) {
          const uint8_t* q = srcRow + (x * R / ins) * 3;
          dst[x * 3] = q[0]; dst[x * 3 + 1] = q[1]; dst[x * 3 + 2] = q[2];
        }
      }
      rect(&img, 0, 0, ins, ins, COL_BLUE, 1);
    }
    static Det showDets[MAX_DETS];
    static int showN = 0, showMatched = -1;
    static uint32_t showMs = 0;
    if (ranFace) { showN = nDets; memcpy(showDets, dets, sizeof(Det) * nDets); showMatched = matched; showMs = millis(); }
    if (cfg.mode == 1 || millis() - showMs > 1000) showN = 0;   // no new face result for 1 s: gone
    for (int i = 0; i < showN; i++) {
      const Det& d = showDets[i];
      bool tracked = faceOk && i == showMatched;
      rect(&img, d.x1, d.y1, d.x2, d.y2, tracked ? COL_GREEN : COL_YELLOW, tracked ? 3 : 2);
      if (d.eyes) {
        dot(&img, d.lex, d.ley, COL_CYAN);
        dot(&img, d.rex, d.rey, COL_CYAN);
      }
    }
    if (src) {
      int px = (int)((nx + 1.0f) * w / 2.0f);
      int py = (int)((ny + 1.0f) * h / 2.0f);
      crosshair(&img, px, py, w / 24, COL_RED);
    }

    uint8_t* out = nullptr;
    size_t outLen = 0;
    if (fmt2jpg(rgbBuf, w * h * 3, w, h, PIXFORMAT_RGB888, STREAM_QUAL, &out, &outLen)) {
      xSemaphoreTake(jpgMutex, portMAX_DELAY);
      free(jpgBuf);
      jpgBuf = out; jpgLen = outLen;
      jpgSeq++;
      xSemaphoreGive(jpgMutex);
    }
  }

  int64_t now = esp_timer_get_time();
  {                                               // timing per step (smoothed), printed once a second
    auto sm = [](volatile float& acc, int64_t us) { acc = acc * 0.8f + (us / 1000.0f) * 0.2f; };
    sm(tCam, tB - tA); sm(tConv, tC - tB); sm(tRoom, tD - tC); sm(tFace, tE - tD); sm(tStream, now - tE);
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint >= 1000) {
      lastPrint = millis();
      logTracking("timing ms: camera %.0f  convert %.0f  room %.0f  face %.0f  stream %.0f  | face check %.0f  fps %.1f\n",
                    (float)tCam, (float)tConv, (float)tRoom, (float)tFace, (float)tStream, (float)statDetMs, (float)statFps);
    }
  }
  if (lastLoopUs) {
    float fps = 1e6f / (now - lastLoopUs);
    statFps = statFps * 0.9f + fps * 0.1f;
  }
  lastLoopUs = now;
}
