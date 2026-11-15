// Uncanny Eyes – https://www.youtube.com/@fluxandfur – https://github.com/fluxfur/uncannyeyes
// Copyright (c) 2026 @fluxandfur – MIT License (see LICENSE)
//
// web.h – WiFi, mDNS and the web server (page + settings on port 80, stream on port 81).
#pragma once

void wifiStart();                   // connect (waits up to WIFI_WAIT_MS, then goes on and keeps trying)
void wifiTick();                    // call from loop(): announce / reconnect
void printAddress();
bool wifiSerialCommand(char* line); // "wifi" / "wifi <name> <password>"; false = not a wifi command
void webStart();                    // start both servers
