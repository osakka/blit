#!/usr/bin/env python3
"""Draw Blit's player icons (ADR 0237) as SVG into assets/fonts/icons.

Two families, from the owner-approved preview (2026-11-19):

* transport -- play, pause, stop, prev, next, rewind, forward, eject, record,
  repeat, repeat-one, shuffle -- one drawing each in a square view box, in
  the text colour (the catalogue marks them colour: false). The same drawing
  is the one-cell glyph and, larger, the two-cell glyph;
* now playing -- a CD with a turning sheen, a record with a turning
  highlight, a cassette with turning reels -- eight frames each, in their
  own colours, two cells wide.

Every shape is a closed filled outline: a stroke is drawn as the band
around its centre line, a ring as an outer loop and an inner loop wound
the other way (a hole), so the font's nonzero fill draws exactly this.
Deterministic: run it, then build-blit-icons.py.
"""
import math
from pathlib import Path

OUT = Path(__file__).resolve().parents[2] / 'assets/fonts/icons'
INK = '#d9dde3'      # the transport icons' fill; they are drawn in the text colour


def f(v):
    return ('%.2f' % v).rstrip('0').rstrip('.') or '0'


def area(points):
    return sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(points, points[1:] + points[:1])) / 2


def poly(points, clockwise=True):
    """A closed loop, wound one way (SVG y down: positive area is clockwise)."""
    points = list(points)
    if (area(points) > 0) != clockwise:
        points.reverse()
    return 'M' + ' L'.join(f'{f(x)} {f(y)}' for x, y in points) + 'Z'


def circle(cx, cy, r, clockwise=True, n=48):
    return poly([(cx + r * math.cos(2 * math.pi * k / n), cy + r * math.sin(2 * math.pi * k / n))
                 for k in range(n)], clockwise)


def ring(cx, cy, r_out, r_in, n=48):
    return circle(cx, cy, r_out, True, n) + ' ' + circle(cx, cy, r_in, False, n)


def sector(cx, cy, r_out, r_in, a0, a1, n=12):
    """An annular sector from angle a0 to a1 (radians)."""
    outer = [(cx + r_out * math.cos(a0 + (a1 - a0) * k / n), cy + r_out * math.sin(a0 + (a1 - a0) * k / n))
             for k in range(n + 1)]
    inner = [(cx + r_in * math.cos(a1 - (a1 - a0) * k / n), cy + r_in * math.sin(a1 - (a1 - a0) * k / n))
             for k in range(n + 1)]
    return poly(outer + inner)


def rect(x, y, w, h):
    return poly([(x, y), (x + w, y), (x + w, y + h), (x, y + h)])


def tri(x, y, w, h, right):
    if right:
        return poly([(x, y), (x + w, y + h / 2), (x, y + h)])
    return poly([(x + w, y), (x, y + h / 2), (x + w, y + h)])


def stroke_curve(points, half):
    """The band of half-width `half` around a polyline."""
    left, right = [], []
    for i, (x, y) in enumerate(points):
        a = points[max(i - 1, 0)]
        b = points[min(i + 1, len(points) - 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy) or 1
        nx, ny = -dy / length * half, dx / length * half
        left.append((x + nx, y + ny))
        right.append((x - nx, y - ny))
    return poly(left + right[::-1])


def bezier(p0, p1, p2, p3, n=24):
    out = []
    for k in range(n + 1):
        t = k / n
        u = 1 - t
        out.append(tuple(u ** 3 * a + 3 * u * u * t * b + 3 * u * t * t * c + t ** 3 * d
                         for a, b, c, d in zip(p0, p1, p2, p3)))
    return out


def svg(view, title, paths):
    body = '\n'.join(f'  <path fill="{fill}" d="{d}"/>' for fill, d in paths)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{view}">\n  <title>{title}</title>\n'
            f'{body}\n</svg>\n')


# ---- transport: a 24-unit square, the shape in its middle 15.4 units (the preview's 18% margin)
S = 24.0
P = S * .18
Q = S - 2 * P


def repeat_arc(one):
    cx = cy = S / 2
    r, half = Q * .42, max(1.5, Q * .42 / 4) / 2
    a0, a1 = math.pi * .15, math.pi * 1.85
    shapes = [sector(cx, cy, r + half, r - half, a0, a1, 32)]
    ax, ay = cx + r * math.cos(a0), cy + r * math.sin(a0)
    shapes.append(poly([(ax - r * .55, ay - r * .05), (ax + r * .45, ay - r * .2), (ax + r * .05, ay + r * .6)]))
    if one:
        # A "1": its stem and its flag, inside the ring.
        h = r * 1.0
        stem = r * .22
        shapes.append(rect(cx - stem / 2, cy - h / 2, stem, h))
        shapes.append(poly([(cx - stem / 2, cy - h / 2), (cx - stem / 2 - r * .3, cy - h / 2 + r * .25),
                            (cx - stem / 2 - r * .3, cy - h / 2 + r * .45), (cx - stem / 2, cy - h / 2 + r * .2)]))
    return shapes


def shuffle():
    x, y, w, h = P, P, Q, Q
    half = max(1.5, h / 9) / 2
    a = bezier((x, y + h * .2), (x + w * .5, y + h * .2), (x + w * .45, y + h * .8), (x + w * .8, y + h * .8))
    b = bezier((x, y + h * .8), (x + w * .5, y + h * .8), (x + w * .45, y + h * .2), (x + w * .8, y + h * .2))
    return [stroke_curve(a, half), stroke_curve(b, half),
            tri(x + w * .78, y + h * .05, w * .22, h * .3, True), tri(x + w * .78, y + h * .65, w * .22, h * .3, True)]


TRANSPORT = {
    'play': lambda: [tri(P * 1.2, P, Q * .85, Q, True)],
    'pause': lambda: [rect(P, P, Q * .32, Q), rect(P + Q * .68, P, Q * .32, Q)],
    'stop': lambda: [rect(P, P, Q, Q)],
    'prev': lambda: [rect(P, P, Q * .16, Q), tri(P + Q * .16, P, Q * .84, Q, False)],
    'next': lambda: [tri(P, P, Q * .84, Q, True), rect(P + Q * .84, P, Q * .16, Q)],
    'rewind': lambda: [tri(P, P, Q / 2, Q, False), tri(P + Q / 2, P, Q / 2, Q, False)],
    'forward': lambda: [tri(P, P, Q / 2, Q, True), tri(P + Q / 2, P, Q / 2, Q, True)],
    'eject': lambda: [poly([(P, P + Q * .62), (S / 2, P), (S - P, P + Q * .62)]), rect(P, P + Q * .78, Q, Q * .22)],
    'record': lambda: [circle(S / 2, S / 2, Q / 2)],
    'repeat': lambda: repeat_arc(False),
    'repeat-one': lambda: repeat_arc(True),
    'shuffle': shuffle,
}
TITLES = {'play': 'Play', 'pause': 'Pause', 'stop': 'Stop', 'prev': 'Previous track', 'next': 'Next track',
          'rewind': 'Rewind', 'forward': 'Fast forward', 'eject': 'Eject', 'record': 'Record',
          'repeat': 'Repeat', 'repeat-one': 'Repeat one', 'shuffle': 'Shuffle'}


# ---- now playing: a 24 x 24 box (two cells wide, one tall), eight frames a turn
SHEEN = ['#c9d6e2', '#f2a6ff', '#9de8ff', '#c7ffad', '#ffe49a', '#c9d6e2']


def mix(a, b, t):
    ca = [int(a[i:i + 2], 16) for i in (1, 3, 5)]
    cb = [int(b[i:i + 2], 16) for i in (1, 3, 5)]
    return '#' + ''.join('%02x' % round(x + (y - x) * t) for x, y in zip(ca, cb))


def cd(frame):
    cx = cy = 12.0
    r = 24 * .45
    paths, wedges = [], 20
    for k in range(wedges):
        t = k / wedges
        pos = t * (len(SHEEN) - 1)
        colour = mix(SHEEN[int(pos)], SHEEN[min(int(pos) + 1, len(SHEEN) - 1)], pos - int(pos))
        a0 = frame * math.pi / 4 + 2 * math.pi * k / wedges
        paths.append((colour, sector(cx, cy, r, r * .32, a0, a0 + 2 * math.pi / wedges + .002, 3)))
    paths.append(('#6c7682', ring(cx, cy, r * .32, r * .12)))
    return paths


def vinyl(frame):
    cx = cy = 12.0
    r = 24 * .46
    a = frame * math.pi / 4
    paths = [('#111111', ring(cx, cy, r, r * .34))]
    for k in (.86, .72, .58):
        paths.append(('#2b2b2b', ring(cx, cy, r * k + .4, r * k - .4)))
    paths.append(('#ffffff8c', sector(cx, cy, r * .8 + .6, r * .8 - .6, a, a + .7, 8)))
    paths.append(('#e0533d', circle(cx, cy, r * .34)))
    paths.append(('#f5d6a0', circle(cx + math.cos(a) * r * .18, cy + math.sin(a) * r * .18, r * .07, True, 16)))
    return paths


def cassette(frame):
    w, h = 24 * .92, 24 * .62
    ox, oy = (24 - w) / 2, (24 - h) / 2
    paths = [('#4a4f58', rect(ox, oy, w, h)),
             ('#e7d6a8', rect(ox + w * .08, oy + h * .12, w * .84, h * .5)),
             ('#2b2e33', rect(ox + w * .22, oy + h * .7, w * .56, h * .2))]
    for k in (.32, .68):
        rx, ry, rr = ox + w * k, oy + h * .37, h * .17
        paths.append(('#1b1d20', circle(rx, ry, rr, True, 24)))
        for s in range(3):
            a = frame * math.pi / 12 + s * 2 * math.pi / 3
            dx, dy = math.cos(a), math.sin(a)
            nx, ny = -dy * .35, dx * .35
            paths.append(('#e7d6a8', poly([(rx + nx, ry + ny), (rx + dx * rr + nx, ry + dy * rr + ny),
                                           (rx + dx * rr - nx, ry + dy * rr - ny), (rx - nx, ry - ny)])))
    return paths


DISCS = {'disc': ('CD', cd), 'vinyl': ('Record', vinyl), 'cassette': ('Cassette', cassette)}


def main():
    written = []
    for name, shapes in TRANSPORT.items():
        path = OUT / f'transport-{name}.svg'
        path.write_text(svg('0 0 24 24', f'{TITLES[name]} (transport)', [(INK, d) for d in shapes()]))
        written.append(path.name)
    for name, (title, draw) in DISCS.items():
        for frame in range(8):
            path = OUT / f'{name}-{frame}.svg'
            path.write_text(svg('0 0 24 24', f'{title}, frame {frame} of 8', draw(frame)))
            written.append(path.name)
    print(f'PLAYER_ICONS_DRAWN {len(written)} files')


if __name__ == '__main__':
    main()
