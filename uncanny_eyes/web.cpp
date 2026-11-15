// Uncanny Eyes – https://www.youtube.com/@fluxandfur – https://github.com/fluxfur/uncannyeyes
// Copyright (c) 2026 @fluxandfur – MIT License (see LICENSE)
//
// web.cpp – WiFi, mDNS and the web server.
// Port 80: the page and all settings (one server task; handlers are short and take eyeLock
// around the eye state). Port 81: the MJPEG stream (it blocks its task while someone watches).
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "esp_ota_ops.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "secrets.h"
#include "web.h"
#include "core.h"
#include "motion.h"
#include "behavior.h"
#include "settings.h"
#include "vision.h"
#include "page.h"

#if !defined(WIFI_SSID_VALUE) || !defined(WIFI_PASS_VALUE)
#error "secrets.h must define WIFI_SSID_VALUE and WIFI_PASS_VALUE"
#endif

static httpd_handle_t webServer = nullptr;
static httpd_handle_t streamServer = nullptr;

// =====================================================================
//  WiFi
// =====================================================================

void printAddress() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Open  http://%s/   or   http://" HOSTNAME ".local/\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("WiFi not connected (check name/password: command wifi; 2.4 GHz network only)");
  }
}

// Starts mDNS and prints the address the first time WiFi is up. Returns true while connected.
static bool networkUp() {
  static bool announced = false;
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!announced) {
    announced = true;
    if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
    printAddress();
  }
  return true;
}

// The WiFi name and password live in one place: Preferences namespace "wifi". On the first
// boot after flashing (the firmware fingerprint changed) they are taken from secrets.h.
// The serial command "wifi" can change them later.
static char wifiSsid[33], wifiPass[65];

static void loadWifi() {
  strlcpy(wifiSsid, WIFI_SSID_VALUE, sizeof(wifiSsid));   // fallback if flash can't be read
  strlcpy(wifiPass, WIFI_PASS_VALUE, sizeof(wifiPass));
  char build[17] = "";
  esp_ota_get_app_elf_sha256(build, sizeof(build));       // fingerprint of this firmware
  Preferences p;
  if (!p.begin("wifi", false)) return;
  if (p.getString("build", "") != build) {                // new firmware: take secrets.h
    p.putString("ssid", wifiSsid);
    p.putString("pass", wifiPass);
    p.putString("build", build);
  }
  p.getString("ssid", wifiSsid, sizeof(wifiSsid));
  p.getString("pass", wifiPass, sizeof(wifiPass));
  p.end();
}

static void saveWifi() {
  Preferences p;
  if (!p.begin("wifi", false)) return;
  p.putString("ssid", wifiSsid);
  p.putString("pass", wifiPass);
  p.end();
}

// Connects to wifiSsid / wifiPass and waits up to WIFI_WAIT_MS. Returns true if connected.
static bool connectWifi() {
  WiFi.begin(wifiSsid, wifiPass);
  Serial.print("Connecting to WiFi");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_WAIT_MS) { delay(300); Serial.print("."); }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

void wifiStart() {
  WiFi.setHostname(HOSTNAME);        // name shown in the router's device list
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);              // the stream and the page stay responsive
  WiFi.setAutoReconnect(true);
  loadWifi();
  connectWifi();
  if (!networkUp()) {
    Serial.println("WiFi not connected yet - eyes and tracking start anyway, WiFi keeps trying");
    Serial.println("To set the WiFi, type: wifi <name> <password>");
  }
}

// Next word of a command line; "quotes" allow spaces. Returns nullptr at the end of the line.
static char* nextWord(char*& p) {
  while (*p == ' ') p++;
  if (!*p) return nullptr;
  char end = ' ';
  if (*p == '"') { end = '"'; p++; }
  char* w = p;
  while (*p && *p != end) p++;
  if (*p) *p++ = 0;
  return w;
}

// "wifi" shows the WiFi. "wifi <name> <password>" tries it and saves it only if it connects.
// A name with spaces needs quotes: wifi "My Home" secret. No password = open network.
bool wifiSerialCommand(char* line) {
  if (strncmp(line, "wifi", 4) || (line[4] && line[4] != ' ')) return false;
  char* p = line + 4;
  char* ssid = nextWord(p);
  if (!ssid) {
    if (WiFi.status() == WL_CONNECTED) Serial.printf("WiFi %s: connected, RSSI %d dBm\n", wifiSsid, (int)WiFi.RSSI());
    else Serial.printf("WiFi %s: not connected. To change it: wifi <name> <password>\n", wifiSsid);
    return true;
  }
  while (*p == ' ') p++;
  char* pass = (*p == '"') ? nextWord(p) : p;             // without quotes: the rest of the line
  if (strlen(ssid) > 32 || strlen(pass) > 63) {
    Serial.println("WiFi name: max 32 characters, password: max 63");
    return true;
  }
  char oldSsid[33], oldPass[65];
  strlcpy(oldSsid, wifiSsid, sizeof(oldSsid));
  strlcpy(oldPass, wifiPass, sizeof(oldPass));
  strlcpy(wifiSsid, ssid, sizeof(wifiSsid));
  strlcpy(wifiPass, pass, sizeof(wifiPass));
  WiFi.disconnect();
  if (connectWifi()) {
    saveWifi();
    Serial.println("Connected and saved");
    printAddress();
  } else {
    Serial.println("Could not connect - not saved, back to the previous WiFi");
    strlcpy(wifiSsid, oldSsid, sizeof(wifiSsid));
    strlcpy(wifiPass, oldPass, sizeof(wifiPass));
    WiFi.begin(wifiSsid, wifiPass);
  }
  return true;
}

// Reconnect quietly if the connection dropped (checked twice a second)
void wifiTick() {
  static uint32_t lastCheck = 0, lastReconnect = 0;
  if (millis() - lastCheck < 500) return;
  lastCheck = millis();
  if (!networkUp() && millis() - lastReconnect > 15000) {
    lastReconnect = millis();
    Serial.println("WiFi not connected - reconnecting");
    WiFi.reconnect();
  }
}

// =====================================================================
//  Request helpers
// =====================================================================

// All port-80 handlers run in the one server task, so one argument buffer is enough.
static char qbuf[1024];

// Loads the arguments: the URL query (GET) or the body (POST, used for the long /cfg).
static bool loadArgs(httpd_req_t* req) {
  qbuf[0] = 0;
  if (req->method == HTTP_POST) {
    int len = req->content_len;
    if (len >= (int)sizeof(qbuf)) return false;
    int got = 0;
    while (got < len) {
      int r = httpd_req_recv(req, qbuf + got, len - got);
      if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
      if (r <= 0) return false;
      got += r;
    }
    qbuf[got] = 0;
    return true;
  }
  size_t qlen = httpd_req_get_url_query_len(req);
  if (qlen >= sizeof(qbuf)) return false;
  if (qlen && httpd_req_get_url_query_str(req, qbuf, sizeof(qbuf)) != ESP_OK) qbuf[0] = 0;
  return true;
}
static bool  hasArg(const char* k) { char v[24]; return httpd_query_key_value(qbuf, k, v, sizeof(v)) == ESP_OK; }
static int   argInt(const char* k, int def) {
  char v[24];
  return httpd_query_key_value(qbuf, k, v, sizeof(v)) == ESP_OK ? atoi(v) : def;
}
static float argFloat(const char* k, float def) {
  char v[24];
  return httpd_query_key_value(qbuf, k, v, sizeof(v)) == ESP_OK ? (float)atof(v) : def;
}

static esp_err_t sendJson(httpd_req_t* req, const char* json) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}
static esp_err_t sendBad(httpd_req_t* req) {
  httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing args");
  return ESP_OK;
}

// movement / settings requests answer with the target angles (eyeLock held)
static void anglesJson(char* buf, size_t len) {
  snprintf(buf, len, "{\"a1\":%d,\"a2\":%d,\"st\":\"%s\"}",
           (int)lroundf(tgt[0]), (int)lroundf(tgt[1]), behaviorStatus());
}

// eye part of the live status (eyeLock held); returns the length, leaves the object open
static int eyeStatusJson(char* buf, size_t len) {
  return snprintf(buf, len, "{\"a1\":%d,\"a2\":%d,\"lx\":%.3f,\"ly\":%.3f,\"set\":%d,\"st\":\"%s\",\"ftest\":%d",
                  (int)lroundf(pos[0]), (int)lroundf(pos[1]), lookX, lookY, curSet, behaviorStatus(),
                  followTest ? 1 : 0);
}

static esp_err_t sendEyeStatus(httpd_req_t* req) {     // eyeLock held on entry, released here
  char out[220];
  int len = eyeStatusJson(out, sizeof(out));
  unlockEyes();
  snprintf(out + len, sizeof(out) - len, "}");
  return sendJson(req, out);
}

// =====================================================================
//  Eye handlers
// =====================================================================

static esp_err_t pageHandler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, PAGE_HTML, HTTPD_RESP_USE_STRLEN);
}

// Optional &n=.. on manual moves: the eye set selected on the page, so the
// board and the page always agree (an action may have switched sets)
static void manualSetArg() {
  if (!hasArg("n")) return;
  int n = constrain(argInt("n", 0), 0, NUM_SETS - 1);
  if (n == 0 || hasLids) curSet = n;
}

// Both servos directly: /both?a=90&b=45&n=0  (Main: clamped to Min/Max, Lids: 0..180; no ellipse)
static esp_err_t bothHandler(httpd_req_t* req) {
  if (!loadArgs(req) || !hasArg("a") || !hasArg("b")) return sendBad(req);
  char out[128];
  lockEyes();
  manualTakeover();
  manualSetArg();
  lookMode = false;
  jumpTo(argInt("a", 90), argInt("b", 90));
  lookFromAngles(tgt[0], tgt[1]);             // joystick knob follows the sliders
  anglesJson(out, sizeof(out));
  unlockEyes();
  return sendJson(req, out);
}

// Look direction: /look?x=0.5&y=-0.2&n=0  (-1..+1, limited to the eye set's ellipse)
static esp_err_t lookHandler(httpd_req_t* req) {
  if (!loadArgs(req) || !hasArg("x") || !hasArg("y")) return sendBad(req);
  char out[128];
  lockEyes();
  manualTakeover();
  manualSetArg();
  manualLook(argFloat("x", 0), argFloat("y", 0));
  anglesJson(out, sizeof(out));
  unlockEyes();
  return sendJson(req, out);
}

// Select eye set: /set?n=1&x=0&y=0  (also sets the look direction)
static esp_err_t eyeSetHandler(httpd_req_t* req) {
  if (!loadArgs(req) || !hasArg("n")) return sendBad(req);
  char out[128];
  lockEyes();
  manualTakeover();
  int n = constrain(argInt("n", 0), 0, NUM_SETS - 1);
  if (n == 0 || hasLids) curSet = n;          // Lids ignored if the painting has none
  manualLook(argFloat("x", 0), argFloat("y", 0));   // missing x/y -> 0 = center
  anglesJson(out, sizeof(out));
  unlockEyes();
  return sendJson(req, out);
}

// Run an action now: /act?n=0..6 idle actions, 20..29 vision reactions (Run demos),
// 100 = sleep, 101 = wake. Works even when behavior is off.
static esp_err_t actHandler(httpd_req_t* req) {
  loadArgs(req);
  lockEyes();
  runAction(argInt("n", -1));
  return sendEyeStatus(req);
}

// End the manual pause now: /resume
static esp_err_t resumeHandler(httpd_req_t* req) {
  lockEyes();
  endPause();
  return sendEyeStatus(req);
}

// Test: just follow, nothing else: /test?follow=0|1 (not saved)
static esp_err_t testHandler(httpd_req_t* req) {
  loadArgs(req);
  lockEyes();
  setFollowTest(argInt("follow", 0) == 1);
  return sendEyeStatus(req);
}

// Live status: eyes + camera. /status?hold=1 (sent while Free is ticked) keeps the
// behavior paused, so it doesn't move the eyes during calibration.
static esp_err_t statusHandler(httpd_req_t* req) {
  loadArgs(req);
  char out[560];
  lockEyes();
  if (argInt("hold", 0) == 1) holdPause();
  int len = eyeStatusJson(out, sizeof(out));
  unlockEyes();
  VisionStatus v;
  visionStatus(v);
  snprintf(out + len, sizeof(out) - len,
           ",\"src\":%d,\"x\":%.2f,\"y\":%.2f,\"size\":%.2f,\"fps\":%.1f,\"det\":%.0f,\"temp\":%.1f,"
           "\"bg\":%d,\"wait\":%d,\"people\":%d,\"tm\":[%.0f,%.0f,%.0f,%.0f,%.0f]}",
           v.src, v.x, v.y, v.size, v.fps, v.detMs, v.temp, v.bg, v.wait ? 1 : 0, v.people,
           v.tCam, v.tConv, v.tRoom, v.tFace, v.tStream);
  return sendJson(req, out);
}

// Apply settings, optionally persisting them (POST body or GET query):
// min1=..&max1=..&inv1=0|1&min2=..&max2=..&inv2=0|1&save=0|1
//   [&s0cx=..&s0cy=..&s0rx=..&s0ry=.. per eye set, &s1on=0|1 (lids)]
//   [&relax=0|1&relaxms=..]
//   [&bon=0|1&ivmin=..&ivmax=..&spd=..&glmin=..&hmin=..&hmax=..&awake=..&sleep=..&w0..w6=..]
//   [&won=0|1&wmulti=0|1&wdms=..&wspd=..&wdb=..&wsm=..&wnot=..&wstill=..&wclose=..
//    &wlost=..&wwake=0|1&c0..c9=..&wgx=..&wgy=..&wox=..&woy=..]
static esp_err_t cfgHandler(httpd_req_t* req) {
  if (!loadArgs(req)) return sendBad(req);
  const char* need[] = {"min1", "max1", "inv1", "min2", "max2", "inv2"};
  for (const char* k : need) if (!hasArg(k)) return sendBad(req);

  char out[128], k[6];
  lockEyes();
  min1 = argInt("min1", min1);
  max1 = argInt("max1", max1);
  inv1 = argInt("inv1", 0) == 1;
  min2 = argInt("min2", min2);
  max2 = argInt("max2", max2);
  inv2 = argInt("inv2", 0) == 1;
  for (int i = 0; i < NUM_SETS; i++) {
    sets[i].cx = argInt(setKey(i, "cx"), sets[i].cx);
    sets[i].cy = argInt(setKey(i, "cy"), sets[i].cy);
    sets[i].rx = argInt(setKey(i, "rx"), sets[i].rx);
    sets[i].ry = argInt(setKey(i, "ry"), sets[i].ry);
  }
  bool lidsWereOn = hasLids, relaxWasOn = relaxOn;
  hasLids = argInt("s1on", hasLids) == 1;
  relaxOn = argInt("relax", relaxOn) == 1;
  relaxMs = argInt("relaxms", relaxMs);
  int oldAwake = beh.awake;
  bool wasOn = beh.on, watchWasOn = watch.on;
  beh.on      = argInt("bon", beh.on);
  beh.ivMin   = argInt("ivmin", beh.ivMin);
  beh.ivMax   = argInt("ivmax", beh.ivMax);
  beh.speed   = argInt("spd", beh.speed);
  beh.glMin   = argInt("glmin", beh.glMin);
  beh.holdMin = argInt("hmin", beh.holdMin);
  beh.holdMax = argInt("hmax", beh.holdMax);
  beh.awake   = argInt("awake", beh.awake);
  beh.sleepS  = argInt("sleep", beh.sleepS);
  for (int i = 0; i < NUM_ACTS; i++) {
    snprintf(k, sizeof(k), "w%d", i);
    beh.w[i] = argInt(k, beh.w[i]);
  }
  watch.on       = argInt("won", watch.on);
  watch.multi    = argInt("wmulti", watch.multi);
  watch.delayMs  = argInt("wdms", watch.delayMs);
  watch.smoothMs = argInt("wsm", watch.smoothMs);
  watch.gainX    = argInt("wgx", watch.gainX);
  watch.gainY    = argInt("wgy", watch.gainY);
  watch.offX     = argInt("wox", watch.offX);
  watch.offY     = argInt("woy", watch.offY);
  watch.speed    = argInt("wspd", watch.speed);
  watch.db       = argInt("wdb", watch.db);
  watch.noticeMs = argInt("wnot", watch.noticeMs);
  watch.stillS   = argInt("wstill", watch.stillS);
  watch.closePct = argInt("wclose", watch.closePct);
  watch.lostS    = argInt("wlost", watch.lostS);
  watch.wake     = argInt("wwake", watch.wake);
  for (int i = 0; i < NUM_VACTS; i++) {
    snprintf(k, sizeof(k), "c%d", i);
    watch.ch[i] = argInt(k, watch.ch[i]);
  }
  sanitizeCfg();
  applyChanges(lidsWereOn, oldAwake, wasOn, watchWasOn, relaxWasOn);
  if (argInt("save", 0) == 1) saveCfg();
  anglesJson(out, sizeof(out));
  unlockEyes();
  return sendJson(req, out);
}

// Called when the page loads: reports the SAVED config and discards any
// unsaved preview that was still active on the board.
static esp_err_t stateHandler(httpd_req_t* req) {
  char buf[1200];
  lockEyes();
  revertToSaved();
  int n = snprintf(buf, sizeof(buf),
           "{\"a1\":%d,\"a2\":%d,\"min1\":%d,\"max1\":%d,\"inv1\":%d,"
           "\"min2\":%d,\"max2\":%d,\"inv2\":%d,\"set\":%d,\"lx\":%.3f,\"ly\":%.3f,"
           "\"relax\":%d,\"relaxms\":%d,\"lids\":%d,\"ftest\":%d,\"st\":\"%s\",\"sets\":[",
           (int)lroundf(pos[0]), (int)lroundf(pos[1]), min1, max1, inv1 ? 1 : 0, min2, max2, inv2 ? 1 : 0,
           curSet, lookX, lookY, relaxOn ? 1 : 0, relaxMs, hasLids ? 1 : 0, followTest ? 1 : 0,
           behaviorStatus());
  for (int i = 0; i < NUM_SETS; i++)
    n += snprintf(buf + n, sizeof(buf) - n, "%s[%d,%d,%d,%d]", i ? "," : "",
                  sets[i].cx, sets[i].cy, sets[i].rx, sets[i].ry);
  n += snprintf(buf + n, sizeof(buf) - n,
                "],\"beh\":{\"on\":%d,\"ivmin\":%d,\"ivmax\":%d,\"spd\":%d,\"glmin\":%d,"
                "\"hmin\":%d,\"hmax\":%d,\"awake\":%d,\"sleep\":%d,\"w\":[",
                beh.on, beh.ivMin, beh.ivMax, beh.speed, beh.glMin,
                beh.holdMin, beh.holdMax, beh.awake, beh.sleepS);
  for (int i = 0; i < NUM_ACTS; i++)
    n += snprintf(buf + n, sizeof(buf) - n, "%s%d", i ? "," : "", beh.w[i]);
  n += snprintf(buf + n, sizeof(buf) - n,
                "]},\"wat\":{\"on\":%d,\"multi\":%d,\"dms\":%d,\"spd\":%d,\"db\":%d,\"sm\":%d,"
                "\"not\":%d,\"still\":%d,\"close\":%d,\"lost\":%d,\"wake\":%d,\"ch\":[",
                watch.on, watch.multi, watch.delayMs, watch.speed, watch.db, watch.smoothMs,
                watch.noticeMs, watch.stillS, watch.closePct, watch.lostS, watch.wake);
  for (int i = 0; i < NUM_VACTS; i++)
    n += snprintf(buf + n, sizeof(buf) - n, "%s%d", i ? "," : "", watch.ch[i]);
  snprintf(buf + n, sizeof(buf) - n, "],\"gx\":%d,\"gy\":%d,\"ox\":%d,\"oy\":%d}}",
           watch.gainX, watch.gainY, watch.offX, watch.offY);
  unlockEyes();
  return sendJson(req, buf);
}

// =====================================================================
//  Camera handlers
// =====================================================================

static esp_err_t camSettingsHandler(httpd_req_t* req) {
  char json[512];
  visionSettingsJson(json, sizeof(json));
  return sendJson(req, json);
}

static esp_err_t camSetHandler(httpd_req_t* req) {
  if (!loadArgs(req) || !qbuf[0]) return sendBad(req);
  visionSetFromQuery(qbuf);
  char json[512];
  visionSettingsJson(json, sizeof(json));
  return sendJson(req, json);
}

static esp_err_t camRelearnHandler(httpd_req_t* req) {
  visionRequestRelearn();
  return sendJson(req, "{\"ok\":true}");
}

#define PART_BOUNDARY "uncannyeyesframe"
static esp_err_t streamHandler(httpd_req_t* req) {
  esp_err_t res = httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=" PART_BOUNDARY);
  if (res != ESP_OK) return res;
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  visionStreamClient(+1);
  uint8_t* local = nullptr;
  size_t cap = 0;
  uint32_t lastSeq = 0;
  int64_t lastFrameUs = esp_timer_get_time();
  char hdr[96];

  while (true) {
    int len = visionCopyJpeg(lastSeq, local, cap);
    if (len < 0) break;                                          // out of memory
    if (len == 0) {
      if (esp_timer_get_time() - lastFrameUs > 5000000) break;   // nothing for 5 s
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    lastFrameUs = esp_timer_get_time();
    int h = snprintf(hdr, sizeof(hdr),
                     "\r\n--" PART_BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                     (unsigned)len);
    res = httpd_resp_send_chunk(req, hdr, h);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)local, len);
    if (res != ESP_OK) break;                                    // browser closed
  }

  free(local);
  visionStreamClient(-1);
  return res;
}

// =====================================================================
//  Servers
// =====================================================================

void webStart() {
  httpd_config_t c = HTTPD_DEFAULT_CONFIG();
  c.server_port = 80;
  c.max_uri_handlers = 16;
  c.stack_size = 8192;                 // /state builds its JSON on the stack
  c.lru_purge_enable = true;           // stale browser tabs can't block the server: oldest connection is closed
  static const httpd_uri_t uris[] = {
    { "/",             HTTP_GET,  pageHandler,        nullptr },
    { "/both",         HTTP_GET,  bothHandler,        nullptr },
    { "/look",         HTTP_GET,  lookHandler,        nullptr },
    { "/set",          HTTP_GET,  eyeSetHandler,      nullptr },
    { "/act",          HTTP_GET,  actHandler,         nullptr },
    { "/resume",       HTTP_GET,  resumeHandler,      nullptr },
    { "/test",         HTTP_GET,  testHandler,        nullptr },
    { "/status",       HTTP_GET,  statusHandler,      nullptr },
    { "/cfg",          HTTP_POST, cfgHandler,         nullptr },
    { "/cfg",          HTTP_GET,  cfgHandler,         nullptr },
    { "/state",        HTTP_GET,  stateHandler,       nullptr },
    { "/cam/settings", HTTP_GET,  camSettingsHandler, nullptr },
    { "/cam/set",      HTTP_GET,  camSetHandler,      nullptr },
    { "/cam/relearn",  HTTP_GET,  camRelearnHandler,  nullptr },
  };
  if (httpd_start(&webServer, &c) == ESP_OK) {
    for (const httpd_uri_t& u : uris) httpd_register_uri_handler(webServer, &u);
  } else {
    Serial.println("Web server start FAILED");
  }

  httpd_config_t s = HTTPD_DEFAULT_CONFIG();
  s.server_port = 81;
  s.ctrl_port = c.ctrl_port + 1;
  s.max_open_sockets = 3;
  s.lru_purge_enable = true;
  httpd_uri_t streamUri = { "/stream", HTTP_GET, streamHandler, nullptr };
  if (httpd_start(&streamServer, &s) == ESP_OK) httpd_register_uri_handler(streamServer, &streamUri);
}
