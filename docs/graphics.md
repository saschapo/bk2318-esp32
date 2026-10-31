# Own characters (DRC) and PDI graphics

Tags: [manual] · [forum] · [measured] on SN 1941478 · [calc] · [hypothesis] · [conclusion] · [choice] a design decision for our firmware. Page numbers are the ones printed in the manual.

## Own characters, DRC [manual, pp. 41–42]

`ESC 43 <index> <7 columns × 2 bytes> ESC 45`. The index is a position in the table (0x23 = the place of `#`). Columns left to right; the first and last are usually empty (the gap between characters). First byte of a column `X1DDDDDD`: bit 5 = top dot, bit 0 = sixth from the top. Second byte: bit 5 = seventh, bottom dot. Bytes are 0x40–0x7F in 7-bit mode, 0xC0–0xFF in 8-bit mode. The manual's example, digit 7 in place of `#`: `1B 43 23 40 40 60 40 63 60 64 40 68 40 70 40 40 40 1B 45`. Print with `SS3 1D` + index (G3 = DRC at power-on).

- Cell 7 × 7 dots; a dot is about 0.27 × 0.29 mm, a glyph about 1.9 × 2.0 mm (5 × 7 dots + 2 empty columns).
- A row of glyphs that use all 7 columns prints as one continuous band: 168 dots = 24 × 7, no gap between cells [measured].

**First DRC probe, 2026-10-19** [measured; `evidence/printer_SN1941478/drc_mock_receipt.jpg`, `tools/drc_receipt.py`, glyphs in `glyphs/drc_probe_2026-10-19.txt`]:

- 14 glyphs loaded with one `ESC C <idx> <14 bytes> ESC E` each (indices 0x21–0x2E), printed with `SS3 1D` + idx. Worked first time. Put `SS3` after `SI` if Cyrillic from G1 came before.
- Good: Ъ and Ё in "СЪЁМКА"; lowercase д, ж, я, ё sit level with the ROM Latin а, о, р, с, х; bold ✔ ✖ and the arrows read well.
- Bad: an empty round ☐ looks like О/0; a thin ✗ looks like Latin x. **The ROM Latin y looks like Cyrillic "ч"**, so "уха" reads "чха".
- **DRC characters survive** [measured; `drc_persistence_power_nsr.jpg`]: after the battery pack was **out for about 15 s** and after an NSR `1F`, a line with ☑ printed it without reloading. So N2-2 = ON (factory) and the SRAM is backed for at least 15 s. Longer was not tested, and our backup cells had leaked (`hardware.md`), so our firmware reloads the glyphs it needs in every job [choice]: 17 bytes per glyph, about 0.14 s at 1200 baud.

**Glyphs built into firmware v2** [`glyphs/drc_approved.txt`]: ✓ ✗ Ъ ☐ … → ─ at 0x21–0x27. ✓ and ✗ are lowered one row to sit on the line. ☐ is a full 7 × 7 square for ticking with a pen; two in a row merge, so put a space between. A row of ─ prints as a solid line.

## PDI graphics [manual, pp. 42–48, table p. 64]

- A print line is 168 dots (0–167), 2 mm high, 7 rows, 46 mm wide [fig. 5.4]. 168 = 24 × 7: a character is 7 dots including the gap. Scale SCLE: "1.0" = 165 − crp dots, 162 by default.
- On: SO `0E` (G1 = PDI into GL); back to text: SI `0F`. Commands use codes 0x20–0x35: `20` COS, `21` COT, `22` CEL, `23` CER, `24`/`25` move the cursor right/left by 1–63 dots (argument 0x41–0x7F), `26` CMRR, `27` SCLE, `28` LINE/LLINE, `29` TAB, `2A` DOT (one column of dots), `30`–`35` bars (filled, hatched, outline; wide/narrow), `2F` BK.
- Lengths are a binary fraction in two bytes `X1LLLLLL`: length = (b1 & 0x3F)/64 + (b2 & 0x3F)/4096 of SCLE. Example `66 58` = 0.5996 (the manual's CMRR example has the misprint "0.5926").
- LINE `28` + `X10TTYYY` + length: TT = 00 solid, 01 dotted, 10 dashed, 11 dash-dot. YYY = 001 (bottom row) … 111 (top). LLINE is the same with bit 5 = 1 (`X11TTYYY`): a full-width line, no length.
- `2A` DOT + 2 bytes (same format as a DRC column) draws one 7-dot column and moves the cursor one dot.
- `2F` BK appears only in the PDI table (p. 64) and is not described in the text. Don't send it blind.
- LLINE recipe [measured]: `ESC - W` (PDI into G1), `SO 28 <0x60|TT<<3|Y> SI`, CR LF. All 4 line types print. If G1 held Russian, send `ESC ) L` again afterwards, or keep PDI in G2 (below).

### PDI probe, 2026-10-20 [measured; `evidence/printer_SN1941478/pdi_probe_2026-10-20.jpg`, `tools/pdi_probe.py`, log next to it]

One strip with 21 numbered checks and a mock receipt.

- **DOT** works: a solid block, a staircase and a checkerboard print as drawn. **There is a white gap between print lines** about as tall as the line itself (see the scan numbers below), so images and QR codes come out striped.
- **LINE** with length and row: all 4 types; rows 7/4/1 at different heights.
- **Bars `30`–`35`:** all 6 kinds. After a bar **the cursor moves by its length**: two bars in a row without a cursor move sit end to end.
- **Overlay works (dots are ORed):** a wide outline bar over a narrow filled one in the same place (as in fig. 3.6); text and a bar in one line.
- **Text anywhere:** an `X` placed by CMRR 0.5996 (the manual's example) lands at about 60 % of the width.
- **Bold by overprinting:** the same text twice, the second time shifted by 1 dot, gives a clean bold; a 2-dot shift looks doubled. So the line is built as a raster and repeated text is ORed [conclusion].
- **Wide and normal text in one line** work (`ESC K` … `ESC L` mid-line). Own glyphs are doubled in wide mode too.
- **PDI in G2** (`ESC 2E 57`, `ESC 6E`, back with `SI`) prints the same as swapping G1, so Russian can stay in G1 and graphics live in G2. Firmware v2 does this.
- **Code 128 "0125"** (module 2 dots, 114 DOT columns, 3 lines high with white gaps) was **read first time by a Datalogic Memor scanner**. The gaps between lines don't matter.
- The first run went to a printer that was **off**: the adapter said `OK` to everything and the bytes vanished. Hence the power check in firmware v2.

### Receipt-editor probe, 2026-10-26 [measured; 600 dpi scan `evidence/printer_SN1941478/tape_probe_2026-10-26_scan600.jpeg`, `tools/tape_probe.py`, log next to it]

Two firmware v2 jobs of raw bytes. Measured on the scan: dot pitch across 0.277 mm; **line pitch 3.94 mm** for 7 dot rows of about 2.0 mm, so pitch / printed height ≈ 1.95. Use that to correct the aspect ratio of images.

- **ROM font:** a character uses columns 1–5 of its 7 × 7 cell. Zero is slashed, `y` looks like "ч", `g` like "9"; `q` and `t` have their own shapes.
- **Wide** doubles every column (`##`), with no gaps.
- **Inverse only affects characters:** text, a title on a black band made of spaces, a white zigzag on a DRC tile. It has no effect on PDI graphics: hatching and LLINE printed normally. Small white text is hard to read (1-dot strokes).
- **Wide + inverse looks unreadable on stoneset's printer** [forum, his photo in reply #23 of the EEVblog thread, 2026-10-31]: the banners "CALIBRATED" and "TEST" came out as a grey band with broken letters, while the normal text and the line on the same receipt are clean. The cause is unclear (head, paper or batteries on a solid fill) [hypothesis]. Not tried on ours.
- **Bold, second pass shifted by 1 dot:** good, but glyphs with strokes one dot apart fill into a block (Ш, Ж; by the rule "#.#" in 3+ rows also # & @ M W m w Д Л М Ф Щ Ю), so our editor doesn't bolden them. **Inverse bold is unreadable**: the shifted black background eats the white letters. **Wide bold is no thicker than wide** (2-dot stroke, not 3) [hypothesis: in wide mode text snaps to its own grid and the 1-dot shift is lost].
- **A second pass past dot 168 pushes the last character onto a new line.** Shift the last character left, not right, at the right edge. Normal text ending exactly on dot 168 does not wrap.
- **Centre and right alignment by the PDI cursor** are exact.
- **DRC 7 × 7 tiles** (zigzag, double line, waves and so on) print clean, edge to edge; scissors + a LINE segment make a tear line.
- **Dash patterns:** full-width LLINE dashes repeat every 3 dots (`##.`), a LINE segment every 7 (`####...`).
- **Bars:** wide ones fill the whole line height, narrow ones 4 rows; hatching is a checkerboard between solid edges; outline is two lines and a bar at the left end. DOT dotted lines 1 and 2 dots apart are even.
- **Barcodes:** Code 128 with a **1-dot module also reads** with the Memor: "0125" is 57 dots (8 mm) wide.

### Ideas from chapter 5.2.7 [manual; not all tried]

- **Strip charts like the 2231's** (figs. 3.5–3.6): one line per reading, bar length = value (`30`–`35` + length as a fraction of SCLE), text and bars in one line, two bars overlaid on one scale. Handy for sensors: 0.8 lines/s on endless tape.
- **Raster graphics by DOT:** a full line is 168 × 3 = 504 bytes, about 4.2 s at 1200 baud [calc], plus the white gaps between lines.
- **Icons are cheaper as DRC** than as DOT: define once (up to 96), then print as one byte instead of 21.

## Lowercase Cyrillic as DRC, 2026-10-27 [measured; scan `evidence/printer_SN1941478/lowercase_probe_2026-10-27_scan.png`, `tools/lowercase_probe.py`, log next to it]

The printer's Russian set has uppercase only. Glyph table: `glyphs/drc_lowercase.txt`.

- **Seven letters match the ROM Latin font dot for dot:** а→a, и→u, о→o, р→p, с→c, у→y, х→x. Print them as Latin (after `SI` if Cyrillic from G1 came before): no glyph needed, 1 byte. The other 26 are own glyphs at **0x37–0x50** in alphabetical order, printed with `SS3 1D` + index. With our other glyphs, 0x21–0x50 are taken and 0x51–0x7E (46) are free.
- Load them in every job, only the letters used: 17 bytes ≈ 0.14 s at 1200 baud per letter; all 26 are 442 bytes ≈ 3.7 s before the first line.
- Indices 0x20 and 0x7F were left alone (0x7F is DEL); neither was tested.
- Results:
  - 49 definitions in one job (931 bytes, 7.8 s) all loaded; all 26 glyphs 0x37–0x50 and 0x7E printed correctly, and tiles loaded before them were intact. The Latin look-alikes sit level with the glyphs.
  - Text reads well, also mixed with uppercase Cyrillic and lowercase Latin ("Камера Sony fx3"). Ё → Е, Ъ and ъ correct.
  - Wide doubles the lowercase and reads well. Bold is fine except **ш**, which fills into a block as the "#.#" rule predicts; by that rule ж л ш щ ю stay normal.
  - **Inverse lowercase is unreadable**: 1-dot white strokes over 5 rows drown in black. Uppercase in inverse was hard to read; lowercase is clearly worse.
  - е and Latin e are almost the same on paper (one dot in the bottom row). п/n and к/k are distinct.
- Our firmware's `RAW` command line is limited to 1024 characters, so 931 bytes of definitions (1862 hex digits) must be split into several `RAW` commands.
