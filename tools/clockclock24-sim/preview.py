#!/usr/bin/env python3
"""ASCII preview of a wall pose. Cells are [row][col] on the 8x3 wall.

Each mini-clock is drawn as an N x N block with the pivot at the centre and a
full stroke out to the rim for each hand, so strokes that are meant to join
across cell edges visibly join.
"""
import sys, math
P = 225.0  # PARK

def render(wall, sub=5, park_blank=True, rows=3, cols=8):
    h, w = rows * sub, cols * sub
    g = [[' '] * w for _ in range(h)]
    m = sub // 2
    for r in range(rows):
        for c in range(cols):
            a, b = wall[r][c]
            parked = (a == P and b == P)
            cy, cx = r * sub + m, c * sub + m
            for ang in (a, b):
                rad = math.radians(ang - 90)
                dx, dy = math.cos(rad), math.sin(rad)
                ch = '|' if abs(dx) < .35 else ('-' if abs(dy) < .35 else ('\\' if dx*dy > 0 else '/'))
                # A parked cell is not "off" - the hands are physical and still
                # show as one short diagonal. Drawn faint so it reads as unlit.
                if parked and park_blank:
                    ch = '.'
                for s in range(1, m + 1):
                    y, x = round(cy + dy * s), round(cx + dx * s)
                    if 0 <= y < h and 0 <= x < w:
                        g[y][x] = ch if g[y][x] in ' ' else ('+' if g[y][x] != ch else ch)
            g[cy][cx] = '.' if (parked and park_blank) else 'o'
    return "\n".join("".join(r).rstrip() for r in g)

if __name__ == "__main__":
    ns = {'P': P}
    exec(open(sys.argv[1]).read(), ns)
    for name in ns.get('SHOW', []):
        print(f"--- {name} ---")
        print(render(ns[name]))
        print()
