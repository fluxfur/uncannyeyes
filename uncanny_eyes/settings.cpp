// Uncanny Eyes – https://www.youtube.com/@fluxandfur – https://github.com/fluxfur/uncannyeyes
// Copyright (c) 2026 @fluxandfur – MIT License (see LICENSE)
//
// settings.cpp – eye settings: limits, flash storage (Preferences namespace "eyes"),
// and the saved copy. The live values may hold unsaved previews from the page; the
// saved ones only change on Save or at boot.
#include <Preferences.h>
#include "settings.h"
#include "motion.h"
#include "behavior.h"

static Preferences prefs;

static int    sMin1 = 0, sMax1 = 180, sMin2 = 0, sMax2 = 180;
static bool   sInv1 = true, sInv2 = true;
static EyeSet sSets[NUM_SETS];
static bool   sHasLids = true;
static bool   sRelaxOn = true;
static int    sRelaxMs = 1500;
static Behave sBeh;
static Watch  sWatch;

void sanitizeCfg() {
  min1 = constrain(min1, 0, 180); max1 = constrain(max1, 0, 180);
  min2 = constrain(min2, 0, 180); max2 = constrain(max2, 0, 180);
  if (min1 > max1) { int t = min1; min1 = max1; max1 = t; }
  if (min2 > max2) { int t = min2; min2 = max2; max2 = t; }
  for (int i = 0; i < NUM_SETS; i++) {
    sets[i].cx = constrain(sets[i].cx, 0, 180);
    sets[i].cy = constrain(sets[i].cy, 0, 180);
    sets[i].rx = constrain(sets[i].rx, 0, 90);
    sets[i].ry = constrain(sets[i].ry, 0, 90);
  }
  relaxMs = constrain(relaxMs, 200, 60000);
  if (!hasLids) curSet = 0;
  beh.on      = beh.on ? 1 : 0;
  beh.ivMin   = constrain(beh.ivMin, 1, 600);
  beh.ivMax   = constrain(beh.ivMax, beh.ivMin, 600);
  beh.speed   = constrain(beh.speed, 10, 600);
  beh.glMin   = constrain(beh.glMin, 0, 100);
  beh.holdMin = constrain(beh.holdMin, 0, 20000);
  beh.holdMax = constrain(beh.holdMax, beh.holdMin, 20000);
  beh.awake   = constrain(beh.awake, 0, 3600);
  beh.sleepS  = constrain(beh.sleepS, 1, 3600);
  for (int i = 0; i < NUM_ACTS; i++) beh.w[i] = constrain(beh.w[i], 0, 100);
  watch.on       = watch.on ? 1 : 0;
  watch.multi    = watch.multi ? 1 : 0;
  watch.wake     = watch.wake ? 1 : 0;
  watch.delayMs  = constrain(watch.delayMs, 100, 10000);
  watch.smoothMs = constrain(watch.smoothMs, 0, 2000);
  watch.gainX    = constrain(watch.gainX, 30, 300);
  watch.gainY    = constrain(watch.gainY, 30, 300);
  watch.offX     = constrain(watch.offX, -50, 50);
  watch.offY     = constrain(watch.offY, -50, 50);
  watch.speed    = constrain(watch.speed, 10, 600);
  watch.db       = constrain(watch.db, 0, 10);
  watch.noticeMs = constrain(watch.noticeMs, 0, 10000);
  watch.stillS   = constrain(watch.stillS, 1, 60);
  watch.closePct = constrain(watch.closePct, 2, 60);
  watch.lostS    = constrain(watch.lostS, 0, 60);
  for (int i = 0; i < NUM_VACTS; i++) watch.ch[i] = constrain(watch.ch[i], 0, 100);
}

static void commitSaved() {
  sMin1 = min1; sMax1 = max1; sInv1 = inv1;
  sMin2 = min2; sMax2 = max2; sInv2 = inv2;
  for (int i = 0; i < NUM_SETS; i++) sSets[i] = sets[i];
  sHasLids = hasLids;
  sRelaxOn = relaxOn;  sRelaxMs = relaxMs;
  sBeh = beh;
  sWatch = watch;
}

void revertToSaved() {
  bool lidsWereOn = hasLids, wasOn = beh.on, watchWasOn = watch.on, relaxWasOn = relaxOn;
  int oldAwake = beh.awake;
  min1 = sMin1; max1 = sMax1; inv1 = sInv1;
  min2 = sMin2; max2 = sMax2; inv2 = sInv2;
  for (int i = 0; i < NUM_SETS; i++) sets[i] = sSets[i];
  hasLids = sHasLids;
  relaxOn = sRelaxOn;  relaxMs = sRelaxMs;
  beh = sBeh;
  watch = sWatch;
  sanitizeCfg();
  applyChanges(lidsWereOn, oldAwake, wasOn, watchWasOn, relaxWasOn);
}

const char* setKey(int i, const char* f) {
  static char k[8];
  snprintf(k, sizeof(k), "s%d%s", i, f);
  return k;
}

void loadCfg() {
  prefs.begin("eyes", true);       // read-only
  min1 = prefs.getInt("min1", 0);
  max1 = prefs.getInt("max1", 180);
  inv1 = prefs.getBool("inv1", true);
  min2 = prefs.getInt("min2", 0);
  max2 = prefs.getInt("max2", 180);
  inv2 = prefs.getBool("inv2", true);
  for (int i = 0; i < NUM_SETS; i++) {
    sets[i].cx = prefs.getInt(setKey(i, "cx"), DEFAULT_SETS[i].cx);
    sets[i].cy = prefs.getInt(setKey(i, "cy"), DEFAULT_SETS[i].cy);
    sets[i].rx = prefs.getInt(setKey(i, "rx"), DEFAULT_SETS[i].rx);
    sets[i].ry = prefs.getInt(setKey(i, "ry"), DEFAULT_SETS[i].ry);
  }
  hasLids = prefs.getBool(setKey(1, "on"), true);
  relaxOn = prefs.getBool("relax", true);
  relaxMs = prefs.getInt("relaxms", 1500);
  beh = DEFAULT_BEH;
  if (prefs.getBytesLength("beh") == sizeof(Behave)) prefs.getBytes("beh", &beh, sizeof(Behave));
  watch = DEFAULT_WATCH;
  if (prefs.getBytesLength("watch") == sizeof(Watch)) prefs.getBytes("watch", &watch, sizeof(Watch));
  prefs.end();
  sanitizeCfg();
  commitSaved();
}

void saveCfg() {
  prefs.begin("eyes", false);
  prefs.putInt("min1", min1);
  prefs.putInt("max1", max1);
  prefs.putBool("inv1", inv1);
  prefs.putInt("min2", min2);
  prefs.putInt("max2", max2);
  prefs.putBool("inv2", inv2);
  for (int i = 0; i < NUM_SETS; i++) {
    prefs.putInt(setKey(i, "cx"), sets[i].cx);
    prefs.putInt(setKey(i, "cy"), sets[i].cy);
    prefs.putInt(setKey(i, "rx"), sets[i].rx);
    prefs.putInt(setKey(i, "ry"), sets[i].ry);
  }
  prefs.putBool(setKey(1, "on"), hasLids);
  prefs.putBool("relax", relaxOn);
  prefs.putInt("relaxms", relaxMs);
  prefs.putBytes("beh", &beh, sizeof(Behave));
  prefs.putBytes("watch", &watch, sizeof(Watch));
  prefs.end();
  commitSaved();
  Serial.println("settings saved to flash");
}
