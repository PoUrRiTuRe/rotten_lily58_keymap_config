"""Animated preview of the nice_view_rotten screens (not the real display:
fonts and shapes are approximations of the firmware's LVGL drawing).

Usage: python3 tools/preview.py out.gif   (needs Pillow and DejaVu Sans)
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sprites import build_frames  # noqa: E402

BG, FG, FRAME = (28, 30, 34), (205, 208, 212), (18, 22, 46)
CYAN, DIM = (0, 240, 255), (125, 134, 168)
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
F18, F14, F11 = (ImageFont.truetype(FONT, s) for s in (16, 13, 11))
SPRITES = build_frames()
MS = 50   # one preview frame = one cube frame (CUBE_FRAME_MS)


def status_row(d):
    d.rectangle([0, 2, 28, 13], fill=FG); d.rectangle([1, 3, 27, 12], fill=BG)
    d.rectangle([2, 4, 22, 11], fill=FG); d.rectangle([30, 5, 32, 10], fill=FG)
    d.rectangle([31, 6, 31, 9], fill=BG)
    cx, cy = 59, 13
    for r in (10, 6):
        d.arc([cx - r, cy - r, cx + r, cy + r], 225, 315, fill=FG, width=2)
    d.ellipse([cx - 2, cy - 2, cx + 2, cy + 2], fill=FG)


def left_screen(sprite):
    im = Image.new("RGB", (68, 160), BG); d = ImageDraw.Draw(im)
    status_row(d)
    for y, row in enumerate(SPRITES[sprite]):
        for x, c in enumerate(row):
            if c == "#":
                im.putpixel((x, 26 + y), FG)
    for i, (x, y) in enumerate([(13, 13), (55, 13), (34, 34), (13, 55), (55, 55)]):
        y += 68
        if i == 2:
            d.ellipse([x - 13, y - 13, x + 13, y + 13], outline=FG, width=2)
        elif i < 2:
            for j in range(8):
                d.arc([x - 13, y - 13, x + 13, y + 13], 45 * j + 10, 45 * (j + 1) - 10, fill=FG, width=2)
        if i == 2:
            d.ellipse([x - 9, y - 9, x + 9, y + 9], fill=FG)
        d.text((x, y), str(i + 1), font=F18, fill=BG if i == 2 else FG, anchor="mm")
    d.text((34, 141), "Base", font=F14, fill=FG, anchor="mt")
    return im


V = [(-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1), (-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)]
E = [(0, 1), (1, 2), (2, 3), (3, 0), (4, 5), (5, 6), (6, 7), (7, 4), (0, 4), (1, 5), (2, 6), (3, 7)]


def right_screen(ay, ax):
    im = Image.new("RGB", (68, 160), BG); d = ImageDraw.Draw(im)
    status_row(d)
    a, b = ay * math.pi / 18000, ax * math.pi / 18000
    pts = []
    for x, y, z in V:
        x1 = x * math.cos(a) + z * math.sin(a); z1 = -x * math.sin(a) + z * math.cos(a)
        y2 = y * math.cos(b) - z1 * math.sin(b); z2 = y * math.sin(b) + z1 * math.cos(b)
        f = 88 / (z2 + 5)
        pts.append((34 + round(x1 * f), 55 + 34 + round(y2 * f)))
    for i, j in E:
        d.line([pts[i], pts[j]], fill=FG)
    return im


# Demo: (sprite, duration in ms, caption)
DEMO = [("idle%d" % (i % 5), 200, "idle") for i in range(10)]
for half, paw in (("left", "right"), ("right", "left")):     # mirror
    for top in (True, False):
        cap = f"{half} half · {'top rows' if top else 'bottom rows'}"
        sprite = "tap_%s%s" % (paw, "_far" if top else "")
        for _ in range(4):
            DEMO += [(sprite, 150, cap), ("prep0", 50, cap)]   # lift between presses
DEMO += [("prep0", 900, "paws up"), ("idle0", 200, "idle")]

frames, ay, ax = [], 0, 2860
LOOP = 36000 // 125   # cube frames until both angles are back to the start
timeline = []
for sprite, dur, cap in DEMO:
    timeline += [(sprite, cap)] * (dur // MS)
# The GIF lasts one full cube loop so it repeats without a jump
timeline += [("idle%d" % (i // 4 % 5), "idle") for i in range(LOOP - len(timeline))]
for k in range(LOOP):
    sprite, cap = timeline[k]
    o = Image.new("RGB", (68 * 2 + 30, 214), FRAME)
    o.paste(left_screen(sprite), (10, 10)); o.paste(right_screen(ay, ax), (88, 10))
    d = ImageDraw.Draw(o)
    d.text((83, 184), cap, font=F11, fill=CYAN, anchor="mm")
    d.text((83, 202), "preview, not the real display", font=F11, fill=DIM, anchor="mm")
    frames.append(o.resize((o.width * 3, o.height * 3), Image.NEAREST))
    ay, ax = (ay + 250) % 36000, (ax + 125) % 36000
frames[0].save(sys.argv[1], save_all=True, append_images=frames[1:], duration=MS, loop=0, optimize=True)
print(f"{len(frames)} frames written to {sys.argv[1]}")
