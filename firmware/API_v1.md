# bk2318 adapter protocol, v1

Firmware `bk2318_v1/`: line-by-line commands without jobs. Superseded by v2 (`API_v2.md`). Tested on SN 1941478 on 2026-10-19 (`../evidence/printer_SN1941478/v1_receipt_cyrillic.jpg`).

## Transport and format

- The C6's native USB port (USB CDC; the baud rate setting does not matter). One program at a time. Opening the port does not reset the board.
- One command is one UTF-8 line ending in `\n` (`\r` before it is ignored), up to 1024 bytes. Keyword case-insensitive; argument after the first space.
- **Every command gets exactly one reply:** `OK [data]` or `ERR <code> <explanation>`. Send the next command only after the reply.
- `EVT …` lines can come at any time.

## Commands

| Command | Action | Reply |
|---|---|---|
| `PRINT <text>` | prints a line and a line feed. Over 24 characters (12 wide) wraps in the printer. `PRINT` alone is an empty line | `OK` |
| `FEED <n>` | n empty lines, 1–20 | `OK` |
| `WIDE ON\|OFF` | double width, 12 per line (`ESC K` / `ESC L`) | `OK` |
| `INVERSE ON\|OFF` | white on black (`ESC H` / `ESC I`); prints worse than normal | `OK` |
| `ALIGN LEFT\|RIGHT` | line alignment (`ESC & <` / `ESC & >`) | `OK` |
| `RESET` | printer reset (NSR `1F`); runs after the lines already queued | `OK` |
| `RAW <hex>` | raw bytes, e.g. `RAW 1B 4B`, for experiments | `OK` |
| `CLEAR` | drop everything not yet sent | `OK` |
| `DRAIN` | replies when the queue is empty, that is, everything has been **sent**; the printer may still be printing | `OK` |
| `STATUS` | state | `OK queued=0 free=4095 paused=0 sent=… rx=… xoff=… xon=… resets=… wide=0 inverse=0 align=LEFT` |
| `HELLO` | link check | `OK bk2318 1 24` (name, protocol version, line width) |
| `HELP` | list of commands | `OK commands: …` |

| Error | When |
|---|---|
| `ERR UNKNOWN <cmd>` | no such command |
| `ERR ARG …` | bad argument |
| `ERR FULL <n> bytes free` | the adapter's 4 KB queue is full; the command was **not taken**, retry later |
| `ERR TOOLONG` | line over 1024 bytes |

| Event | Meaning |
|---|---|
| `EVT BOOT bk2318 1` | the adapter started |
| `EVT XOFF` / `EVT XON` | the printer asks to stop / continue; the adapter pauses its queue itself |
| `EVT PRINTER_RESET` | XON came less than 50 ms after XOFF: printer power-on or `RESET`. The printer is back to normal width, no inverse, left; the adapter resets its own `wide`, `inverse`, `align` |
| `EVT RX <hex>` | any other byte from the printer (never seen) |

Switching the printer off gives no event: the line just goes quiet.

## Text

- **ASCII 0x20–0x7E** prints as is, Latin in both cases.
- **Cyrillic** prints uppercase only: lowercase becomes uppercase, Ё → Е, Ъ → Ь. Inside, it is the printer's Russian set (`4C`) in G1 with SO/SI, so mixed text like `Sony КАМЕРА fx3` works in one line.
- **Typography** is replaced: «» “” „ → `"`, ‘’ → `'`, – — − → `-`, … → `...`, № → `N`, no-break space and tab → space.
- **Everything else** becomes `?`. Command bytes never get into the text.

## Example

```
> HELLO
< OK bk2318 1 24
> WIDE ON
< OK
> PRINT ВЫЕЗД 42
< OK
> WIDE OFF
< OK
> PRINT 1001 Кофр Sony FX3
< OK
> DRAIN
< OK
```
