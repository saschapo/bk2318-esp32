# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial>=3.5"]
# ///
"""DRC glyph probe: load 14 own glyphs into G3 and print a mock checkout receipt.

usage: drc_receipt.py PORT load|receipt|line|reset
  load     define the glyphs (no paper)
  receipt  print the mock receipt (glyphs must be loaded)
  line     print only the "☑ 1001 ..." line, to see if the glyphs survived
  reset    send NSR (1F)
Talks to firmware bk2318_v1 through RAW, since PRINT knows no DRC yet.
"""
import sys
import time

import serial

# 7x7, rows top to bottom; designs agreed in chat 2026-10-19.
GLYPHS = {
    "☑": ["..###..", ".#...#.", "#..#..#", "#.###.#", "#..#..#", ".#...#.", "..###.."],
    "☐": ["..###..", ".#...#.", "#.....#", "#.....#", "#.....#", ".#...#.", "..###.."],
    "✔": ["##....#", "##...##", "##..##.", "##.##..", "####...", "###....", "##....."],
    "✖": ["##...##", "###.###", ".#####.", "..###..", ".#####.", "###.###", "##...##"],
    "Ъ": [".##....", "..#....", "..#....", "..###..", "..#..#.", "..#..#.", "..###.."],
    "✓": [".....#.", ".....#.", "....#..", ".#..#..", "..##...", "..#....", "......."],
    "✗": [".......", ".#...#.", "..#.#..", "...#...", "..#.#..", ".#...#.", "......."],
    "Ё": ["..#.#..", ".......", ".#####.", ".#.....", ".####..", ".#.....", ".#####."],
    "ё": ["..#.#..", ".......", "..###..", ".#...#.", ".#####.", ".#.....", "..###.."],
    "д": [".......", ".......", "..###..", "..#.#..", ".#..#..", ".#####.", ".#...#."],
    "ж": [".......", ".......", ".#.#.#.", ".#.#.#.", "..###..", ".#.#.#.", ".#.#.#."],
    "я": [".......", ".......", "..####.", ".#...#.", "..####.", "..#..#.", ".#...#."],
    "→": [".......", "...#...", "....#..", ".#####.", "....#..", "...#...", "......."],
    "←": [".......", "...#...", "..#....", ".#####.", "..#....", "...#...", "......."],
}
DRC_INDEX = {ch: 0x21 + i for i, ch in enumerate(GLYPHS)}

ESC, SO, SI, SS3, NSR = 0x1B, 0x0E, 0x0F, 0x1D, 0x1F
RU = "ЮАБЦДЕФГХИЙКЛМНОПЯРСТУЖВЬЫЗШЭЩЧ"  # set 0x4C, positions 0x60..0x7E
RU_KOI7 = {ch: 0x60 + i for i, ch in enumerate(RU)}
LATIN_TWIN = {"а": "a", "е": "e", "о": "o", "р": "p", "с": "c", "у": "y", "х": "x"}


def columns(rows):
    out = []
    for c in range(7):
        b1 = 0x40 | sum(1 << (5 - r) for r in range(6) if rows[r][c] == "#")
        b2 = 0x40 | (0x20 if rows[6][c] == "#" else 0)
        out += [b1, b2]
    return bytes(out)


def define_all():
    return b"".join(bytes([ESC, 0x43, DRC_INDEX[ch]]) + columns(rows) + bytes([ESC, 0x45])
                    for ch, rows in GLYPHS.items())


def text(s):
    """One line: ASCII as is, uppercase Cyrillic via G1 (SO/SI), own glyphs via SS3."""
    out, ru = bytearray([ESC, 0x29, 0x4C]), False   # Russian set into G1
    for ch in s:
        if ch in DRC_INDEX:
            if ru:
                out.append(SI); ru = False
            out += bytes([SS3, DRC_INDEX[ch]])
            continue
        ch = LATIN_TWIN.get(ch, ch)
        koi = RU_KOI7.get(ch.upper()) if "Ѐ" <= ch <= "ӿ" else None
        if koi:
            if not ru:
                out.append(SO); ru = True
            out.append(koi)
            continue
        if ru:
            out.append(SI); ru = False
        out.append(ord(ch) if 0x20 <= ord(ch) < 0x7F else ord("?"))
    if ru:
        out.append(SI)
    return bytes(out) + b"\r\n"


def rule(style):
    """Full-width PDI line (LLINE) at the middle row: solid, dots, dashes, dashdot."""
    tt = {"solid": 0, "dots": 1, "dashes": 2, "dashdot": 3}[style]
    return bytes([ESC, 0x2D, 0x57, SO, 0x28, 0x60 | tt << 3 | 4, SI]) + b"\r\n"  # PDI into G1


def wide(b):
    return bytes([ESC, 0x4B]) + b + bytes([ESC, 0x4C])


LINE_CHECK = "☑ 1001 КОФР SONY FX3"
RECEIPT = [
    wide(text("ВЫЕЗД 42")),
    rule("solid"),
    text("19.10 СЪЁМКА → 21.10"),
    rule("dashes"),
    text(LINE_CHECK),
    text("☐ 2116 HDMI 3 М"),
    text("✔ ВЗЯТО 2  ✖ БРАК 1"),
    text("✓ ВЕРНУЛ 1 ✗ НЕТ 1"),
    rule("dots"),
    text("ряд: ёж сода сахар уха"),
    text("← ВОЗВРАТ 21.10"),
    rule("dashdot"),
]


def send(port, payload):
    with serial.Serial(port, 115200, timeout=0.1) as s:
        s.reset_input_buffer()
        for i in range(0, len(payload), 400):        # RAW line stays under 1024 chars
            chunk = payload[i:i + 400]
            s.write(b"RAW " + chunk.hex().upper().encode() + b"\n")
            deadline, buf = time.time() + 3, b""
            while b"OK" not in buf and b"ERR" not in buf and time.time() < deadline:
                buf += s.read(256)
            print(buf.decode(errors="replace").strip())
            if b"ERR" in buf:
                return
        s.write(b"DRAIN\n")
        deadline, buf = time.time() + 60, b""
        while b"OK" not in buf and time.time() < deadline:
            buf += s.read(256)
        print(buf.decode(errors="replace").strip(), f"({len(payload)} bytes)")


if __name__ == "__main__":
    port, what = sys.argv[1], sys.argv[2]
    payload = {"load": define_all, "receipt": lambda: b"".join(RECEIPT),
               "line": lambda: text(LINE_CHECK), "reset": lambda: bytes([NSR])}[what]()
    send(port, payload)
