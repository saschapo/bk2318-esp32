# Protocol: switches, serial line, flow control, text, character sets

Tags: [manual] · [forum] · [measured] on SN 1941478 · [calc] · [hypothesis] · [conclusion]. Page numbers are the ones printed in the manual. Own characters (DRC) and graphics (PDI) are in `graphics.md`.

## DIP switches inside the ZI 0054 [manual, pp. 18–21]

Switch the printer off and slide the module out to reach them (pp. 16–17). Layout of N1 and N2: fig. 4.3.

| Switch | Function | OFF | ON | Factory |
|---|---|---|---|---|
| N1-1…N1-4 | baud rate | see below | | **1200** = OFF OFF ON OFF |
| N1-5 | XON/XOFF | disabled | enabled | enabled (p. 10) |
| N1-6 | EOT command | **enabled** | disabled | not stated |
| N1-7, N1-8 | unused | | | |
| N2-1 | data | 7 bits | 8 bits | **7 bits** |
| N2-2 | power-on reset | NSR, full reset | NSR without clearing user characters (DRC) | **ON** |

Baud rates, N1-1…4: 19200 = ON ON ON ON or OFF ON ON ON; 9600 = ON ON ON OFF; **4800 = OFF ON ON OFF**; 2400 = ON ON OFF OFF or OFF OFF OFF ON; 1800 = ON OFF ON OFF; **1200 = OFF OFF ON OFF**; 600 = ON OFF OFF ON; 300 = OFF ON OFF OFF; 200 = OFF ON OFF ON; 150 = ON OFF OFF OFF; 134.5 = ON ON OFF ON; 110 = OFF OFF OFF OFF; 75 = OFF OFF ON ON; 50 = ON OFF ON ON.

At 9600 and 19200 there must be at least 2 ms between the start bits of consecutive characters (p. 20). The specification (p. 49) says the same as a 1 ms gap at 9600 and 1.5 ms at 19200.

**Our module** [measured, photo `evidence/printer_SN1941478/zi0054_dip_N1_N2_2026-10-20.jpg`]: in N1 only 3 and 5 are up, in N2 only 2. The ON label is not visible; "up = ON" follows from what already works (1200 baud, XON/XOFF, 7-bit, DRC kept over power-off). So everything is factory, and **N1-6 is OFF: EOT is enabled**.

## Serial line

- **8N1 works in both modes as long as bytes are below 0x80** [manual, p. 19]. In 7-bit mode the printer takes the 8th bit as parity or the first stop bit and ignores it.
- **Keep bytes ≥ 0x80 out of the stream.** In 7-bit mode the top bit is dropped [measured: `B4` printed as `4`, `C1` as `A`], so a stray UTF-8 byte 0x9F becomes 0x1F (NSR, reset). In 8-bit mode 0x80–0x9F are C1 commands and 0xA0–0xFF the right-hand table GR, PDI graphics by default.
- Printing is 0.8 lines/s, about 19 characters/s; the buffer is about 7 KB [manual, p. 49]. At 1200 baud (about 120 bytes/s) the line is about 6× faster than the printer [calc], so for text the printer is the limit at any baud rate. The line only limits graphics: a full raster line by DOT is 504 bytes, 4.2 s at 1200 baud against 1.25 s of printing [calc].
- **Our choice: stay at 1200, 7 bits.** 8-bit mode only adds one-byte C1 commands and a second table in GR; it saves 2 bytes per switch between Latin and Cyrillic and makes every byte ≥ 0x80 a command or graphics. The practical ceiling is about 4800 baud (about 480 bytes/s); 9600 and 19200 with the 2 ms gap give no more [calc]. Changing either needs the module out.
- The B&K 2239 sound level meter has a fixed 9600 8N1 XON/XOFF port and lists "AO 0404 → LEMO (for 2318)" as an accessory, so the 2318 was used at 9600 too [manual of the 2239].

## Flow control [manual, pp. 20, 30, 36]

- **READY/BUSY on the HS line is always on.** XON/XOFF works in addition when N1-5 = ON.
- The printer only **sends** DC1 (0x11, XON) and DC3 (0x13, XOFF) (footnote to table 5.2).
- **XON and XOFF arrive as `91` and `93` on an 8N1 receiver** [measured, `evidence/printer_SN1941478/nsr_xoff_xon_2026-10-19.log`]. In 7-bit mode the printer sends 7 data bits, so an 8N1 UART reads the stop bit as bit 7. Compare `b & 0x7F` with 0x11/0x13, or nothing will ever pause. FrodeM's point [forum]: a receiver set to 7 data bits simply ignores that bit; old RS-232 hardware never checked stop bits. There is no need to switch to `SERIAL_7N1`: the extra 0 bit we send is ignored in 7-bit mode [manual, p. 19].
- **After NSR (`1F`) and at power-on the printer sends XOFF, then XON 10–11 ms later** [measured; `power_cycle_listen_2026-10-19.log`]. Nothing at power-off. This pair is a handy paper-free check that the printer is on, and a "printer just reset" signal.
- **NSR is queued**: it runs after the lines already received have printed. The XOFF/XON pair came several seconds later while the printer was busy [measured].
- Commands do not fill the buffer: 8000 bytes of the no-op `1B 4C` were accepted in 84 s with no XOFF [measured, `xoff_fill_2026-10-19.log`], so commands execute on arrival. You cannot fill the buffer without printing.
- **XOFF on a really full buffer (about 7 KB) was never tested**: it would take about 330 lines of paper. Our firmware keeps jobs under 6 KB and paces sending to the printing speed instead.
- Without a power-on pair, a printer that is off looks exactly like one that is on: bytes just vanish [measured]. Firmware v2 sends NSR before each job and waits for the pair (`firmware/API_v2.md`).

## EOT, 0x04 [manual, pp. 21, 36]

- EOT is **not needed to print**.
- On EOT the printer holds the sender through the handshake until it has printed the whole buffer and stopped the motor. B&K used it so that a sound level meter does not measure while the printer is noisy.
- Works only with N1-6 = OFF; with N1-6 = ON the printer ignores EOT.
- Use: `04` at the end of a job, then XON (or BUSY released) should mean "paper has stopped" [conclusion; not measured].

## Formatting [manual, pp. 37–38]

| Code | Name | Action |
|---|---|---|
| 0A | LF | prints the line buffer, feeds one line, cursor to the start. **CR right after LF is ignored** |
| 0D | CR | same as LF. **LF right after CR is ignored** |
| 0C | FF | same as LF, but always acts |
| 1F | NSR | reset to the power-on state: tables, video, width, alignment; the line buffer is cleared; DRC depending on N2-2 |
| 1B 48 / 1B 49 | VDOR / VDON | inverse video (white on black) / normal |
| 1B 4B / 1B 4C | TXTW / TXTN | double width, 12 characters / normal, 24 characters |
| 1B 26 3E / 1B 26 3C | TXTR / TXTL | align the line right / left (default). Not with the PDI set |

- So **CR, LF and CRLF each give one line feed**; an empty line is two of the same kind (LF LF) or CRLF CRLF. A paper tail of three lines must repeat the same character (`CR CR CR`), not alternate.
- In 8-bit mode the C1 commands can be single bytes: 0x83, 0x85, 0x88, 0x89, 0x8B, 0x8C (table 5.4). In 7-bit mode they are `ESC` + (byte − 0x40).
- **A misprint in table 5.5** (p. 35): for 7-bit mode it draws DEFDRC, ENDDRC, VDOR, VDON, TXTW and TXTN in column 3, which would give `ESC 33`, `ESC 38` and so on. The text, the DRC example (`1B 43 … 1B 45`) and ISO 2022 give column 4. **Column 4 is right** [measured].

**Checked on the printer, 2026-10-19** [measured; photos `probe1_lines_eol_ascii.jpg`, `probe2_24chars_tail.jpg`, `esc_probe_wide_inverse_align.jpg`]:

- `CR` only, `LF` only and `CRLF` each give exactly one line feed.
- A 30-character line **wraps by itself** after 24 characters, nothing lost. A line of exactly 24 characters + CRLF does not add an empty line.
- 0x20–0x7E all print correctly, 24 per line.
- `1B 4B` gives wide text, 12 per line, and wraps by itself; `1B 4C` goes back to 24.
- `1B 48` gives inverse; it prints thin and hard to read (see `graphics.md`).
- `1B 26 3E` aligns the line right, `1B 26 3C` left again.
- **The last printed line stays under the head**, about two lines below the tear edge: the printer only moves paper while printing. Two more lines bring it above the edge, three give a margin to tear. Or press Paper Feed with the Power switch in the top On position.

## Character sets [manual, pp. 27–40, appendices pp. 53–64]

- Based on ISO 646 / ISO 2022. The in-use table: 32 C0 control codes + 96 characters (GL). Four slots G0–G3.
- **Power-on default:** G0 = ISO 7-bit US-ASCII, G1 = PDI (graphics), G2 = Greek, G3 = DRC (own characters); GL = G0 [manual, p. 29]. Plain ASCII needs no prefix.
- **In 8-bit mode GR defaults to G1 = PDI** [manual, fig. 5.3, table 5.3]: 0x80–0x9F are C1 commands and 0xA0–0xFF PDI graphics commands, so random bytes 0xA0–0xB5 draw bars and move the cursor.
- Invoking into GL: SI `0F` → G0; SO `0E` → G1; SS2 `19` and SS3 `1D` → G2 and G3 for one character; `ESC 6E` / `ESC 6F` → G2 / G3, locking. In 8-bit mode, into GR: `ESC 7E`, `ESC 7D`, `ESC 7C` (p. 38).
- Designating a set into a slot (SCS): `ESC <slot> <set>`. Slots `28` G0, `29` G1, `2A` G2, `2B` G3 for normal (N) sets; `2D`, `2E`, `2F` for G1–G3 with the special (S) sets PDI `57` and DRC `7B`. For example, PDI back into G1: `1B 2D 57`.
- Sets: `42` US-ASCII, `43` English (£), `44` German, `45` French, `46` Spanish, `47` Italian, `48` Swedish, `49` Danish/Norwegian, `4A` Japanese (katakana), `4B` Greek, `4C` Russian, `57` PDI, `7B` DRC. **`4D` runes**: not in the printed list (`hardware.md`).
- **Dangerous bytes in text:** `04`, `0E`, `0F`, `19`, `1B`, `1D`, `1F`. Above all `0E` (SO): everything after it is read as PDI graphics commands. Pass only 0x20–0x7E as text.

### Tables printed by FrodeM's printer [forum]

FrodeM printed sets 0x42–0x4D, positions 0x20–0x7F. They confirm the list above.

- `4C` Russian is **KOI-7 N1**: Latin uppercase at 0x40–0x5F, and in place of the Latin lowercase 0x60–0x7E the Cyrillic uppercase `Ю А Б Ц Д Е Ф Г Х И Й К Л М Н О П Я Р С Т У Ж В Ь Ы З Ш Э Щ Ч`. **No lowercase Cyrillic**, no Ё or Ъ. `¤` in place of `$`.
- `4D` runes: only 0x40–0x70 are filled.
- National Latin sets replace `#`, `@`, `[ \ ]`, `{ | }`, `~` and so on. We have not checked them position by position.

### Cyrillic, checked on the printer 2026-10-19 [measured; `koi7_cyrillic_probe.jpg`]

- **Method A, Russian in G0:** `1B 28 4C` + KOI-7 bytes → `ПРИВЕТ, 2318! ABC` correct; row 0x60–0x7E matches FrodeM's table. Only uppercase Latin; `1B 28 42` goes back to ASCII.
- **Method B, Russian in G1 with SO/SI (used by our firmware):** `1B 29 4C` once, then `SO 0E` → Cyrillic, `SI 0F` → ASCII. `Sony \x0E КАМЕРА \x0F fx3` → **`Sony КАМЕРА fx3`**: Latin lowercase stays, mixed text in one line works.
  - Risk: after power-on or NSR, G1 is PDI again, and SO would switch to graphics. Send `1B 29 4C` before every line with Cyrillic.
- Mapping: Ю60 А61 Б62 Ц63 Д64 Е65 Ф66 Г67 Х68 И69 Й6A К6B Л6C М6D Н6E О6F П70 Я71 Р72 С73 Т74 У75 Ж76 В77 Ь78 Ы79 З7A Ш7B Э7C Щ7D Ч7E. Our choice: Ё → Е; Ъ as an own DRC glyph (or Ь). Lowercase Cyrillic as DRC glyphs: `graphics.md`.

## Commands of other B&K instruments: do not send to the printer

Settings like these appear in the manuals of the instruments that drive a 2318. They are for those instruments, not for the printer.

| Command | Instrument |
|---|---|
| `BAUD 12`, `HANDSHAKE X`, `FORMAT L_R PR24 SH`, `PRINT L_R`, ETX 0x03 | 2236 |
| `TY 1` + Enter, `P` + Enter | 2231 / BZ 7110 |
| SY-8: `0000` English … `0005` Russian | 2231 setting |
