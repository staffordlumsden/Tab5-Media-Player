# fMP3 Player for M5Stack Tab5

This is Stafford Lumsden's **fMP3 Player** fork of
[`Hiroki-Kawakami/Tab5-Media-Player`](https://github.com/Hiroki-Kawakami/Tab5-Media-Player).
It keeps the upstream video, audio and image engine while applying the Great
Bespoke Tab5 Console interface language:

- CYD PixelFonts for navigation, labels, controls and app chrome, with the
  existing Noto Japanese subset retained as a fallback for media metadata.
- Console dark and light palettes, persisted in NVS and selectable from
  **Settings → Display → Appearance**.
- Console blue (`0x3b82f6`), green (`0x22c55e`), amber and red accents.
- An SD-installable `Tab5fMP3Player` application image that carries its fonts
  and icons inside the application, so it runs from the resident Console's
  5 MiB OTA slot without an upstream `resources` partition.

Build and SD installation details are in
[`docs/fmp3-console-skin.md`](docs/fmp3-console-skin.md).

**Physical acceptance:** fMP3 Player `0.1.1` was accepted on the Tab5 on
31 October 2026 after the AAC/M4A playback reset was fixed and the user
confirmed the firmware worked as intended.

Version `0.1.2` adds an explicit Console return action, Tab5 Keyboard file
navigation, and physical-keyboard Wi-Fi password entry without the software
keyboard.

The original upstream project documentation follows.

---

Open Video, Audio and Image files on M5Stack Tab5 (ESP32-P4).

## Supported Media Formats

### Video

Convert videos in your browser with the
[converter](https://hiroki-kawakami.github.io/Tab5-Media-Player/).
The converter works only in Chrome and Chromium-based browsers.  
The command-line version, `tab5conv`, is on the
[`converter-latest`](https://github.com/Hiroki-Kawakami/Tab5-Media-Player/releases/tag/converter-latest)
release; it needs `ffmpeg` and `ffprobe` on `PATH`.

- **Containers:** AVI, MKV, MP4 (`.mp4`, `.m4v`, `.mov`)
- **Video:** MJPEG (up to 2560 px wide), H.264 (Constrained Baseline/Main/High,
  progressive 4:2:0 8-bit, up to 1280x720), MPEG-2 (Simple/Main, progressive,
  up to 1280x720)
- **Audio:** PCM, MP3, MP2, IMA ADPCM, AAC, Opus, FLAC, ALAC (the last three
  in MKV and MP4 only)

### Audio

- `.mp3`, `.mp2`, `.aac` (ADTS)
- `.wav` (16/24/32-bit PCM, IMA ADPCM)
- `.flac`
- `.m4a` (AAC, MP3, Opus, FLAC, ALAC or PCM)
- `.opus` (Ogg Opus, mono or stereo)

### Image

- JPEG (`.jpg`, `.jpeg`), baseline and progressive
- PNG (`.png`)
- BMP (`.bmp`)
- GIF (`.gif`), the first frame only
- TIFF (`.tif`, `.tiff`), uncompressed, PackBits, LZW or Deflate
- WebP (`.webp`), lossy, lossless and with alpha; the first frame of an animation

For the fastest display, use baseline JPEG no larger than the screen in the
orientation you view it (720x1280 portrait, 1280x720 landscape).

## Development

Clone with the required submodule:

```sh
git clone --recurse-submodules https://github.com/Hiroki-Kawakami/Tab5-Media-Player.git
cd Tab5-Media-Player
```

For an existing checkout, run `git submodule update --init --recursive` once.

The dev environment lives in a Nix flake; always run build tooling through
`nix develop`:

```sh
nix develop -c ./run.sh                                             # host simulator (SDL window)
nix develop -c ./run.sh esp32p4                                     # flash + monitor the device
nix develop -c ./run.sh esp32p4 build
nix develop -c ./run.sh esp32p4oc                                   # overclocked build (CPU 400MHz, PSRAM 220MHz)
nix develop -c ./run.sh simulator --verify simulator/verify/home.txt # headless UI check
```

`--verify` drives the simulator through the esp-devkit test harness and writes
JPEGs of the panel under `captures/`. The same scripts run against a flashed
board with `./run.sh esp32p4 --verify <port> <script>`.

Board support, the LVGL port and the UI framework come from the pinned
[`esp-devkit`](esp-devkit/) submodule.

See [`docs/architecture.md`](docs/architecture.md) for the component layout.

## License

MIT — see [`LICENSE`](LICENSE). This is a personal, non-commercial project
published as source on GitHub; it comes with no warranty and no support.
