"""Label the B&K-SI socket photo with pin numbers (fig. 4.5, external view)."""
import sys
from PIL import Image, ImageDraw, ImageFont

src, dst = sys.argv[1], sys.argv[2]
im = Image.open(src).convert("RGB").crop((420, 480, 640, 760)).resize((880, 1120), Image.LANCZOS)
k = 4.0  # scale; hole centres measured in the original photo
holes = {  # pin: (x, y) in original photo, label, colour
    1: ((505, 632), "1 EXTC - leave open", (200, 0, 0)),
    2: ((470, 658), "2 HS - leave open", (200, 0, 0)),
    3: ((486, 695), "3 TD → RS232 RXD", (0, 110, 220)),
    4: ((527, 700), "4 SG → GND", (0, 0, 0)),
    5: ((543, 658), "5 RD ← RS232 TXD", (0, 150, 0)),
}
canvas = Image.new("RGB", (1700, 1120), "white")
canvas.paste(im, (0, 0))
d = ImageDraw.Draw(canvas)
font = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Unicode.ttf", 40)
big = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Unicode.ttf", 54)
ty = {1: 300, 2: 440, 5: 580, 3: 720, 4: 860}
for pin, ((x, y), text, col) in holes.items():
    cx, cy = (x - 420) * k, (y - 480) * k
    d.ellipse((cx - 34, cy - 34, cx + 34, cy + 34), outline=col, width=7)
    d.text((cx, cy - 60), str(pin), fill=col, font=big, anchor="mm")
    d.line((cx + 36, cy, 920, ty[pin]), fill=col, width=3)
    d.text((930, ty[pin]), text, fill=col, font=font, anchor="lm")
d.text((930, 120), "External view, red dot on top", fill=(0, 0, 0), font=font, anchor="lm")
d.text((930, 175), "(manual, fig. 4.5)", fill=(90, 90, 90), font=font, anchor="lm")
canvas.save(dst, quality=90)
