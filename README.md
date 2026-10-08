# NanoGig Screen

A standalone, USB-powered gig screen for the **Neural DSP Nano Cortex**, running on an
ESP32-2432S028 "Cheap Yellow Display" (2.8" 320×240). It talks to the pedal over Bluetooth
LE with the same reverse-engineered protocol as [NanoGig](https://github.com/johnnyborjomi/NanoGig)
and shows, with no phone on the floor:

- the active preset's name in big type and its bank label (`3B`, Mvave Chocolate style), with
  IA / IB / IIA / IIB badges when the preset is assigned to a footswitch
- the five FX blocks with their model names, lit in the Cortex Cloud category colours
- the capture and IR names with on/off dots, gate state and tempo
- preset switching with the ◀ ▶ buttons beside the name
- preset rename: long-press the preset name (or a row in the presets list) for a keyboard; the pedal
  stores the new name at once
- FX block on/off: tap a tile (writes the same bypass frame as NanoGig's control mode)
- menu (≡): a tuner (note, cents bar, the pedal's reference pitch, a mute label that toggles),
  settings (presets per bank and label style, outputs 1/2 mute, expression indicators; page 2:
  display rotation 0° / 180° and brightness 1–10, both remembered; page 3: firmware version and
  updates over Wi-Fi), a tempo view (big BPM, − / + set the pedal's tempo,
  follows the pedal's tap tempo live), and Disconnect / Connect so Cortex Cloud can take the
  pedal without powering the screen off. A tuner started on the pedal opens the view too.

Everything about the protocol is provisional and firmware-specific (verified on NanOS 2.2.1,
September 2026). The pedal takes one Bluetooth connection at a time: close Cortex Cloud and the
NanoGig app first, or use Menu → Disconnect to hand the pedal over to them.

## Status

Working on the pedal since 2026-09-26: the screen boots in about a second, connects to the
Nano Cortex, shows the preset, capture, IR and FX blocks, follows footswitch presses, and
switches presets and toggles blocks from the touch screen. The protocol core
(`components/nano_protocol`) is plain C and passes its host tests against packets captured from
the real pedal. Touch is calibrated from the firmware's own log. Open items: auto-off / dimming after idle,
mounting.

## Layout

```
main/                    app: sync rules, NVS metadata cache, packet queue, touch → preset select
components/
  nano_protocol/         plain C, no ESP dependencies: framing, assembler, protobuf walker,
                         state / metadata / event decoders, FX model catalogue, category palette
    test/                host tests with hardware fixtures (cc + CMake)
  nano_ble/              NimBLE central: scan → connect → MTU 517 → a002 → subscribe c305 → write c304
  nano_ota/              update mode: Wi-Fi join (NVS credentials), HTTPS image check + install, rollback
  cyd_board/             ESP32-2432S028 bring-up: SPI panel, XPT2046 touch, backlight, LED, LVGL port
  nano_ui/               LVGL 9 gig screen
    nano_ui.c            the views (gig, menu, settings, presets, capture, tuner, tempo, connect, update)
    ui_common.h/.c       palette, screen size, fonts, label / box / button constructors
    ui_value_ctrl.h/.c   reusable parameter control: value, slider, fine / coarse steps, double tap reset
    ui_text_edit.h/.c    reusable text entry: one-line field, keyboard, length / custom checks, saving state
docs/HARDWARE.md         board facts, pinout, mounting notes
```

New pages build on `ui_common.h`. A new adjustable pedal parameter is a `ui_value_ctrl_cfg_t`:
its raw range, the mapping to the slider and the readout, the step sizes and a change callback
(see the capture volume in `nano_ui.c`). A text the pedal stores is a `ui_text_edit_cfg_t` (see the
preset rename). Both are freed with the page that holds them.

## Build

ESP-IDF v5.3.2 is installed at `~/esp/esp-idf` (shallow clone, `install.sh esp32`, tools in
`~/.espressif`). The development Mac's shell runs under Rosetta, so every ESP-IDF command is
wrapped in an arm64 shell with Homebrew first on the PATH:

```sh
cd ~/projects/NanoGigScreen
arch -arm64 /bin/zsh -c 'export PATH=/opt/homebrew/bin:$PATH; . ~/esp/esp-idf/export.sh >/dev/null; idf.py build'
arch -arm64 /bin/zsh -c 'export PATH=/opt/homebrew/bin:$PATH; . ~/esp/esp-idf/export.sh >/dev/null; idf.py -p /dev/cu.usbserial-110 -b 115200 flash monitor'
```

Flash at 115200 baud: the board's CH340 fails at the default 460800 / 921600. Exit the monitor
with Ctrl-]. On another machine: install ESP-IDF 5.2 or newer, `idf.py set-target esp32`, then
the same build and flash commands without the wrapper.

`sdkconfig.defaults` carries the required settings: NimBLE central only, preferred MTU 517
with a larger mbuf pool, custom partition table (two 1.94 MB OTA slots), Wi-Fi + HTTPS for
updates (Wi-Fi fast paths out of IRAM: Bluetooth fills it), LVGL 16-bit colour with the
Montserrat 12/14/20/28/40 fonts. `idf.py menuconfig → NanoGig Screen board` selects the panel
controller (ILI9341 vs ST7789 on some two-USB batches), colour inversion, 180° rotation and
touch mirroring.

Managed components (fetched on first build): `lvgl/lvgl ^9.2`, `espressif/esp_lvgl_port ^2.4`,
`espressif/esp_lcd_ili9341`, `atanisoft/esp_lcd_touch_xpt2046`.

### UI preview on the host

`tools/preview` builds the real LVGL and `nano_ui` natively and renders every view to PNG files
from the hardware fixtures, so layout work needs no board:

```sh
cd tools/preview
cmake -B build && cmake --build build -j8 && ./build/preview out
open out
```

### Protocol tests on the host

```sh
cd components/nano_protocol/test
cmake -B build && cmake --build build && ./build/test_protocol
```

## Firmware updates over Wi-Fi

Menu → Settings → page 3 → **Check for updates**. The screen drops the pedal link, shuts
Bluetooth down (no PSRAM: the heap cannot hold Bluetooth, Wi-Fi and TLS at once), starts Wi-Fi
and joins the remembered network, or lists the networks in range and asks for the password
(kept in NVS, namespace `nanogig_wifi`, unencrypted). It then reads only the header of
`CONFIG_NANOGIG_OTA_URL` (menuconfig → NanoGig Screen firmware update):

    https://github.com/johnnyborjomi/NanoGigScreen-firmware/releases/latest/download/nanogig_screen.bin

If the image's version differs from the running one, **Install** downloads it into the other
OTA slot and restarts. Closing the update page restarts too, which brings Bluetooth back. The
bootloader keeps the previous image until the new one reaches the end of `app_main` (display,
touch and Bluetooth up), so an image that crashes on boot rolls back by itself.

**Releasing.** This repo is private, so `.github/workflows/release.yml` builds every `v*` tag
with `NANOGIG_VERSION=<tag>` (the version the screen shows and compares) and publishes only
`nanogig_screen.bin` as a release of the public repo `johnnyborjomi/NanoGigScreen-firmware`
(secret `FIRMWARE_REPO_TOKEN`: a fine-grained token with Contents read/write on that repo).

```sh
git tag v0.4.0 && git push origin v0.4.0
```

Local builds report `git describe` as their version, so a development build always offers the
newest release.

**First switch to OTA.** The partition table changed from one factory app to two OTA slots, and
the bootloader gained rollback, so the first flash after this change must go over USB with
`idf.py flash` (bootloader, partition table, OTA data and app). The `nvs` partition stays where
it was, so settings and the preset cache survive. Delete a local `sdkconfig` first, or new
defaults such as rollback stay off: `rm sdkconfig && idf.py build`.

## How it syncs

1. Boot: the screen shows the cached preset names from NVS while Bluetooth starts (goal: under
   two seconds to a useful screen).
2. Scan for a device named like "Nano Cortex" or advertising service `a002`, connect, exchange
   MTU (517: the pedal sends 512-byte notifications), discover `a002`, subscribe to **`c305`
   only** (`c306` is an indicate mirror that throttles the pitch stream).
3. Request the current state (`0C C0 08 03 18 01 20 01 28 01 01 00 00 00`, ~0.3 s). Field 13 is
   the active preset (absent = preset 1: zero-valued varints are omitted), field 31 the bypass
   array, fields 48–52 the FX model IDs, 32/33 the capture and IR names.
4. Request the metadata dump (`06 C0 08 03 01 00 00 00`, ~17 KB, ~6 s) only when the NVS cache
   is missing or its record for the active preset names a different capture. The cache is
   rewritten when the dump differs.
5. Live: `0x1D` preset-changed events update the name at once and trigger a state re-read;
   bypass / knob / encoder / generic-change events re-read the state after a 400 ms debounce.
6. Preset select: `36 C0 18 00 20 <preset> 28 <-1> 30 <-1> 38 <-1> 40 <-1> 48 04 1D 00 00 00`
   on `c304`; the pedal acks with `0x1F` then `0x1E`, and the following state dump confirms.
7. Expression pedal: `0x40` position events (0–254, ~20/s) drive a thin orange bar in the right
   gutter; `0xAA` values events fill a track at the bottom of every FX tile the pedal is
   assigned to. Assignments are per preset and read with `08 C0 08 03 18 <preset> 3C 00 00 00`
   once the shown preset has settled (reply `0x3D`; it names no preset, so the app remembers
   what it asked for). Settings → "Show expression pedal" (NVS `expshow`, default on) draws the
   indicators always or never; off also skips the ~40 UI updates/s while the pedal moves.

Details and every other frame: NanoGig `docs/PROTOCOL.md`.

## First hardware pass

Things to check on the bench, in order:

1. `idf.py build` on a fresh ESP-IDF: API drift in `esp_lvgl_port` / `esp_lcd_touch` is the
   likeliest compile error.
2. Panel: the defaults are ST7789, no inversion (the user's CYD2USB board, checked with the
   community Arduino examples 2026-09-26). If the image is upside down, toggle
   Settings → page 2 → Rotate display (stored in NVS as `rot`; `CYD_ROTATE_180` is only the
   first-boot default); on a single-USB board switch to ILI9341. Brightness lives next to it
   (NVS `bright`, 1–10, PWM on the backlight pin).
3. Touch: the axis swap and calibration live in `touch_calibrate()` in `cyd_board.c`, measured
   from the `touch press x= y=` lines the firmware prints on the serial port. On another board
   tap the four corners, read those lines, and adjust the four range constants.
4. Log: `MTU 517` must appear before the first state dump; an MTU of 23 means the exchange
   failed and the dump packets will be truncated.
5. State dump decodes: preset, capture, IR, tiles match the pedal. Then press a footswitch.

## License

MIT, like NanoGig. Protocol knowledge builds on choldy/nano-cortex-web-editor (MIT),
rixrix/deskop-nano-cortex (Apache-2.0) and DrD85/nano-cortex-controller (MIT: the preset rename). Not affiliated with Neural DSP.
