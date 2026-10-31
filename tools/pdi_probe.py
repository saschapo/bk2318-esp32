# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial>=3.5", "python-barcode>=0.15"]
# ///
"""PDI graphics probe (manual ch. 5.2.7) + v2 glyphs + mock receipt, one strip.

usage: pdi_probe.py dry          print every line as hex, no port
       pdi_probe.py PORT run     load glyphs from glyphs/drc_approved.txt, print
Talks to firmware bk2318_v1 through RAW. Each test line starts with its number.

01 glyphs ☐ ☐☐ ─×7 … · →           11 text + two overlaid bars (fig. 3.6)
02-04 location formats              12 X at CMRR 0,5996 (manual example)
05-06 DOT block, gap between lines  13 bold: 0125 twice +1 dot | normal | +2 dots
07 LINE 4 types, lengths, rows      14 wide 0125 + normal text in one line
08 bars wide: filled hatch outline  15 glyphs in wide mode
09 bars narrow                      16 PDI in G2, Russian stays in G1
10 two bars without cursor move     17 PDI by swapping G1 back and forth
                                    18 rule with title from ─   19 same from LINE
                                    20 name cut with …   21 Code 128 "0125"
then a mock checkout / put-away receipt.
"""
import pathlib
import sys
import time

ESC, SO, SI, SS3 = 0x1B, 0x0E, 0x0F, 0x1D
RU = "ЮАБЦДЕФГХИЙКЛМНОПЯРСТУЖВЬЫЗШЭЩЧ"  # set 0x4C, positions 0x60..0x7E
RU_KOI7 = {ch: 0x60 + i for i, ch in enumerate(RU)}
GLYPH_FILE = pathlib.Path(__file__).resolve().parent.parent / "glyphs" / "drc_approved.txt"


def load_glyphs():
    """{char: (index, 7 rows)} from glyphs/drc_approved.txt."""
    lines, out = GLYPH_FILE.read_text().splitlines(), {}
    for i, ln in enumerate(lines):
        parts = ln.split()
        if len(parts) >= 4 and parts[0] == "#" and parts[2].startswith("U+") and parts[3].startswith("0x"):
            out[parts[1]] = (int(parts[3], 16), lines[i + 1:i + 8])
    return out


GLYPHS = load_glyphs()


def column(rows, c):
    """DRC/DOT column: 6 upper dots in byte 1 (bit 5 = top), the 7th in byte 2."""
    b1 = 0x40 | sum(1 << (5 - r) for r in range(6) if rows[r][c] == "#")
    return [b1, 0x40 | (0x20 if rows[6][c] == "#" else 0)]


def define_all():
    out = bytearray()
    for idx, rows in GLYPHS.values():
        out += bytes([ESC, 0x43, idx])
        for c in range(7):
            out += bytes(column(rows, c))
        out += bytes([ESC, 0x45])
    return bytes(out)


def text(s):
    """ASCII as is, uppercase Cyrillic via G1 (SO/SI), own glyphs via SS3."""
    s = "".join(ch.upper() if ("а" <= ch <= "я" or ch == "ё") and ch not in GLYPHS else ch
                for ch in s).replace("Ё", "Е")
    out, ru = bytearray(), False
    if any(ch in RU_KOI7 for ch in s):
        out += bytes([ESC, 0x29, 0x4C])              # Russian set into G1
    for ch in s:
        if ch in GLYPHS:
            if ru:
                out.append(SI); ru = False
            out += bytes([SS3, GLYPHS[ch][0]])
        elif ch in RU_KOI7:
            if not ru:
                out.append(SO); ru = True
            out.append(RU_KOI7[ch])
        else:
            if ru:
                out.append(SI); ru = False
            out.append(ord(ch) if 0x20 <= ord(ch) < 0x7F else ord("?"))
    if ru:
        out.append(SI)
    return bytes(out)


# ---- PDI commands; valid only between pdi_on() and SI ----
def pdi_on():
    return bytes([ESC, 0x2D, 0x57, SO])              # PDI into G1, shift in


def at(dot):
    """Cursor to an absolute dot 0..167: CEL, then CMR steps of up to 63."""
    out = bytearray([0x22])
    while dot > 0:
        n = min(dot, 63)
        out += bytes([0x24, 0x40 | n]); dot -= n
    return bytes(out)


def length(dots):
    """Relative length in two bytes; default SCLE: 1,0 = 162 dots."""
    n = min(round(dots / 162 * 4096), 4095)
    return bytes([0x40 | n >> 6, 0x40 | n & 0x3F])


def line(kind, y, dots):
    """LINE: kind 0 solid, 1 dots, 2 dashes, 3 dash-dot; y 1 = bottom row .. 7 = top."""
    return bytes([0x28, 0x40 | kind << 3 | y]) + length(dots)


def bar(code, dots):
    return bytes([code]) + length(dots)


def dots(cols):
    """DOT per column; each column is 7 chars top to bottom."""
    out = bytearray()
    for col in cols:
        out += bytes([0x2A] + column([[ch] for ch in col], 0))
    return bytes(out)


FULL, EMPTY = "#######", "......."
CRLF = b"\r\n"
WIDE, NORMAL = bytes([ESC, 0x4B]), bytes([ESC, 0x4C])


def barcode_cols(value, module=2):
    from barcode import Code128
    bits = Code128(value).build()[0]
    return [FULL if b == "1" else EMPTY for b in bits for _ in range(module)]


def tests():
    stair = ["".join("#" if r == i % 7 else "." for r in range(7)) for i in range(14)]
    checker = ["#.#.#.#" if i % 2 == 0 else ".#.#.#." for i in range(14)]
    bc = barcode_cols("0125")
    bc_at = (168 - len(bc)) // 2
    return [
        text("01 ☐ ☐☐ ─────── … · →"),
        text("02 ШКАФ 1 · ПОЛКА 2"),
        text("03 Ш1 П3 · БОКС РОЗОВЫЙ"),
        text("04 ШКАФ 1 ПОЛКА 2"),
        text("05") + pdi_on() + at(21) + dots([FULL] * 42) + at(70) + dots(stair) + at(91) + dots(checker) + bytes([SI]),
        text("06") + pdi_on() + at(21) + dots([FULL] * 42) + bytes([SI]),
        text("07") + pdi_on() + at(21) + line(0, 7, 30) + at(56) + line(1, 4, 30)
        + at(91) + line(2, 1, 30) + at(126) + line(3, 4, 35) + bytes([SI]),
        text("08") + pdi_on() + at(21) + bar(0x30, 30) + at(63) + bar(0x32, 30) + at(105) + bar(0x34, 30) + bytes([SI]),
        text("09") + pdi_on() + at(21) + bar(0x31, 30) + at(63) + bar(0x33, 30) + at(105) + bar(0x35, 30) + bytes([SI]),
        text("10") + pdi_on() + at(21) + bar(0x31, 25) + bar(0x34, 25) + dots([FULL, FULL]) + bytes([SI]),
        text("11 125 1.99") + pdi_on() + at(84) + bar(0x34, 70) + at(84) + bar(0x31, 45) + bytes([SI]),
        text("12") + pdi_on() + bytes([0x21, 0x26, 0x66, 0x58, SI]) + text("X"),
        text("13") + pdi_on() + at(21) + bytes([SI]) + text("0125") + bytes([SO]) + at(22) + bytes([SI]) + text("0125")
        + bytes([SO]) + at(63) + bytes([SI]) + text("0125")
        + bytes([SO]) + at(105) + bytes([SI]) + text("0125") + bytes([SO]) + at(107) + bytes([SI]) + text("0125"),
        text("14 ") + WIDE + text("0125") + NORMAL + text(" FX3 КОРПУС"),
        text("15 ") + WIDE + text("☐✓✗") + NORMAL + text(" ☐✓✗"),
        text("16 ШКАФ") + bytes([ESC, 0x2E, 0x57, ESC, 0x6E]) + at(56) + line(0, 4, 28) + at(91) + bytes([SI]) + text("ПОЛКА"),
        text("17 ШКАФ") + pdi_on() + at(56) + line(0, 4, 28) + at(91) + bytes([SI]) + text("ПОЛКА"),
        text("18 ── ПРОВЕРКА ─────────"),
        text("19") + pdi_on() + at(21) + line(0, 4, 35) + at(63) + bytes([SI]) + text("PUT AWAY")
        + bytes([SO]) + at(126) + line(0, 4, 41) + bytes([SI]),
        text("20 0307 ШТАТИВ MANFROT…"),
        text("21 CODE 128: 0125"),
    ] + [pdi_on() + at(bc_at) + dots(bc) + bytes([SI])] * 3 + [
        text("    0125"),
        # mock receipt with the formats agreed so far
        WIDE + text("KIA SPOT") + NORMAL,
        text("20.10-22.10  3 ДНЯ"),
        text("── 2001 BAG ────────────"),
        text("☐ 0125 FX3 КОРПУС"),
        text("☐ 0304 V-MOUNT 150WH x4"),
        text("☐ 0307 ШТАТИВ MANFROTT…"),
        text("── PUT AWAY ────────────"),
        text("ШКАФ 1 · ПОЛКА 3"),
        text(" ☐ 0125 FX3 КОРПУС"),
        text(" СЛЕВА, ПОД КОФРОМ"),
        text("── MISSING 1/14 ────────"),
        text("✗ 0307 ШТАТИВ MANFROTTO"),
        text("✓ 0125 FX3 КОРПУС"),
        b"", b"", b"",                                # feed so the end clears the tear bar
    ]


def payload():
    return b"".join(t + CRLF for t in tests())


def send(port, data, log):
    import serial
    with serial.Serial(port, 115200, timeout=0.1) as s:
        s.reset_input_buffer()
        i = 0
        while i < len(data):
            chunk = data[i:i + 400]                  # RAW line stays under 1024 chars
            s.write(b"RAW " + chunk.hex().upper().encode() + b"\n")
            deadline, buf = time.time() + 3, b""
            while b"OK" not in buf and b"ERR" not in buf and time.time() < deadline:
                buf += s.read(256)
            log(buf.decode(errors="replace").strip())
            if b"ERR FULL" in buf:
                time.sleep(2); continue              # adapter queue full: same chunk again
            if b"OK" not in buf:
                log("stop: no OK"); return
            i += len(chunk)
        s.write(b"DRAIN\n")
        deadline, buf = time.time() + 120, b""
        while b"OK" not in buf and time.time() < deadline:
            buf += s.read(256)
        log(buf.decode(errors="replace").strip() + f" ({len(data)} bytes)")


if __name__ == "__main__":
    if sys.argv[1] == "dry":
        for n, t in enumerate(tests(), 1):
            print(f"{n:2} {len(t):4} {t.hex(' ').upper()}")
        print("glyphs", len(define_all()), "bytes; strip", len(payload()), "bytes")
    else:
        send(sys.argv[1], define_all() + payload(), print)
