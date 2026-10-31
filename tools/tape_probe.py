# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial>=3.5", "python-barcode>=0.15"]
# ///
"""Probe for a receipt editor: firmware v2 jobs with bytes built on the host.

usage: tape_probe.py list         what each printed line checks, and job sizes
       tape_probe.py dry          every line as hex
       tape_probe.py run [PORT]   print both jobs (JOB BEGIN, RAW, JOB END)

Job 1 — font and text: the printer's character set for the screen font, WIDE, v2 PRINT
and RULE, inverse (text, bold, band, over PDI bar, over a glyph tile, over LLINE), bold
inside a line (Cyrillic, wide, whole line), centring and right alignment by PDI cursor.
Job 2 — graphics: Code 128 with module 1 and 2 dots and 1-3 lines high; dividers from
own 7x7 tiles, PDI bars, dots by DOT, a tear line, titles on dividers.
Lines without a number are named in `list`.
"""
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "host"))
import pdi_probe as P  # noqa: E402

ESC, SI = 0x1B, 0x0F
WIDE, NORMAL = P.WIDE, P.NORMAL
INV_ON, INV_OFF = bytes([ESC, 0x48]), bytes([ESC, 0x49])
CRLF = b"\r\n"

# Divider tiles: one 7x7 own glyph repeated across the line (168 = 24 x 7, no gaps).
# Private-use code points stand for them in the text; indices after the approved set.
TILES = {
    "zigzag":   [".......", ".......", "#.....#", ".#...#.", "..#.#..", "...#...", "......."],
    "wave":     [".......", ".......", ".##....", "#..#..#", "....##.", ".......", "......."],
    "double":   [".......", ".......", "#######", ".......", "#######", ".......", "......."],
    "rail":     [".......", "#######", "#######", ".......", "#######", ".......", "......."],
    "dots":     [".......", ".......", ".......", "...#...", ".......", ".......", "......."],
    "bullets":  [".......", ".......", "..###..", "#######", "..###..", ".......", "......."],
    "diamonds": [".......", "...#...", "..###..", "#######", "..###..", "...#...", "......."],
    "rhombs":   [".......", "...#...", "..#.#..", "##...##", "..#.#..", "...#...", "......."],
    "chevrons": [".......", "##.....", ".##....", "..##...", ".##....", "##.....", "......."],
    "stars":    [".......", "...#...", ".#.#.#.", "..###..", ".#.#.#.", "...#...", "......."],
    "hearts":   [".......", ".......", ".##.##.", ".#####.", "..###..", "...#...", "......."],
    "meander":  [".......", "####...", "#..#...", "#..#...", "#..#...", "#..####", "......."],
    "ruler":    [".......", ".......", ".......", "#......", "#......", "#..#...", "#######"],
    "rope":     [".......", "#######", "..#..#.", ".#..#..", "#..#...", "#######", "......."],
    "scissors": [".......", "##....#", "##..##.", "..##...", "##..##.", "##....#", "......."],
}
TILE_CH = {}
for i, (name, rows) in enumerate(TILES.items()):
    ch = chr(0xE000 + i)
    TILE_CH[name] = ch
    P.GLYPHS[ch] = (0x28 + i, rows)   # P.text() and P.define_all() pick them up


def tile(name, n):
    return TILE_CH[name] * n


def g2():
    """PDI into G2 and locked into GL; Russian stays in G1. SI goes back to G0."""
    return bytes([ESC, 0x2E, 0x57, ESC, 0x6E])


def goto(dot):
    return g2() + P.at(dot) + bytes([SI])


def cells(s):
    return len(s)


def seg_dots(text, wide):
    return cells(text) * 7 * (2 if wide else 1)


def segs(parts, start=0):
    """parts: (text, wide, bold). Prints them from `start`, then bold ones again 1 dot right."""
    out = bytearray(goto(start) if start else b"")
    dot, overlays = start, []
    for text, wide, bold in parts:
        out += (WIDE if wide else b"") + P.text(text) + (NORMAL if wide else b"")
        if bold:
            overlays.append((dot + 1, text, wide))
        dot += seg_dots(text, wide)
    for d, text, wide in overlays:
        out += goto(d) + (WIDE if wide else b"") + P.text(text) + (NORMAL if wide else b"")
    return bytes(out)


def width(parts):
    return sum(seg_dots(t, w) for t, w, _ in parts)


def bar_line(code, start, end):
    """PDI bar from start to end; one bar is at most 1,0 of the scale (162 dots)."""
    out, d = bytearray(g2() + P.at(start)), start
    while d < end:
        n = min(end - d, 160)
        out += P.bar(code, n)
        d += n
    return bytes(out + bytes([SI]))


def runs(cols, start):
    """Columns of 7 rows; white runs become cursor moves, the rest DOT."""
    out = bytearray(g2() + P.at(start))
    i = 0
    while i < len(cols):
        if cols[i] == P.EMPTY:
            j = i
            while j < len(cols) and cols[j] == P.EMPTY:
                j += 1
            n = j - i
            while n > 0:
                k = min(n, 63)
                out += bytes([0x24, 0x40 | k])
                n -= k
            i = j
        else:
            out += P.dots([cols[i]])
            i += 1
    return bytes(out + bytes([SI]))


def barcode(value, module):
    return P.barcode_cols(value, module)


def dotted(start, end, step):
    cols = []
    for d in range(start, end):
        cols.append("...#..." if (d - start) % step == 0 else P.EMPTY)
    return cols


def job1():
    ascii_ = "".join(chr(c) for c in range(0x21, 0x7F))
    ru = "АБВГДЕЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ"
    lines = [
        ("font: ASCII 21-38", P.text(ascii_[0:24])),
        ("font: ASCII 39-50", P.text(ascii_[24:48])),
        ("font: ASCII 51-68", P.text(ascii_[48:72])),
        ("font: ASCII 69-7E", P.text(ascii_[72:])),
        ("font: А..Ч", P.text(ru[:24])),
        ("font: Ш..Я, own glyphs", P.text(ru[24:] + " ✓✗☐…→─")),
        ("07 WIDE doubles columns?", P.text("07 ") + WIDE + P.text("AЖ@y") + NORMAL + P.text(" AЖ@y")),
        ("08 v2 PRINT path", "PRINT 08 ✓✗Ъ☐…→─ ☐ ☐ v2"),
        ("(no number) v2 RULE DASH", "RULE DASH"),
        ("10 inverse text, new paper", P.text("10 ") + INV_ON + P.text("ИНВЕРСИЯ 0125 ✓☐") + INV_OFF),
        ("11 inverse + bold", P.text("11 ") + INV_ON + segs([("ИНВЕРСИЯ ЖИРНЫЙ", False, True)], 21) + INV_OFF),
        ("12 inverse band, title", P.text("12") + INV_ON + P.text(f"{'РАЗДЕЛ 3 ВЕЩИ':^22}") + INV_OFF),
        ("13 inverse over PDI hatch", P.text("13") + INV_ON + bar_line(0x32, 21, 168) + INV_OFF),
        ("14 inverse over zigzag tile", P.text("14 ") + INV_ON + P.text(tile("zigzag", 21)) + INV_OFF),
        ("15 inverse over LLINE", P.text("15") + INV_ON + g2() + bytes([0x28, 0x60 | 0 << 3 | 4, SI]) + INV_OFF),
        ("16 bold word in a line", segs([("16 ☐ 0125 ", False, False), ("ШТАТИВ", False, True),
                                          (" x2", False, False)])),
        ("17 bold wide word", segs([("17 ", False, False), ("КОРПУС", True, True), (" x1", False, False)])),
        ("18 whole line bold", segs([("18 ЖИРНАЯ СТРОКА ✓☐ 0125", False, True)])),
    ]
    centre = [("KIA", True, False), (" SPOT 3 ДНЯ", False, False)]
    lines.append(("(no number) centred: wide KIA + SPOT 3 ДНЯ", segs(centre, (168 - width(centre)) // 2)))
    right = [("ПРАВО ", False, False), ("x4", False, True)]
    lines.append(("20 left label, right part flush right",
                  P.text("20 ЛЕВО") + segs(right, 168 - width(right))))
    return lines


def job2():
    m1 = barcode("0125", 1)
    m2 = barcode("0125", 2)
    left, right = 7, 168 - 7 - len(m1)
    l1, r1 = runs(m1, left), runs(m1, right)
    lines = [
        ("21 label", P.text("21 M1 H1, H2 | H3")),
        ("M1: left H1, right H3 row 1", l1 + r1),
        ("M1: right H3 row 2", r1),
        ("M1: left H2 row 1, right H3 row 3", l1 + r1),
        ("M1: left H2 row 2", l1),
        ("22 label", P.text("22 M2 H1")),
        ("M2 H1", runs(m2, (168 - len(m2)) // 2)),
        ("23 label", P.text("23 M2 H2")),
        ("M2 H2 row 1", runs(m2, (168 - len(m2)) // 2)),
        ("M2 H2 row 2", runs(m2, (168 - len(m2)) // 2)),
    ]
    n = 24
    for name in TILES:
        if name == "scissors":
            continue
        lines.append((f"{n} tile {name}", P.text(f"{n} " + tile(name, 21))))
        n += 1
    lines.append((f"{n} tear line: scissors + PDI dashes",
                  P.text(f"{n}" + TILE_CH["scissors"]) + g2() + P.at(21) + P.line(2, 4, 147) + bytes([SI])))
    n += 1
    for code, kind in [(0x30, "filled wide"), (0x31, "filled narrow"), (0x32, "hatch wide"),
                       (0x33, "hatch narrow"), (0x34, "outline wide"), (0x35, "outline narrow")]:
        lines.append((f"{n} PDI bar {kind}", P.text(f"{n}") + bar_line(code, 21, 168)))
        n += 1
    lines.append((f"{n} DOT dots: step 2 | step 3",
                  P.text(f"{n}") + runs(dotted(21, 91, 2) + [P.EMPTY] * 7 + dotted(98, 168, 3), 21)))
    n += 1
    lines.append(("title on zigzag tiles", P.text(tile("zigzag", 2) + " PUT AWAY " + tile("zigzag", 12))))
    lines.append(("title between double tiles", P.text(tile("double", 2) + " ИТОГО 12 " + tile("double", 12))))
    name = "ОСОБНЯК"
    end = 21 + len(name) * 14 + 7
    lines.append(("hatch + wide title (tape.py ##)", bar_line(0x32, 0, 14) + goto(21) + WIDE + P.text(name)
                  + NORMAL + bar_line(0x32, end, 168)))
    return lines


def run_job(p, lines, feed):
    p.command("JOB BEGIN")
    defs = P.define_all()
    for i in range(0, len(defs), 400):             # RAW line stays under 1024 chars
        p.command("RAW " + defs[i:i + 400].hex().upper())
    for _, body in lines:
        if isinstance(body, str):
            p.command(body)                        # v2 command as is
            continue
        data = body + CRLF
        for i in range(0, len(data), 400):         # RAW line stays under 1024 chars
            p.command("RAW " + data[i:i + 400].hex().upper())
    if feed:
        p.command(f"FEED {feed}")
    return p.command("JOB END", timeout=10)


def main():
    mode = sys.argv[1]
    jobs = [job1(), job2()]
    if mode in ("list", "dry"):
        for j, lines in enumerate(jobs, 1):
            size = len(P.define_all()) + sum(len(b) + 2 if isinstance(b, bytes) else 30 for _, b in lines)
            print(f"== job {j}: {len(lines)} lines, ~{size} bytes")
            for k, (what, body) in enumerate(lines, 1):
                if mode == "list":
                    print(f"{k:3}  {what}")
                else:
                    print(f"{k:3}  {body if isinstance(body, str) else body.hex(' ').upper()}")
        return
    from bk2318 import Printer
    with Printer(sys.argv[2] if len(sys.argv) > 2 else None, on_event=lambda e: print("EVT", e)) as p:
        print("HELLO", p.version)
        print("job 1:", run_job(p, jobs[0], 0))
        print("job 2:", run_job(p, jobs[1], 3))
        p.command("DRAIN", timeout=120)
        print("sent; the printer finishes by eta")


if __name__ == "__main__":
    main()
