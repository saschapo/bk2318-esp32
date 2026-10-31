"""Build /hex lines for the Russian set (SCS 0x4C) probe. Prints one command per line."""
RU = "ЮАБЦДЕФГХИЙКЛМНОПЯРСТУЖВЬЫЗШЭЩЧ"  # 0x60..0x7E, FrodeM table 0x4C
MAP = {ch: 0x60 + i for i, ch in enumerate(RU)}

def koi7(s):
    return bytes(MAP.get(c, ord(c)) for c in s.upper())

def hx(b):
    return "/hex " + b.hex().upper()

CRLF = b"\r\n"
cmds = [
    hx(b"\x1b(L" + koi7("ПРИВЕТ, 2318! ABC") + CRLF),          # A: Russian in G0
    hx(bytes(range(0x60, 0x7F)) + CRLF),                       # whole Cyrillic row, wraps at 24
    hx(b"\x1b(B"),                                             # G0 back to US-ASCII
    hx(b"\x1b)L" + b"Sony \x0e" + koi7("КАМЕРА") + b"\x0f fx3" + CRLF),  # B: Russian in G1 via SO/SI
    hx(b"\x1f"),                                               # NSR: back to power-on sets (G1 = PDI)
    "ascii ok after reset",
]
print("\n".join(cmds))
