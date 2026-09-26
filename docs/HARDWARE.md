# Hardware notes

## Board

AliExpress item 1005006315444628, TENSTAR "ESP32 2.8-inch LCD LVGL" = **ESP32-2432S028**, the
Cheap Yellow Display (CYD). Arrived 2026-09-26.

- ESP32-WROOM-32 (classic ESP32: BLE 4.2, no USB host), 4 MB flash, no PSRAM
- 2.8" 320×240 TFT over SPI. Classic boards: ILI9341. **This board has both micro-USB and
  USB-C (CYD2USB) and carries an ST7789**, confirmed 2026-09-26: the community
  `DisplayConfig/CYD2USB/User_Setup.h` (ST7789, BGR, `TFT_INVERSION_OFF`) draws the Hello
  World example correctly in landscape (`setRotation(1)`).
- XPT2046 resistive touch on a second SPI bus ("R" variant)
- 5 V over USB (USB-C on this batch), ~150 mA with the backlight on
- PCB 86.0 × 50.0 mm, active area 57.6 × 43.2 mm, 10–13 mm thick with connectors
- Community reference: github.com/witnessmenow/ESP32-Cheap-Yellow-Display

## Pinout used

| Function        | GPIO | Notes                              |
| --------------- | ---- | ---------------------------------- |
| LCD MOSI        | 13   | HSPI (SPI2_HOST)                   |
| LCD MISO        | 12   |                                    |
| LCD SCLK        | 14   |                                    |
| LCD CS          | 15   |                                    |
| LCD DC          | 2    |                                    |
| LCD RST         | –    | tied to EN                         |
| LCD backlight   | 21   | high = on                          |
| Touch MOSI      | 32   | own bus (SPI3_HOST)                |
| Touch MISO      | 39   | input only                         |
| Touch SCLK      | 25   |                                    |
| Touch CS        | 33   |                                    |
| Touch IRQ       | 36   | input only, low while pressed      |
| RGB LED R/G/B   | 4 / 16 / 17 | active low                  |
| LDR             | 34   | unused (could drive the backlight) |
| Speaker         | 26   | unused                             |
| SD card         | 5 / 18 / 19 / 23 | unused (VSPI)          |

The firmware lights the LED red while scanning, blue while connecting, off when the link is up.

## Bench findings (2026-09-26, Arduino smoke test)

- Power and serial only over **USB-A to USB-C** (or a USB-A hub on the Mac). A USB-C to USB-C
  cable or USB-C charger gives nothing: the socket lacks the CC pull-downs.
- Serial port on the Mac: `/dev/cu.usbserial-110` (CH340, driver built into macOS 15).
  **Upload at 115200 baud**: 921600 fails with "Unable to verify flash chip connection".
- Chip: ESP32-D0WD-V3 revision 3.1, 40 MHz crystal, 4 MB flash.
- Touch (Arduino `XPT2046_Touchscreen`, rotation 1 = raw pass-through, landscape):
  top-left 408/610, top-right 3636/491, bottom-left 357/3545, bottom-right 3710/3607.
  In the firmware (esp_lcd_touch_xpt2046) the controller's X channel runs **down** the landscape
  screen and Y runs **across** it, the opposite of what the Arduino numbers suggested. Measured
  with the firmware's own `touch press` log after the driver's scaling to 320 / 240: down
  35..287, across 18..217. `touch_calibrate()` in `cyd_board.c` swaps the axes and stretches
  both ranges onto the full 320×240; the driver's own swap / mirror flags stay off.
- Colour order: `LCD_RGB_ELEMENT_ORDER_RGB` in esp_lcd. `BGR` swapped red and blue on this
  panel (the red "1A" bank label showed blue), although TFT_eSPI's CYD2USB setup says `TFT_BGR`.
- Bug found on the pedal 2026-09-26: a state dump re-parsed from a shifted offset produced one
  bogus preset record, was taken for a metadata dump and wiped the NVS name cache (every preset
  then showed its capture name). Metadata now needs >= 2 records, >= 2 KB and a pending request.
- First firmware flash 2026-09-26: screen, LED, Bluetooth link, state dump, footswitch events,
  preset select and FX toggles all worked. Touch taps must use the **press** point: the last
  sample before lift-off on this resistive panel drifts to a small X, which made every tap read
  as "previous preset" until fixed.
- Orientation: in landscape with the text upright the **USB sockets are on the right**. TFT_eSPI
  `setRotation(1)` sends MADCTL `MX | MV` to the ST7789, which is the firmware's default
  (`swap_xy` + `mirror_x`, i.e. `CYD_ROTATE_180=n`).
- Community reference clone: `~/projects/CYD-reference/ESP32-Cheap-Yellow-Display`.

## Protocol finding: tap tempo (2026-09-26)

Not in NanoGig's PROTOCOL.md yet. Hold the left footswitch to enter tap tempo, tap, hold again to
leave. Every tap sends a type `0x91` message with the current tempo:

```
0D C0 08 01 18 01 2D <f32 BPM> 91 00 00 00     field 3 = 1 (mode on), field 5 = BPM
0B C0 08 01 2D <f32 BPM> 91 00 00 00           leaving the mode: field 3 absent, final BPM
```

Seen values 60–186 BPM. The exit is sometimes followed by `08 C0 08 01 18 01 73 00 00 00` and
sometimes by nothing, so the screen re-reads the state itself after the exit message; state
field 56 then carries the same tempo.

**Setting the tempo** (found by trial, verified 2026-09-26): write the per-tap shape to `c304`:

```
0D C0 08 01 18 01 2D <f32 BPM> 91 00 00 00
```

No ack; the pedal enters its tap tempo mode and the next state dump's field 56 has the new
value. The screen sends it when its Tempo view opens (current tempo) and on − / +. Closing the
view sends the pedal's exit shape `0B C0 08 01 2D <f32 BPM> 91 00 00 00` to leave the mode.

## Pedal advertising (user's observation 2026-09-26)

After a disconnect the Nano Cortex advertises only for a limited window (tens of seconds). While
it is in tap tempo mode past that window it does not advertise at all, so a Connect from the
screen finds nothing until the mode is left on the pedal. The screen's status line says so after
30 s of scanning. A footswitch-started tuner and tap tempo mode are both announced to a connected
client (tuner report 0x7F, tap message 0x91); the pedal answers our own tuner-on with the same
report within ~65 ms and sends nothing at all after our tuner-off (capture 2026-09-27), so every
on-report with the view closed is a footswitch start; only pitch readings can trail an off; a reconnect during tap tempo mode carries state
field 60 = 1, which the screen uses to open the tempo view.

## Memory budget

- Assembler buffer 20 KB (metadata dump ~17 KB), NVS metadata cache ~7 KB, LVGL draw buffers
  2 × 320 × 40 × 2 B = 51 KB in DMA-capable RAM, NimBLE ~40 KB. Fits the ESP32's 320 KB with
  room; no PSRAM needed.

## Mounting on the pedal

The Nano Cortex body is 142 × 102 × 61 mm with two large rotary footswitches; the module is
86 mm long, so it will not fit between the switches lengthwise and probably not in portrait
either. Expected placement: behind the switches or on a rear-edge bracket angled toward the
player, 3D-printed, with the glass recessed below switch-cap height.

Two measurements decide it: clear width between the switch rings, and clear depth in front of
the knobs. Power comes from the same USB brick as the pedal (a Y cable or a small hub).
