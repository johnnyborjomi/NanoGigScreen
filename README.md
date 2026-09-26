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
- FX block on/off: tap a tile (writes the same bypass frame as NanoGig's control mode)
- menu (≡): a tuner (note, cents bar, the pedal's reference pitch, a mute label that toggles),
  settings (presets per bank for the label), and Disconnect / Connect so Cortex Cloud can take
  the pedal without powering the screen off. A tuner started on the pedal opens the view too.

Everything about the protocol is provisional and firmware-specific (verified on NanOS 2.2.1,
September 2026). The pedal accepts several Bluetooth clients at once, so the screen can run
next to the NanoGig app; Cortex Cloud must be disconnected first.

## Status

Working on the pedal since 2026-09-26: the screen boots in about a second, connects to the
Nano Cortex, shows the preset, capture, IR and FX blocks, follows footswitch presses, and
switches presets and toggles blocks from the touch screen. The protocol core
(`components/nano_protocol`) is plain C and passes its host tests against packets captured from
the real pedal. Touch is calibrated from the firmware's own log. Open items: gate toggle,
expression view, brightness / auto-off, mounting.

## Layout

```
main/                    app: sync rules, NVS metadata cache, packet queue, touch → preset select
components/
  nano_protocol/         plain C, no ESP dependencies: framing, assembler, protobuf walker,
                         state / metadata / event decoders, FX model catalogue, category palette
    test/                host tests with hardware fixtures (cc + CMake)
  nano_ble/              NimBLE central: scan → connect → MTU 517 → a002 → subscribe c305 → write c304
  cyd_board/             ESP32-2432S028 bring-up: SPI panel, XPT2046 touch, backlight, LED, LVGL port
  nano_ui/               LVGL 9 gig screen
docs/HARDWARE.md         board facts, pinout, mounting notes
```

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
with a larger mbuf pool, custom partition table (3.9 MB app), LVGL 16-bit colour with the
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

Details and every other frame: NanoGig `docs/PROTOCOL.md`.

## First hardware pass

Things to check on the bench, in order:

1. `idf.py build` on a fresh ESP-IDF: API drift in `esp_lvgl_port` / `esp_lcd_touch` is the
   likeliest compile error.
2. Panel: the defaults are ST7789, no inversion (the user's CYD2USB board, checked with the
   community Arduino examples 2026-09-26). If the image is upside down, toggle
   `CYD_ROTATE_180`; on a single-USB board switch to ILI9341.
3. Touch: the axis swap and calibration live in `touch_calibrate()` in `cyd_board.c`, measured
   from the `touch press x= y=` lines the firmware prints on the serial port. On another board
   tap the four corners, read those lines, and adjust the four range constants.
4. Log: `MTU 517` must appear before the first state dump; an MTU of 23 means the exchange
   failed and the dump packets will be truncated.
5. State dump decodes: preset, capture, IR, tiles match the pedal. Then press a footswitch.

## License

MIT, like NanoGig. Protocol knowledge builds on choldy/nano-cortex-web-editor (MIT) and
rixrix/deskop-nano-cortex (Apache-2.0). Not affiliated with Neural DSP.
