# fMP3 Player 0.1.0: Console Skin for Tab5

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

The 0.1.0 release build produced:

- image project: `Tab5fMP3Player`
- image version: `0.1.0`
- target: ESP32-P4, 16 MB flash, minimum chip revision 1.0
- image size: `4,778,848` bytes (`0x48eb60`), 464,032 bytes below the slot limit
- SHA-256: `8c420bbc2f47de2b21387c82aaa227d70bdb8979fcdf45782505447223c5d3d6`
- maximum package size: 5,242,880 bytes
- package validation: ESP image magic, chip ID, revision, segment checksum and
  appended SHA-256
- on-device acceptance: required after installation from Console; a successful
  host build or SD copy is not physical display/audio acceptance
