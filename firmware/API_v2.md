# bk2318 adapter protocol, v2

Firmware `bk2318_v2/`, on the board since 2026-10-26: self-test and printing jobs passed (`../tools/v2_probe.py`, `../tools/tape_probe.py`).

Tags: [measured] on SN 1941478; [choice] design decision; [calc]; [hypothesis] not verified.

```
computer ──USB──▶ ESP32-C6 (bk2318_v2) ──RS-232, 1200 baud──▶ B&K 2318
```

- Paper: a 57–58 mm till roll. A line is **24 characters** (12 in wide mode), about 46 mm.
- Printing: **0.8 lines/s**, so a 30-line receipt takes about 40 s. The line carries about 120 bytes/s.
- The printer runs from its own batteries; the adapter from USB.
- Client: `../host/bk2318.py` (Python ≥ 3.9 + `pyserial`).

## Transport and format

- The C6's native USB port; `/dev/cu.usbmodem*` on macOS. **One program** holds it at a time. Opening the port does not reset the board (with pyserial) [measured].
- One command is one UTF-8 line ending in `\n` (a `\r` before it is ignored), up to 1024 bytes. The keyword is case-insensitive; the argument starts after the first space.
- **Every command gets exactly one reply:** `OK [data]` or `ERR <code> <explanation>`. Send the next command only after the reply.
- `EVT …` lines can come at any time, also between a command and its reply.

## Jobs: JOB BEGIN … JOB END [choice]

Printing happens only in jobs: a receipt prints completely or not at all.

```
> JOB BEGIN
< OK
> PRINT ВЫЕЗД 42
< OK
> RULE DASH
< OK
> PRINT ☐ 0125 FX3 КОРПУС
< OK
> JOB END
< OK lines=3 bytes=74 eta=4 check=ok
```

- Between `JOB BEGIN` and `JOB END` the adapter **prints nothing**; it only collects the job.
- **The job is dropped** if USB disconnects or **more than 5 s pass between commands**. Then `EVT JOB_ABORTED TIMEOUT` (or `USB`) arrives, and the next `PRINT` gets `ERR NOJOB`. Send the whole job at once, without waiting for the user in between.
- `JOB CANCEL` drops the open job.
- Job size: **up to 6 KB of printer bytes**, after conversion. Over that: `ERR TOOBIG`, job dropped. A 24-cell line is 24 bytes + 2 for the line end + overhead: 3 bytes if it has Cyrillic, 1 per switch between Cyrillic and Latin, 2 instead of 1 for an own glyph, 9 for `RULE`, 2 for `WIDE`. The worst case is about 35 bytes per line, so 6 KB is at least 170 lines. The exact size is `bytes=` in the `JOB END` reply. Glyph loading and the reset don't count. Jobs over 6 KB were never tested against XOFF.
- **On `JOB END` the adapter:**
  1. **Checks that the printer is on.** It sends NSR `1F`; a printer that is on answers XOFF then XON [measured: after 10–11 ms]. No answer: `ERR PRINTER_OFF`, job dropped, nothing printed. Otherwise a printer that is off gives no sign: the bytes just vanish [measured].
  2. The reset also restores the defaults: **every job starts with normal width, no inverse, left aligned.**
  3. Loads the own glyphs the job uses (✓ ✗ Ъ ☐ … → ─).
  4. Queues the job and replies `OK lines=<n> bytes=<n> eta=<s> check=ok`. `eta` estimates when the printer will finish [calc: 1.25 s per line, 120 bytes/s].
- **If the printer is still printing the previous job** (by the adapter's estimate), there is no check: the printer runs `1F` in its own queue, after printing [measured], so the answer would take minutes. If it is printing, it is on. The adapter sends normal width, no inverse, left instead of the reset, and replies at once with `check=busy`.
- `JOB END` replies within a fraction of a second. Client timeout: 10 s.
- `JOB BEGIN` + `JOB END` with no lines is a plain "is the printer on?" check.
- Switching the printer off **in the middle** of printing is not detected: the rest of the job is lost without an error.
- **Pacing:** the adapter keeps the printer at most 2.5 s of printing ahead (`lead=` in `HELLO`), so `CLEAR` stops a receipt within one or two lines.

## Commands inside a job

| Command | Action |
|---|---|
| `PRINT <text>` | a line of text and a line feed. Longer than 24 characters (12 wide) wraps in the printer, by character. `PRINT` with no text is an empty line |
| `FEED <n>` | n empty lines, 1–20 |
| `RULE SOLID\|DOTS\|DASH\|DASHDOT` | a full-width line on its own line [measured: all 4 print] |
| `WIDE ON\|OFF` | double width, 12 characters per line |
| `INVERSE ON\|OFF` | white on black. Hard to read for small text [measured] |
| `ALIGN LEFT\|RIGHT` | alignment of the following lines |

Each replies `OK` (taken into the job) or `ERR ARG …`. Outside a job they give `ERR NOJOB`.

## Commands at any time

| Command | Reply |
|---|---|
| `HELLO` | `OK bk2318 2 24 glyphs=✓✗Ъ☐…→─ rule=SOLID,DOTS,DASH,DASHDOT job=6144 lead=2500`: name, protocol version, line width, capabilities. The `key=value` list may grow; look for the key, not the position |
| `ECHO <text>` | `OK <text after conversion>`: exactly what will print, as UTF-8. `ECHO Съёмка «FX3»…` → `OK СЪЕМКА "FX3"…`. One character of the reply is one cell of the line |
| `STATUS` | `OK job=open\|none jobbytes=… queued=… free=… paused=… busy=… sent=… rx=… xoff=… xon=… resets=…` |
| `DRAIN` | replies when the adapter's queue is empty, that is, everything has been **sent**. The printer may still be printing: see `eta` |
| `CLEAR` | `OK dropped=<bytes>`: drop what has not been sent yet; a cut-off line is closed (SI CR LF). The printer finishes what it has already received |
| `RAW <hex>` | raw bytes for the printer. Outside a job they go at once, inside a job into the job; line feeds in them count towards `lines` and `eta`. Split long data: the command line is limited to 1024 characters |
| `HELP` | list of commands |

## Errors

| Code | When |
|---|---|
| `ERR UNKNOWN <cmd>` | no such command |
| `ERR ARG …` | bad argument; the explanation lists valid values |
| `ERR NOJOB` | a print command outside a job, or the job was dropped by the timeout |
| `ERR INJOB` | `JOB BEGIN` while a job is open |
| `ERR TOOBIG` | job over 6 KB; job dropped |
| `ERR PRINTER_OFF` | the printer did not answer the reset within 0.3 s (or XOFF without XON for 2 s); job dropped |
| `ERR FULL <n> bytes free` | `RAW` outside a job and the adapter's 16 KB queue is full |
| `ERR TOOLONG` | command line over 1024 bytes |

## Events

| Event | Meaning |
|---|---|
| `EVT BOOT bk2318 2` | the adapter started or restarted |
| `EVT PRINTER_RESET` | the printer has just been switched on (by itself, not by the `JOB END` check) |
| `EVT JOB_ABORTED TIMEOUT\|USB` | the open job was dropped |
| `EVT XOFF` / `EVT XON` | the printer asks to wait / continue; the adapter handles it, this is for debugging |
| `EVT RX <hex>` | any other byte from the printer (never seen) |

## Text

- **Latin, digits, ASCII punctuation** print as they are, Latin in both cases.
- **Cyrillic is uppercase only** [choice]: lowercase becomes uppercase, **Ё → Е**, **Ъ prints as an own glyph**. "Съёмка" → `СЪЕМКА`. Lowercase Cyrillic is possible as own glyphs through `RAW` (`../docs/graphics.md`).
- **Own glyphs:** the host sends ordinary Unicode characters and the adapter prints them as 7 × 7 glyphs (`../glyphs/drc_approved.txt`): ✓ U+2713, ✗ U+2717, Ъ U+042A, ☐ U+2610 (two in a row merge: put a space between), … U+2026, → U+2192, ─ U+2500 (a row of them is one solid line). In wide mode they are doubled too [measured].
- **Typography** is replaced: «» “” „ → `"`, ‘’ → `'`, – — − → `-`, № → `N`, no-break space and tab → space.
- **Everything else** becomes `?`: emoji, other alphabets, other arrows, ×, ·.
- **One Unicode code point is exactly one cell** (two of 24 in wide mode), after conversion and for own glyphs too. Except invisible characters, which take no cell and don't print: combining marks U+0300–036F, variation selectors U+FE00–FE0F, U+200B–200F, U+2060, U+FEFF, soft hyphen U+00AD.
- **Send text in NFC**, or "й" made of two code points prints as И. Count cells by code points (`[...s]` in JS, not `s.length`). `ECHO` shows the result.

## Layout is up to the host [choice]

The adapter knows nothing about words or columns; it prints lines.

- **Wrapping and cutting.** The printer breaks a line longer than 24 cells by character, in the middle of a word or number. Cut long names to 24 cells with `…`, or wrap by words yourself.
- **A rule with a title** from `─`: `── PUT AWAY ────────────` (exactly 24 cells) [measured: looks like the graphic rule].
- **Centring:** spaces on the left; `ALIGN` only does left and right.
- **End of receipt:** the last line stays under the head, 2 lines below the tear edge. `FEED 3` at the end of the job, or the Paper Feed button.

Mock receipt from the 2026-10-20 probe (`../evidence/printer_SN1941478/pdi_probe_2026-10-20.jpg`), 24 cells:

```
KIA SPOT                    ← WIDE ON
20.10-22.10  3 ДНЯ
── 2001 BAG ────────────
☐ 0125 FX3 КОРПУС
☐ 0304 V-MOUNT 150WH x4
☐ 0307 ШТАТИВ MANFROTT…
── PUT AWAY ────────────
ШКАФ 1 ПОЛКА 3
 ☐ 0125 FX3 КОРПУС
 СЛЕВА, ПОД КОФРОМ
── MISSING 1/14 ────────
✗ 0307 ШТАТИВ MANFROTTO
✓ 0125 FX3 КОРПУС
```

## Web Serial

`../tools/webserial_test.html` talks to the adapter from Chrome.

- Web Serial works only in a secure context: `https://` or `http://localhost`.
- What DTR/RTS Chrome sets on `port.open()` and whether that resets the C6 is not checked [hypothesis]. After `open()`, send `HELLO`; if `EVT BOOT` arrives, wait for it and send `HELLO` again.
- Open the port for the print and close it after, so other programs can use it.

## Tried on paper but not in the protocol

All printed on the 2026-10-20 and 2026-10-26 probes [measured], with raw bytes. A host can do them through `RAW`; see `../docs/graphics.md` for the bytes.

- **Wide and normal text in one line:** `ESC K 0125 ESC L FX3 КОРПУС` (a wide character takes 2 cells).
- **Bold:** the line printed twice, the second time 1 dot to the right. Not for glyphs with strokes 1 dot apart, inverse or wide text.
- **Code 128** barcodes from DOT columns: a 1-dot module reads; about 1 KB and 9 s of transfer per barcode with a 2-dot module [calc].
- **Hatched bars** as dividers; **bars as indicators** (`12/14 ▬▬▬▬▬▬▬▬▬▬░░`), overlaid in one line.
- Not recommended: images and QR codes. There is a white gap of about 2 mm between print lines [measured], and a raster line takes about 4 s to send at 1200 baud [calc].
