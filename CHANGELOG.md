# Changelog

## 1.0.0 – 2026-11
- First public release.
- New reaction group "When you come close and look" (a big enough face): Freeze, Shy, Twitch.
  Replaces "comes close" (person height); Shy replaces Hide and also works without lids.
- Lose interest is rolled again every "Standing still after" while they stay still.
- Fast follow uses the smoothed target; new defaults: Smoothing 0.3 s, Delayed follow 1.8 s,
  Lose interest 70 %.
- Lids are one fixed position (center only); Min/Max only limit the main eyes, so the lids
  can sit outside that range.
- Eye roll: one smooth round from a random start, random length and direction, curling into the
  center at the end; sized to stay inside Min/Max so it stays round.
- config.h for pins, hostname and serial output.
- Serial command `wifi <name> <password>`: changes the WiFi, saved only if it connects.
  The first boot after flashing takes the WiFi from secrets.h.
- Aim: offset and gain for each axis (replaces the single Tracking gain).
- Web page: Basic / Advanced view, step-by-step calibration, descriptions in the action tables.
- Settings are stored under new names ("eyes", "camera"): boards that ran an earlier
  development version start with defaults once.
