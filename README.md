# Uncanny Eyes

**A painting whose eyes follow you around the room.**

A small camera hidden in the picture frame finds people. Two servos move a sheet of printed eyes behind the painting, so the eyes look at whoever is in the room. When nobody is there, the eyes glance around, blink, roll and doze off on their own. When someone walks in, they react: they follow fast or with a creepy delay, do a double take, startle, freeze or get shy when you come close and look them in the eye, and stare after you when you leave.

Everything runs on one small board and is set up from a web page. No app and no cloud are needed.

**Video:** [Watch how it was made on YouTube](https://youtu.be/VIDEO_ID) <!-- replace VIDEO_ID with the video link -->

**Channel:** [@fluxandfur on YouTube](https://www.youtube.com/@fluxandfur)

<!-- GIF / photo of the painting coming soon -->

---

## Features

- Follows people with head tracking and face detection (aims between the eyes)
- Reacts to people: arriving, standing still, coming close and looking, leaving
- Idle behavior: glances, blinks, eye rolls, falls asleep
- Optional lids for blinking, sleeping and hiding
- Web page for calibration and all settings, with a live camera view
- Runs completely on the board, no internet needed

## Hardware

| Part | Notes |
|---|---|
| Seeed Studio XIAO ESP32S3 Sense | with the OV3660 camera (Sense add-on board); attach the WiFi antenna |
| 2× SG90 micro servo | one for left/right, one for up/down |
| 5 V power supply for the servos | separate from the board's USB power; 1 A or more |
| Picture frame | the painting in front, the eye sheet behind it |
| Printed painting + eye sheet | the painting with cut-out eyes, the eyes printed on a separate sheet behind it |
| Sliders, servo horns, push rods | move the eye sheet; prototype: 15 mm horn, about 55 mm rod |

Optional: a second eye set with closed lids, printed on the same sheet above the main eyes. It is used for blinking, sleeping and hiding.

## Wiring

| Servo | Wire | XIAO ESP32S3 |
|---|---|---|
| Servo 1 (left/right) | signal (orange) | **D0** |
| Servo 2 (up/down) | signal (orange) | **D1** |
| both | + (red) | external **5 V** (not the board's 5 V pin) |
| both | − (brown) | external GND **and** XIAO GND (common ground) |

Do not power the servos from the board. Their current spikes cause brownouts and camera glitches.

## Installation

1. **Download the code.** Click **Code → Download ZIP** on this page and unzip it (or clone the repository).
2. **Install the Arduino IDE** from [arduino.cc](https://www.arduino.cc/en/software).
3. **Install the esp32 board package 2.0.17.** In the Arduino IDE open **Boards Manager**, search for "esp32" by Espressif and install version **2.0.17**. Do not use 3.x: Espressif removed the face detection there.
4. **Select the board settings** in the **Tools** menu:
   - Board: `XIAO_ESP32S3`
   - PSRAM: `OPI PSRAM`
   - Partition Scheme: `8M with spiffs (3MB APP/1.5MB SPIFFS)` (or `Maximum APP`)
   - USB CDC On Boot: `Enabled`
5. **Enter your WiFi.** Open `uncanny_eyes/uncanny_eyes.ino`. In the tab `secrets.h`, enter your WiFi name and password. The board needs a 2.4 GHz network.
6. **Upload.** Connect the board with USB and click **Upload**.
7. **Find the board.** Open the Serial Monitor (115200 baud, line ending "Newline"). It shows the web address.
8. **Open the web page.** Go to **http://eyes.local/** in a browser on the same network. If that does not work (some Windows and Android devices do not support `.local`), use the IP address from the Serial Monitor.

No extra libraries are needed. Everything comes with the esp32 board package. Pins, hostname and serial output can be changed in `config.h`.

You can change the WiFi later without uploading again: type `wifi <name> <password>` in the Serial Monitor. A new upload sets the WiFi from `secrets.h` again.

## First start and calibration

1. Keep the camera's view **empty** for a few seconds after power-up. The board learns what the empty room looks like.
2. Open the **Calibration** card and follow the steps:
   - **Servo range:** tick **Free**, move each servo to where the mechanics stop and set **Min/Max** just inside. Use **Reverse** if an axis moves the wrong way.
   - **Lids** (optional): tick **This painting has lids**, select the **Lids** set, move until the lids cover the eyes and press **Center = current**.
   - **Center:** select **Main**, move the eyes until they look straight ahead and press **Center = current**.
   - **Size:** move to the left or right edge of the eye opening and press **Radius X = current**. Do the same up or down for **Radius Y = current**.
   - **Check:** untick **Free**. The joystick now stays inside the dashed circle.
   - Press **Save**.
3. Walk into view. The eyes should find you. If they look past you, tick **Advanced** and adjust **Aim** in the Vision behavior card (offset and gain for each axis), then press **Save**.

If the room changes (furniture moved, a different light), press **Relearn room**.

## Using the web page

- **Joystick:** steer the eyes by hand. Behavior pauses for 20 s; **Resume** continues right away.
- **Camera:** live view with what the board detects (magenta person, white head, blue face search, green face, red target).
- **Calibration:** servo range and eye sets, step by step.
- **Behavior:** what the eyes do on their own. Each action has a chance in % and a **Run** button to try it.
- **Vision behavior:** how the eyes react to people. One reaction per group by chance; if none is picked, the group title says what happens ("otherwise: …"). **Run** with someone in view plays the real reaction, otherwise a short demo.
- **Advanced** (bottom bar): shows the fine settings (timing, aim, tracking, detection tuning, exposure).

Eye settings take effect immediately but are only kept after **Save**. Camera settings are saved right away.

## Serial commands

Serial Monitor at 115200 baud, line ending "Newline":

| Command | Effect |
|---|---|
| *(empty line)* | print the web address |
| `ip` | network info |
| `wifi` | show the WiFi |
| `wifi <name> <password>` | connect to another WiFi; saved only if it works (name with spaces: `"My Home"`) |
| `relearn` | learn the empty room again |
| `gray 0` / `gray 1` | color / grayscale camera mode (reboots) |
| `defaults` | reset camera settings (reboots) |

## How it works

- **Vision** (core 1, about 5 fps): camera frame → model of the empty room → people as blobs → head estimate → face detection near the head to aim between the eyes.
- **Eyes** (core 0, every 5 ms): the behavior picks a target, and the servos move smoothly toward it.
- **Web server:** the page and settings on port 80, the live camera stream on port 81.

The camera part never touches the servos. It only publishes where a person is, so slow frames never make the eyes jerk.

## Files

| File | Contents |
|---|---|
| `uncanny_eyes.ino` | setup, loop, eyes task, serial commands |
| `config.h` | pins, hostname, serial output: the things you may want to change |
| `secrets.h` | your WiFi name and password |
| `core.h` | what all parts share: target, lock |
| `vision.h/.cpp` | camera tracking and live view |
| `motion.h/.cpp` | servos and eye geometry |
| `behavior.h/.cpp` | idle actions, sleep, reactions to people |
| `settings.h/.cpp` | eye settings and flash storage |
| `web.h/.cpp` | WiFi, mDNS, web server |
| `page.h` | the web page |

## Troubleshooting

- **Eyes do not react:** relearn the room with nobody in view, and check that the Camera card shows a box around you.
- **Eyes look past you:** adjust **Aim** (Advanced, Vision behavior card) while standing where people usually stand.
- **Faces are rarely found:** strong light from behind or above darkens faces. Light from the front helps. Head tracking still works without a face.
- **Servos buzz or stall:** narrow Min/Max to the real mechanical range, and check the 5 V supply and the common GND.
- **eyes.local not found:** use the IP address from the Serial Monitor.
- **No WiFi:** check name and password with the `wifi` command, and use a 2.4 GHz network.
- **Does not compile / face detection missing:** make sure the esp32 board package is 2.0.17, not 3.x.

## Credits

- Face detection: [ESP-DL](https://github.com/espressif/esp-dl) human face detection models by Espressif, included in the esp32 Arduino core 2.0.x.
- [esp32-camera](https://github.com/espressif/esp32-camera) driver and the [Arduino-ESP32](https://github.com/espressif/arduino-esp32) core by Espressif.

These come with the esp32 board package under their own licenses. None of their code is copied into this repository.

## License

MIT, see [LICENSE](LICENSE). Free to use, change and share. Please keep the copyright notice.

---

Made by [@fluxandfur](https://www.youtube.com/@fluxandfur). If you build one, I would love to see it!
