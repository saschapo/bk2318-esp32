# Type 2318 + ZI 0054: hardware

Tags: [manual] the 2318 + ZI 0054 instruction manual (page numbers are the ones printed on the page) · [forum] FrodeM on EEVblog · [seller] listing photos · [measured] on our unit · [hypothesis] not verified.

## Our unit

- SN **1941478**, Made in Denmark, ZI 0054 fitted. No cables or accessories; bought for $15.
- **Set-up printout** [measured, 2026-10-19]: `PRINTING DENSITY 4`, `DATA MODE: 7 BITS` (N2-1 = OFF, factory), GL and G0 = ISO 7 bit (US ASCII), G1 = Picture Description, G2 = Greek, G3 = Dynamic Redefinable; `BAUD RATE: 1200`, `XON/XOFF: ENABLED`.
- The `INTERFACE` block of the set-up always says `8 DATA BITS (FIXED) / 1 STOP BIT (FIXED) / NO PARITY CHECK (FIXED)`. It says nothing about N2-1: the 7/8-bit mode is the separate `DATA MODE` line [manual, p. 10].
- **PROM: `2318-ZI0054 (VP1915)`** [measured, print test], the same version as FrodeM's, so the rune set is there.
- Photos: `evidence/printer_SN1941478/setup_printout.jpg`, `setup_and_selftest_printout.jpg`.

## Button modes and tests [manual, pp. 9–11]

The Power switch has Off and three On positions. They only change what the button does; data printing works in all three.

- top: **Paper Feed**, feeds while the button is held;
- middle: **Print Set-up**, the button prints the current settings: DATA MODE, G0–G3 sets, baud rate, XON/XOFF;
- bottom: **Set Printing Density**, the button steps the density 1 → 5 and round, and prints the new value;
- **print test**: switch on (any On position) with the button held. Prints the module and PROM version, then rolling ASCII lines. Switch off to stop.

## Specifications [manual, ch. 6, p. 49]

- Input: 7- or 8-bit ASCII per ISO 646, ISO 2022, ANSI X3.110; **buffer about 7 KB**.
- B&K-SI interface; RS-232C when the module is jumpered for ±10 V; 50–19200 baud (a gap between characters at 9600 and 19200); hardware READY/BUSY + XON/XOFF.
- 7-dot single-column matrix; 11 character sets, 96 user-defined 7×7 characters, PDI graphics; 24 normal or 12 wide characters per line; left or right alignment; inverse video.
- **0.8 lines/s**; 58 mm paper, roll about 15 m, about 4000 lines; paper QP 0021.
- Power: 4 × 1.25 V NiCd, 0.5 Ah (ZG 0270), charger ZG 0271, about 5 h; **printing current < 1.0 A average, 3.75 A peak**; standby < 25 mA (±5 V) or < 40 mA (±10 V); at least 1000, typically 2000 lines per charge at 25 °C.
- +5…+45 °C; 202 × 85 × 52 mm; about 1 kg.
- Supplied with: ZI 0054, ZG 0270, ZG 0271, QP 0021, plug JP 0510, adaptor JN 0018, case KE 0273. Optional: ZI 9100, cables AO 0299, AO 0175, AO 0176, AO 0177; service manual.
- Charge the batteries only with ZG 0271, at 5–45 °C [manual, p. 5].

The 1991 B&K catalogue says the same: thermal, 7-dot vertical matrix, 24/12 characters, about 0.8 lines/s, 58 mm paper, 50–19200 baud, 11 character sets (US-ASCII, European, Russian, Greek, Japanese) plus 96 user characters.

## Front panel

- Button "Paper Feed / Print Set-up / Set Printing Density".
- Power switch: Off and three On positions (p. 9).
- Batt. Condition lamp.
- **B&K-SI** socket on the ZI 0054: 5-pin LEMO socket for the B&K plug JP 0510 [manual, p. 22], red key dot on top. Pinout: `wiring.md`.

## Firmware and the rune set [forum]

- FrodeM's unit has ROM **VP1915** and an **RCA 1805 (COSMAC)** CPU.
- The rune set `M` (0x4D) is not in the printed manual. FrodeM wrote it into his copy by hand after finding it on the printer, marked "VP1915". It is not a B&K note, so other firmware versions may not have it. To use it: `ESC ) M` = `1B 29 4D` (runes into G1), then SO `0E`. Only 0x40–0x70 are filled; no digits or punctuation.

## The manual

"Instruction Manual — Graphics Printer Plus Serial Interface Module", June 1988, "From serial no. 1129094". FrodeM scanned it and put it on archive.org: https://archive.org/details/b-k-2318-zi-0054-instruction-manual

| Chapter | Sections and pages |
|---|---|
| 1 Introduction | 1 |
| 2 Setting-up | Switching On 3, Recharging the Batteries 4, Changing the Paper 6, Paper gets Stuck 7, Paper Storage 8 |
| 3 Using the Type 2318 | Three "On" Positions 9, Using the Printer with B&K Instruments 12 |
| **4 Interface** | **Interface 15, Plugs, Sockets, and Cables 22** |
| **5 Programming** | **Introduction to Character Coding 27, Control Commands 36** |
| 6 Specifications | 49 |
| 7 Service and Repair | 51 (one page, no schematics) |
| Appendices | 53–64: tables of the 11 character sets and PDI |

Read tables and figures as images: OCR breaks them.

## Printing and head care

- The head is **one column of 7 dots on a carriage** that moves along the line [manual, p. 49: "7-dot single-column matrix"]. Hence 0.8 lines/s and the high peak current.
- The density setting (1–5) is kept by the printer and survives power-off [measured, probably].
- **Diagnosis 2026-10-19** [measured; `evidence/printer_SN1941478/inverse_blank_bars_density5.jpg`]: at density 5, three lines of inverse spaces give solid bars. All 7 dot rows are there across the whole width, with no permanent white line, so no head dot is dead or clogged. The bars were blotchy and grey, with light spots in random places.
- **A new 57 mm till roll fixed it** [measured; `new_paper_57mm_density5.jpg`]: same test, even dark bars, crisp text. **Faint printing was old paper**; the head did not need cleaning. Inverse text is still hard to read: white strokes are 1 dot wide [hypothesis: heat bleeds into them at density 5].
- Paper [manual, pp. 6 and 49]: 58 mm wide, roll diameter up to 40 mm, thermal side out, facing down when loading. A normal 57–58 mm till roll up to 40 mm fits [measured with 57 mm].
- The manual says **nothing** about cleaning the head. Access to the mechanism: remove the metal tear bar (slide the black catch, lift one corner, then the other) [manual, p. 7, "What to do if the paper gets stuck"].
- Alcohol and acetone vapour darken the paper [manual, p. 8]. After cleaning with alcohol, let it evaporate fully with the roll out.
- Cleaning (general thermal-head practice, not from the manual) [hypothesis]: printer off and cool; 90–99 % isopropyl alcohol on a cotton swab or lint-free cloth; gently, along the dot row; no metal or abrasives; let it dry for a few minutes.

## Power: be careful

**FrodeM's warning** [forum, 2026-10-15]:

- The circuit relies on the batteries holding the voltage at a little over 5 V. There is a series diode in the battery compartment against reverse discharge.
- **Without batteries, never put more than 6 V on the round power socket.** The original mains adaptor gives noticeably more.
- Running from the original adaptor without batteries can burn rows of the thermal head. After that the printer trips its protection and stops printing altogether.
- The battery voltage mainly feeds the print mechanism; the rest is behind a switching regulator.

**FrodeM's follow-up** [forum, 2026-10-19]: his batteries had leaked, so he runs from the socket without them. About 5.7 V on the socket corresponds to about 5 V at the battery terminals (the 0.7 V drop is presumably the series diode). For darker print he raised it by 0.2–0.3 V, which is right at his own 6 V limit. We did not repeat that: our faint print was old paper.

What we do:

- Run only from charged batteries. Never use the original adaptor without batteries; no bench supply on the socket.
- NiMH cells are fine for voltage. The built-in charger is made for NiCd: charge NiMH externally until its charging current is measured.
- No alkaline cells while a charger is connected. Check the compartment for corrosion.

## Inside the ZI 0054 [manual, pp. 18–21]

- The module slides out of the printer. Its board has two DIP switch blocks: **N1** (baud rate, XON/XOFF, EOT) and **N2** (7 or 8 bits, power-on reset). Layout: fig. 4.3. Switch tables: `protocol.md`.
- Two interface-level jumpers: ±5 V (factory) or ±10 V (fig. 4.4). See `wiring.md`.
- **The printer's brain is in the module** [forum, FrodeM]: the microcontroller (RCA 1805) and the VP1915 firmware are on the ZI 0054. As FrodeM recalls, there is an external SRAM for the settings and probably the DRC characters.
- **Module, component side** [measured; `zi0054_dip_N1_N2_2026-10-20.jpg`]: EPROM NM27C256 labelled VP1915, HD-4702 (baud rate generator), N1 (8 switches) and N2 (2 switches).

### Backup NiCd cells: check yours

- Two soldered-in rechargeable NiCd button cells keep the SRAM alive in standby. They are **on the brown board inside the printer, under the module** [measured, 2026-10-20], next to a CDP1853E, a 2.4576 MHz crystal and the connector to the module. They are not on the module itself.
- **Both of ours had leaked** [measured; `evidence/printer_SN1941478/backup_nicd_leak_*.jpg`]: white-green crust on the tab and lid of one, white deposit and green spots on the other, green corrosion on the via between them, darkened pads. FrodeM's had started to leak too. On a 35-year-old unit, expect this: remove the cells and wash the board with isopropyl alcohol before the corrosion spreads.
- Without them, settings and DRC characters will probably not survive power-off [hypothesis]. Before removal, ours kept the density setting and the DRC characters through 15 s without batteries (`graphics.md`).
