#!/usr/bin/env python3
"""Draw Blit's telephone icons (ADR 0196) as SVG into assets/fonts/icons.

Two styles in longwire's four line states -- off (outline), wait (dashed
outline: connecting), on (filled) and ring (filled, with ring marks):

* modern: a handset, one variable-width stroke along an arc;
* classic: a rotary desk phone -- base with the dial cut into it, dial and
  hub, and the handset resting across the top.

Every part is a closed loop that can be inset, so the outline is the band
between a loop and its inset, and the dashes are that band cut along the
loop: all four states share one shape. Every icon's ink is narrower than its
16-unit view box: one whose ink is wider reads to the renderer as more than
one column (ADR 0197), which is how the first classic ring broke. Run it, then build-blit-icons.py.
"""
import math
import sys
from pathlib import Path

OUT = Path(__file__).resolve().parents[2] / 'assets/fonts/icons'
GREY, TEAL, AMBER, GOLD = '#c9d1d9', '#41d6bf', '#f2c14e', '#ffd23f'


def f(v):
    return ('%.2f' % v).rstrip('0').rstrip('.')


def poly(points):
    return 'M' + ' L'.join(f'{f(x)} {f(y)}' for x, y in points) + 'Z'


def band(part, stroke):
    return poly(part(0)) + ' ' + poly(part(stroke)[::-1])


def dashed(part, stroke, dash, gap):
    outer, inner = part(0), part(stroke)
    count, pieces, i = len(outer), [], 0
    while i < count:
        j = min(i + dash, count)
        seg = outer[i:j + 1] if j < count else outer[i:] + outer[:1]
        iseg = inner[i:j + 1] if j < count else inner[i:] + inner[:1]
        pieces.append(poly(seg + iseg[::-1]))
        i += dash + gap
    return ' '.join(pieces)


def even(part, count=96):
    """The same loop at `count` points spaced evenly along its outer edge,
    so dashes cut by point index are even. The places are found once, on
    the outer loop, as fractional positions in the part's own points, and
    every inset is read at those same positions: outer and inset stay matched
    point for point, so a dash never twists."""
    outer = part(0)
    closed = outer + outer[:1]
    lengths = [0.0]
    for (x0, y0), (x1, y1) in zip(closed, closed[1:]):
        lengths.append(lengths[-1] + math.hypot(x1 - x0, y1 - y0))
    places, j = [], 0
    for k in range(count):
        target = lengths[-1] * k / count
        while lengths[j + 1] < target:
            j += 1
        span = lengths[j + 1] - lengths[j] or 1.0
        places.append((j, (target - lengths[j]) / span))

    def loop(shrink):
        pts = part(shrink)
        ring = pts + pts[:1]
        return [(ring[j][0] + (ring[j + 1][0] - ring[j][0]) * t,
                 ring[j][1] + (ring[j + 1][1] - ring[j][1]) * t) for j, t in places]
    return loop


def arc_band(cx, cy, r, width, a0, a1, steps=12):
    outer = [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * k / steps)),
              cy + r * math.sin(math.radians(a0 + (a1 - a0) * k / steps))) for k in range(steps + 1)]
    inner = [(cx + (r - width) * math.cos(math.radians(a0 + (a1 - a0) * k / steps)),
              cy + (r - width) * math.sin(math.radians(a0 + (a1 - a0) * k / steps))) for k in range(steps + 1)]
    return poly(outer + inner[::-1])


# Modern: the handset as shipped in 0.2.44.
def modern():
    cx, cy, radius, a0, a1, stroke = 12.6, 3.4, 8.6, 96, 174, 1.15

    def half_width(u):
        e = max(abs(u - 0.5) * 2 - 0.5, 0) / 0.5
        return 1.2 + 1.75 * (0.5 - 0.5 * math.cos(math.pi * min(e, 1)))

    def centre(u):
        a = math.radians(a0 + (a1 - a0) * u)
        return (cx + radius * math.cos(a), cy + radius * math.sin(a)), a

    def loop(shrink, n=36, capn=10):
        pts = []
        for i in range(n + 1):
            u = i / n
            (x, y), a = centre(u)
            w = half_width(u) - shrink
            pts.append((x + w * math.cos(a), y + w * math.sin(a)))
        (x, y), a = centre(1)
        w = half_width(1) - shrink
        for k in range(1, capn):
            ang = a + math.pi * k / capn
            pts.append((x + w * math.cos(ang), y + w * math.sin(ang)))
        for i in range(n, -1, -1):
            u = i / n
            (x, y), a = centre(u)
            w = half_width(u) - shrink
            pts.append((x - w * math.cos(a), y - w * math.sin(a)))
        (x, y), a = centre(0)
        w = half_width(0) - shrink
        for k in range(1, capn):
            ang = a + math.pi + math.pi * k / capn
            pts.append((x + w * math.cos(ang), y + w * math.sin(ang)))
        return pts

    filled = poly(loop(0))
    waves = arc_band(9.2, 6.8, 4.4, 1.3, -85, -5) + ' ' + arc_band(9.2, 6.8, 6.9, 1.3, -85, -5)
    return {
        'phone-off': ('Telephone handset, outline: the line is off', [(GREY, band(loop, stroke))]),
        'phone-wait': ('Telephone handset, dashed outline: connecting', [(AMBER, dashed(loop, stroke, 4, 3))]),
        'phone-on': ('Telephone handset, filled: on, with a number', [(TEAL, filled)]),
        'phone-ring': ('Telephone handset, filled, with ring waves: a call', [(TEAL, filled), (GOLD, waves)]),
    }


# Classic: a rotary desk phone, drawn bold enough to read at ten pixels.
def classic():
    stroke = 1.45

    def base(shrink):
        x0, y0, x1, y1, r, taper = 3.6, 8.2, 12.4, 15.4, 1.6, 0.9
        a, b, c, d = x0 + shrink, y0 + shrink, x1 - shrink, y1 - shrink
        rr = max(r - shrink, 0.2)
        pts = []
        for ccx, ccy, s0, s1 in ((c - rr, b + rr, -90, 0), (c + taper - rr, d - rr, 0, 90),
                                 (a - taper + rr, d - rr, 90, 180), (a + rr, b + rr, 180, 270)):
            for k in range(7):
                t = math.radians(s0 + (s1 - s0) * k / 6)
                pts.append((ccx + rr * math.cos(t), ccy + rr * math.sin(t)))
        return pts

    def circle(ccx, ccy, r):
        return lambda shrink: [(ccx + (r - shrink) * math.cos(2 * math.pi * k / 40),
                                ccy + (r - shrink) * math.sin(2 * math.pi * k / 40)) for k in range(40)]

    dial, hub = circle(8, 11.9, 2.9), circle(8, 11.9, 1.05)

    def handset(shrink):
        x0, x1, top, lift, bar, cupw, cuph = 1.0, 15.0, 3.0, 1.1, 2.6, 3.6, 4.6
        pts, n = [], 20

        def arch(u):
            return math.sin(math.pi * u) * lift

        left, right = x0 + shrink, x1 - shrink
        for i in range(n + 1):
            u = i / n
            pts.append((left + (right - left) * u, top - arch(u) + shrink))
        rc, cy = cupw / 2 - shrink, top + cuph - cupw / 2
        for k in range(13):
            t = math.radians(180 * k / 12)
            pts.append((x1 - cupw / 2 + rc * math.cos(t), cy + rc * math.sin(t)))
        under = top + bar - shrink
        for i in range(n, -1, -1):
            x = (x1 - cupw + shrink - (x0 + cupw - shrink)) * (i / n) + (x0 + cupw - shrink)
            pts.append((x, under - arch((x - x0) / (x1 - x0))))
        for k in range(13):
            t = math.radians(180 * k / 12)
            pts.append((x0 + cupw / 2 + rc * math.cos(t), cy + rc * math.sin(t)))
        return pts

    filled = ' '.join([poly(base(0)), poly(dial(0)[::-1]), poly(hub(0)), poly(handset(0))])
    outline = ' '.join([band(base, stroke), band(dial, stroke), poly(hub(0)), band(handset, stroke)])
    dash = ' '.join([dashed(even(base, 36), stroke, 4, 2), dashed(even(dial, 16), stroke, 3, 1),
                     poly(hub(0)), dashed(even(handset, 42), stroke, 4, 2)])
    # Ring marks either side of the base, inside the view box.
    marks = ' '.join(arc_band(8, 11.8, r, 1.15, a0, a1)
                     for r in (6.2, 7.5) for a0, a1 in ((152, 208), (-28, 28)))
    return {
        'phone-classic-off': ('Classic desk telephone, outline: the line is off', [(GREY, outline)]),
        'phone-classic-wait': ('Classic desk telephone, dashed outline: connecting', [(AMBER, dash)]),
        'phone-classic-on': ('Classic desk telephone, filled: on, with a number', [(TEAL, filled)]),
        'phone-classic-ring': ('Classic desk telephone, filled, with ring marks: a call',
                               [(TEAL, filled), (GOLD, marks)]),
    }


def inside(paths):
    """The ink's extent fits the view box: what the renderer measures to
    tell a one-column icon from a wider one (ADR 0197)."""
    import re
    numbers = [float(v) for _, d in paths for v in re.findall(r'-?[0-9.]+', d)]
    xs, ys = numbers[0::2], numbers[1::2]
    return max(xs) - min(xs) <= 15.5 and max(ys) - min(ys) <= 16


def main():
    for name, (title, paths) in {**modern(), **classic()}.items():
        if not inside(paths):
            sys.exit(f'{name}: ink wider than the view box')
        body = '\n'.join(f'  <path fill="{colour}" d="{d}"/>' for colour, d in paths)
        (OUT / f'{name}.svg').write_text(
            f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16">\n'
            f'  <title>{title}</title>\n{body}\n</svg>\n')
    print('PHONE_ICONS_DRAWN modern=4 classic=4 one_column=yes')


if __name__ == '__main__':
    main()
