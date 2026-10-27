"""Glasses, mouth, cheeks and impact marks added to the classic bongo cat
frames (rows of '#' = lit pixel, '.' = unlit).

Everything is placed relative to the cat's left-of-screen eye (found in each
frame), so it follows the head in every pose. Two styles: "nerd" (the owner's
rectangular glasses, default) and "thug" (easter egg).
"""

H = 34          # frame height (the original OLED frames are padded to it)
W = 68


def grid(rows):
    return [[c == "#" for c in r] for r in rows]


def text(g):
    return ["".join("#" if c else "." for c in r) for r in g]


def left_eye(rows):
    """Top-left pixel of the 2x2 eye on the left of the screen"""
    for y in range(8, 20):
        for x in range(20, 35):
            if (rows[y][x:x + 2] == ".." and rows[y + 1][x:x + 2] == ".." and rows[y - 1][x] == "#"
                    and rows[y + 2][x] == "#" and rows[y][x - 1] == "#" and rows[y][x + 2] == "#"):
                return x, y
    raise ValueError("eye not found")


# Impact marks of the tap with the paw on the right of the screen (tap0),
# mirrored next to the paw on the left of the screen (tap1)
MARK_BOXES = [(53, 59, 2, 10), (58, 66, 6, 13), (56, 67, 17, 27)]
OLD_TAP1_MARKS = [(16, 25, 27, 31), (30, 36, 30, 31)]
MIRROR_X, MIRROR_DY = 69, 9


def mirrored_marks(tap0, tap1):
    """tap1 with the small blob under the table replaced by the mirrored "!"
    and streak of tap0 (its triangle does not fit on that side)"""
    src, g = grid(tap0), grid(tap1)
    for x0, x1, y0, y1 in OLD_TAP1_MARKS:
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                g[y][x] = False
    for x0, x1, y0, y1 in MARK_BOXES:
        for y in range(y0, min(y1, 13) + 1):
            for x in range(x0, x1 + 1):
                nx, ny = MIRROR_X - x, y + MIRROR_DY
                if src[y][x] and 0 <= nx < W and 0 <= ny < H:
                    g[ny][nx] = True
    return text(g)


def left_paw_up(rows):
    """Frames where the paw on the left of the screen is raised (prep0, tap0)"""
    return rows[19][22] == "." and rows[20][25] == "."


def face(rows, tapping, style):
    ex, ey = left_eye(rows)
    g = grid(rows)

    def P(dx, dy, lit):
        x, y = ex + dx, ey + dy
        if 0 <= y < H and 0 <= x < W:
            g[y][x] = lit

    def orig(dx, dy):
        return rows[ey + dy][ex + dx] == "#"

    # Original mouth removed
    for dx, dy in ((11, 0), (5, 1), (8, 1), (9, 1), (10, 1), (11, 1), (6, 2), (7, 2)):
        P(dx, dy, True)
    # Cheek spots: 3x1, one pixel away from the glasses
    if not left_paw_up(rows):
        for dx in range(-3, 1):
            for dy in (3, 4):
                if not orig(dx, dy):
                    P(dx, dy, True)
        for dx in (-2, -1, 0):
            P(dx, 4, False)
    if not orig(16, -1) and not orig(16, 0):
        for dx in range(14, 20):
            for dy in (-1, 0):
                P(dx, dy, True)
        for dx in (14, 15, 16):
            P(dx, 0, False)
    # Lenses, 1 px higher after 4 columns (along the face)
    for x0, top0 in ((-3, -2), (10, -5)):
        if style == "nerd":       # rectangular frame, 7 px wide, eyes visible
            for i in range(7):
                x, t = x0 + i, top0 - (i // 4)
                for r in range(5):
                    if i in (0, 6) or r in (0, 4):
                        P(x, t + r, False)
        else:                     # thug: filled, rounded bottom, shine pixels
            for i in range(8):
                x, t = x0 + i, top0 - (i // 4)
                for r in range(5):
                    if (r == 4 and i in (0, 1, 6, 7)) or (r == 3 and i in (0, 7)):
                        continue
                    P(x, t + r, False)
            P(x0 + 1, top0 + 1, True)
            P(x0 + 3, top0 + 1, True)
            P(x0 + 2, top0 + 2, True)
    for dx, dy in ((4, -1), (5, -1), (6, -2), (7, -2), (8, -3), (9, -3)):   # bridge
        P(dx, dy, False)
    for dx, dy in ((-4, 0), (-5, -1), (-6, -1)):                             # temple
        P(dx, dy, False)
    if tapping:   # open Kirby mouth while the paw hits the table
        for dx, dy in ((6, 1), (7, 1), (8, 1), (9, 1), (10, 1), (6, 2), (7, 2), (8, 2), (9, 2), (10, 2),
                       (7, 3), (8, 3), (9, 3)):
            P(dx, dy, False)
    else:         # UwU mouth
        for dx, dy in ((6, 2), (7, 3), (8, 3), (9, 2), (10, 3), (11, 3), (12, 1), (12, 2)):
            P(dx, dy, False)
    return text(g)
