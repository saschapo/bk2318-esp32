# Firmware

Three Arduino sketches for the ESP32-C6. No third-party libraries.

- `bk2318_v2/`: **current**. Print jobs (`JOB BEGIN … END`), a power check before each job, built-in glyphs, `RULE`, pacing so that `CLEAR` stops within a line or two. Protocol: `API_v2.md`. Client: `../host/bk2318.py`. Tests: `../tools/v2_probe.py self|strip`.
- `bk2318_v1/`: line-by-line commands without jobs, UTF-8 → the printer's Cyrillic. Protocol: `API_v1.md`.
- `bk2318_v0/`: debug console used for the first connection (`/loop`, `/scope`, `/hex`, `/feed`, `/tail`), described below.

## Build and flash

Board: **MuseLab nanoESP32-C6**, core `esp32:esp32` 3.3.x. Connect the **native USB** port (the second USB-C, not the CH343 one); it shows up as `/dev/cu.usbmodem*` on macOS.

```bash
arduino-cli compile -b "esp32:esp32:esp32c6:FlashSize=16M,CDCOnBoot=cdc" firmware/bk2318_v2
```

```bash
arduino-cli upload -b "esp32:esp32:esp32c6:FlashSize=16M,CDCOnBoot=cdc" -p /dev/cu.usbmodemXXXX firmware/bk2318_v2
```

```bash
arduino-cli monitor -p /dev/cu.usbmodemXXXX -c baudrate=115200
```

Only one program can hold the port: close the monitor and scripts before `upload`. If the upload does not start: hold BOOT, press RESET, release BOOT and try again. Other ESP32 boards should work with the pins changed (`PRN_TX_PIN`, `PRN_RX_PIN`, `NEOPIXEL_PIN` at the top of each sketch).

## Wiring

| ESP32-C6 | SP3232 | B&K-SI socket |
|---|---|---|
| 3V3 | VCC (16) | — |
| GND | GND (15) | 4 SG |
| GPIO2 (TX) | T1IN (11) → T1OUT (14) | 5 RD |
| GPIO3 (RX) | R1OUT (12) ← R1IN (13) | 3 TD |

Numbers in brackets are chip pins; check your module's labels with a meter. Details: `../docs/wiring.md`.

## NeoPixel (GPIO8)

- green: idle;
- blue: sending;
- red: the printer sent XOFF, sending paused;
- yellow (v2): a job is open or the power check is running.

## v0 console

Type a command and Enter; `/help` lists them all. Text lines go to the printer as they are, with bytes outside 0x20–0x7E replaced by `?`. Every byte the printer sends back is logged with a timestamp.

- `/test`, `/ascii`, `/cr`, `/lf`, `/crlf`, `/eol none|cr|lf|crlf`: test lines and line endings.
- `/hex 1B 4B`: raw bytes. Keep each command within the board's 256-byte USB buffer.
- `/long N`: N lines of 24 characters, to test XON/XOFF.
- `/feed N`: feed N empty lines now. `/tail N`: feed N lines 2 s after each job (0 = off); 3 gives a margin to tear.
- `/resume`, `/clear`, `/status`: clear an XOFF pause, drop the queue, show counters.
- `/loop`: with RS-232 TXD and RXD of the module bridged, checks the module without a printer; the answer must be `OK`.
- `/scope FF`: repeat one byte every 25 ms for an oscilloscope.

The last printed line stays under the head, about 2 lines below the tear edge; the printer moves paper only while printing. Or feed by hand: Power switch in the top On position (Paper Feed), press the button.
