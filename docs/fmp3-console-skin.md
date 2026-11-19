# fMP3 Player 0.1.3: Console Skin for Tab5

19 November 2026

## Scope

`Tab5fMP3Player` is a fork of Hiroki Kawakami's MIT-licensed
`Tab5-Media-Player`, based on upstream tag `v2.2.0` (commit `ff39716`). The
media engine and supported-format surface are retained. This release changes
the presentation and SD delivery path for the Great Bespoke Tab5 Console.

## Appearance

- The Console neutral dark and light palettes are defined once in the
  `ui_theme` component and shared by the application and Tab5 UI widgets.
- Dark mode is the first-run default. **Settings → Display → Appearance**
  switches between Light and Dark; the selection is stored in NVS under the
  media player's `tab5mediaplayer` namespace.
- PixelFonts `7x5 thin`, `11x5 thin`, and `13x7 semibold` are rasterized at the
  Console scales used by the UI. The existing Noto Sans JP subset remains the
  fallback for Japanese filenames and metadata.
- App, settings, file browser, playlist, audio player and modal surfaces follow
  the selected theme. Immersive video and image viewing remain black for media
  contrast.
- The menu title and application descriptor identify the firmware as
  `fMP3 Player` / `Tab5fMP3Player`.

## SD Delivery

The resident Console partition map is treated as authoritative. This build
therefore:

- generates resources into the application instead of the upstream standalone
  `resources` partition;
- builds against the Console `0x8000` partition-table layout with 5 MiB OTA
  slots;
- blocks the normal ESP-IDF flash targets so the resident factory Console,
  shared partition table, NVS and factory C6 image cannot be overwritten by an
  accidental direct flash.

The package is installed from **Console → SD Apps** and launches into `ota_1`.

## 0.1.1 Playback Reset Repair

The first device run returned to Console during AAC/M4A playback. Investigation
found two device-runtime gaps and added diagnostics for any remaining fault:

- The app never confirmed its pending OTA image or selected Console as its next
  boot target. It now calls the shared `tab5_app_boot_ready()` lifecycle after
  first-frame presentation, matching the other SD applications, and returns to
  Console if confirmation does not complete within 15 seconds.
- `media_audio` executes the AAC/PIE decoder kernels on an 8 KiB SIMD-capable
  stack instead of a 4 KiB non-SIMD stack. The PCM output task stack is also
  enlarged to 8 KiB.
- On the first SD mount after each launch, the app appends the previous reset
  reason and heap snapshot to `/fmp3-diagnostics.log` on the card.

The supplied Africa M4A (`AAC-LC`, 48 kHz stereo) was decoded in full through
`audf_aac` under AddressSanitizer and UBSan in HE-v2 mode: 13,871 packets, 0
errors, 191.6x realtime. The physical Tab5 acceptance test also passed:
on 31 October 2026, after installing 0.1.1, the user confirmed that playback
worked as intended beyond the previous reset point.

## 0.1.2 Navigation and Physical Keyboard

- A **CONSOLE** action is available in the home, page, audio-player, video,
  image-viewer, camera-input and file-picker bars. It selects the verified
  Console boot target and restarts without erasing NVS or SD data.
- The M5Stack Tab5 clip-on keyboard is exposed as an LVGL keypad input through
  the BSP's existing HID provider.
- In the SD file browser, Up/Down moves the selection, Left/Backspace moves to
  the parent directory, and Return opens the selected item. Grid layout also
  accepts Left/Right between cells.
- Wi-Fi password entry no longer creates an on-screen keyboard. The physical
  Tab5 keyboard types directly into the password field; Enter connects and Esc
  cancels.

## 0.1.3 Listening Lock

- The audio player's volume row now has a lock action. It opens a full-screen,
  orientation-aware lock view with the current album art, title and
  artist/album metadata while playback continues.
- **SCREEN OFF** sets the display backlight to zero, puts the MIPI panel into
  sleep and places a full-panel input shield over the player. The framebuffer
  and saved brightness setting are retained for a cheap resume without a full
  redraw.
- The first screen tap or physical-keyboard key wakes the album-art lock view
  only. The shield is removed after that input finishes, preventing a wake tap
  from falling through to **UNLOCK**.
- **UNLOCK** is a separate action. On a keyboard, the first key wakes and a
  subsequent Enter activates the focused Unlock button.
- Returning to the player or leaving the screen always restores the configured
  brightness and releases the keyboard focus lock.
- The simulator verification covers portrait and landscape layout, full-black
  screen-off, keyboard wake/unlock and touch wake/unlock. After SD delivery, the
  user confirmed on 19 November 2026 that all worked as intended; the report
  did not specify individual test steps or the DAC model.

## Bluetooth Headphones

Native Bluetooth headphone output cannot be implemented on the stock Tab5
radio. Espressif's
[ESP32-C6 controller matrix](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-guides/bt-architecture/overview.html)
lists BLE support but not Bluetooth Classic BR/EDR, so the C6 cannot act as an
A2DP source for conventional headphones. The
[BLE feature matrix](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-guides/ble/overview.html)
also lists ISO and LE Audio as unsupported on ESP32-C6, so a BLE scan/pair
screen would not produce an audio route.

The supported architecture is a self-pairing, USB Audio Class Bluetooth
transmitter connected to USB-A:

1. Pair the headphones using the transmitter's own button and indicators.
2. Connect the transmitter to the Tab5 USB-A port.
3. fMP3 automatically changes the active route to **USB Audio**, including an
   already-playing stream, and uses either the adapter's volume feature or its
   existing software-gain fallback.

The M5Stack Tab5 Keyboard uses I2C, leaving USB-A available. A generic UAC
adapter does not expose the radio's paired-headphone state, so the firmware can
truthfully report the adapter connection but cannot claim that the headphones
behind it are paired. A 3.5 mm analogue Bluetooth transmitter is another
hardware-only fallback.

A USB-C DAC is also usable when connected to the Tab5's USB-A host port through
a USB-A-male to USB-C-female adapter. M5Stack documents the
[Tab5 USB-C connector](https://docs.m5stack.com/en/core/Tab5?id=schematics) as
the device/charging/flashing port; it is not the host port used by fMP3. The
current host driver accepts USB Audio Class 1 playback interfaces with
synchronous or adaptive output endpoints. UAC2-only devices and
asynchronous-feedback output endpoints require additional host-driver work and
will not be exposed as **USB Audio** in this release.

## Build Environment

The upstream project pins ESP-IDF `6.1.0` at commit
`fff9895c82d744c7237be8847347bdd1b07c6643`, plus esp-devkit's P4 PPA, CPU,
PSRAM and exFAT patches. Build with the repository's Nix environment when
available:

```sh
nix develop -c ./run.sh esp32p4 build
```

The Nix flake is authoritative because the BSP, PPA workaround and FatFs exFAT
support are patched there. A direct ESP-IDF build is only equivalent when the
same commit and the five patches under `esp-devkit/nix/patches/` are applied.

## Package Verification

The 0.1.3 release build produced:

- image project: `Tab5fMP3Player`
- image version: `0.1.3`
- target: ESP32-P4, 16 MB flash, minimum chip revision 1.0
- image size: `4,794,528` bytes (`0x4928a0`), 448,352 bytes below the slot limit
- SHA-256: `7c43a5e49f488a29b676abf5cb70c362abb23be467cf21f9f6bbef63f8e675ac`
- maximum package size: 5,242,880 bytes
- package validation: ESP image magic, chip ID, revision, segment checksum and
  appended SHA-256
- on-device acceptance: 0.1.1 passed on 31 October 2026; following 0.1.3 SD
  delivery, the user reported on 19 November 2026 that all worked as intended.
  No individual test steps or DAC model were specified.
