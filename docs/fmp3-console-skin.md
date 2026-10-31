# fMP3 Player 0.1.1: Console Skin for Tab5

31 October 2026

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
errors, 191.6x realtime. The remaining acceptance test is the same track on the
physical Tab5 after installing 0.1.1.

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

The 0.1.1 release build produced:

- image project: `Tab5fMP3Player`
- image version: `0.1.1`
- target: ESP32-P4, 16 MB flash, minimum chip revision 1.0
- image size: `4,787,968` bytes (`0x490f00`), 454,912 bytes below the slot limit
- SHA-256: `3abeed45f721d542dfd56a59281df0bceff326922ec1a1d737dcefc1e4d303db`
- maximum package size: 5,242,880 bytes
- package validation: ESP image magic, chip ID, revision, segment checksum and
  appended SHA-256
- on-device acceptance: required after installation from Console; a successful
  host build or SD copy is not physical display/audio acceptance
