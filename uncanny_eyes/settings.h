// Uncanny Eyes – https://www.youtube.com/@fluxandfur – https://github.com/fluxfur/uncannyeyes
// Copyright (c) 2026 @fluxandfur – MIT License (see LICENSE)
//
// settings.h – the eye settings (axes, eye sets, relax, behavior, vision behavior):
// sanity limits, flash storage, and the saved copy the page can go back to.
// The camera settings live in vision.cpp.
#pragma once

void loadCfg();                     // at boot
void saveCfg();                     // "Save" on the page
void sanitizeCfg();                 // clamp everything into its valid range
void revertToSaved();               // page reload: drop unsaved previews
const char* setKey(int i, const char* f);   // "s0cx", "s1on", ...
