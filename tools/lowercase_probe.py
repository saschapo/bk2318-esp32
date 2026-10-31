# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial>=3.5", "python-barcode>=0.15"]
# ///
"""Probe: lowercase Cyrillic as own glyphs (DRC 0x37-0x50).

usage: lowercase_probe.py list         what each printed line checks, and the job size
       lowercase_probe.py dry          every line as hex
       lowercase_probe.py run [PORT]   print the job (JOB BEGIN, RAW, JOB END)

One job, loaded the way a client would load it: 7 approved glyphs, 15 divider tiles and
26 lowercase letters (48 definitions). а и о р с у х print as ROM Latin a u o p c y x
(glyphs/drc_lowercase.txt). Checks: the alphabet, text, mixed with uppercase and Latin,
WIDE, bold, inverse, look-alikes, that the tiles survive the extra definitions, index 0x7E.
"""
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "host"))
import pdi_probe as P  # noqa: E402
import tape_probe as T  # noqa: E402  (puts the 15 tiles at 0x28-0x36 into P.GLYPHS)

TWINS = {}  # lowercase letter -> ROM Latin letter with the same dots
lines = (HERE.parent / "glyphs" / "drc_lowercase.txt").read_text().splitlines()
for i, ln in enumerate(lines):
    parts = ln.split()
    if len(parts) < 4 or parts[0] != "#" or not parts[2].startswith("U+"):
        continue
    if parts[3] == "ROM":
        TWINS[parts[1]] = parts[4]
    elif parts[3].startswith("0x"):
        P.GLYPHS[parts[1]] = (int(parts[3], 16), lines[i + 1:i + 8])
TOP = ""  # ф again, at the last index
P.GLYPHS[TOP] = (0x7E, P.GLYPHS["ф"][1])

ESC, SI = 0x1B, 0x0F
WIDE, NORMAL, INV_ON, INV_OFF = T.WIDE, T.NORMAL, T.INV_ON, T.INV_OFF


def t(s):
    return P.text("".join(TWINS.get(ch, ch) for ch in s))


def segs(parts, start=0):
    return T.segs([("".join(TWINS.get(ch, ch) for ch in s), w, b) for s, w, b in parts], start)


def job():
    abc = "абвгдеёжзийклмнопрстуфхцчшщъыьэюя"
    return [
        ("01 alphabet а..р", t("01 " + abc[:18])),
        ("02 alphabet с..я", t("02 " + abc[18:])),
        ("03 pangram 1", t("03 съешь же ещё этих")),
        ("04 pangram 2", t("04 мягких французских")),
        ("05 pangram 3", t("05 булок, да выпей чаю")),
        ("06 storage line", t("06 Шкаф 1, полка 2")),
        ("07 with Latin lowercase", t("07 Камера Sony fx3 ✓")),
        ("08 Ё → Е, Ъ and ъ", t("08 Ёлка, ёж, СЪЁМКА, съём")),
        ("09 WIDE", t("09 ") + WIDE + t("щука, Жёлудь") + NORMAL),
        ("10 bold word (second pass +1 dot)", segs([("10 ", False, False), ("штатив", False, True),
                                                    (" и ", False, False), ("корпус", False, True)])),
        ("11 inverse", t("11 ") + INV_ON + t("инверсия, ящик 4") + INV_OFF),
        ("12 look-alikes е e ё | и u й | п n | к k", t("12 еeё иuй пn кk тm")),
        ("13 tiles still intact (zigzag)", P.text("13 " + T.tile("zigzag", 21))),
        ("14 index 0x7E (ф)", t("14 ф=") + bytes([P.SS3, 0x7E])),
    ]


def defs():
    return P.define_all()


def main():
    mode = sys.argv[1]
    body = job()
    if mode in ("list", "dry"):
        size = len(defs()) + sum(len(b) + 2 for _, b in body)
        print(f"== {len(body)} lines; definitions {len(defs())} bytes ({len(P.GLYPHS)} glyphs), total ~{size} bytes")
        for k, (what, b) in enumerate(body, 1):
            print(f"{k:3}  {what if mode == 'list' else b.hex(' ').upper()}")
        return
    from bk2318 import Printer
    with Printer(sys.argv[2] if len(sys.argv) > 2 else None, on_event=lambda e: print("EVT", e)) as p:
        print("HELLO", p.version)
        print("job:", T.run_job(p, body, 3))
        p.command("DRAIN", timeout=120)
        print("sent; the printer finishes by eta")


if __name__ == "__main__":
    main()
