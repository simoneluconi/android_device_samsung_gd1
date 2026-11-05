#!/usr/bin/env python3
"""Boot animation: autofocus brackets locking onto a focus point.

Four white corner brackets close in on a thin circle with a centre dot, flash
GC Camera's blue as focus locks, then a thin arc turns around the circle
until boot completes. The picture is symmetric, so it reads the same with the
camera held in landscape (the display's boot orientation) or in portrait.

Usage: make_bootanimation.py OUT.zip   (needs Pillow)
"""
import io
import math
import sys
import zipfile

from PIL import Image, ImageDraw

SIZE, SS, FPS = 600, 2, 30   # frame size, supersampling, frame rate
N_OPEN, N_LOOP = 60, 60      # part0 (played once), part1 (loop)
S = SIZE * SS
C = S / 2
WHITE = (240, 240, 245)
BLUE = (37, 99, 235)         # GC Camera's icon blue, #2563EB


def ease_in_out(t):
    return 4 * t ** 3 if t < 0.5 else 1 - (-2 * t + 2) ** 3 / 2


def ease_out(t):
    return 1 - (1 - t) ** 3


def ease_back(t, s=1.4):
    t -= 1
    return t * t * ((s + 1) * t + s) + 1


def clamp(t):
    return max(0.0, min(1.0, t))


def mix(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def brackets(d, half, length, width, color):
    for sx in (-1, 1):
        for sy in (-1, 1):
            x, y = C + sx * half, C + sy * half
            d.line([(x, y), (x - sx * length, y)], fill=color, width=width)
            d.line([(x, y), (x, y - sy * length)], fill=color, width=width)
            r = width / 2
            d.ellipse([x - r, y - r, x + r, y + r], fill=color)


def frame(t_open=None, t_loop=None):
    """One frame: t_open in 0..1 for part0, or t_loop in 0..1 for part1."""
    img = Image.new("RGB", (S, S), (0, 0, 0))
    d = ImageDraw.Draw(img)
    r_focus = 0.155 * S
    r_arc = 0.2 * S
    arc_width = int(0.008 * S)
    if t_open is not None:
        t = t_open
        half = 0.42 * S - 0.17 * S * ease_in_out(clamp(t / 0.55))
        circle_r = r_focus * ease_back(clamp((t - 0.3) / 0.35)) if t > 0.3 else 0
        flash = math.sin(math.pi * clamp((t - 0.68) / 0.2)) if t > 0.68 else 0
        color = mix((0, 0, 0), mix(WHITE, BLUE, flash), clamp(t / 0.15))
        # the loop's arc grows in, ending where part1 starts
        arc = (-90, -90 + 70 * ease_out(clamp((t - 0.75) / 0.25))) if t > 0.75 else None
    else:
        half = 0.25 * S
        circle_r = r_focus
        color = WHITE
        start = math.degrees(2 * math.pi * t_loop) - 90
        arc = (start, start + 70)
    if circle_r > 0:
        d.ellipse([C - circle_r, C - circle_r, C + circle_r, C + circle_r],
                  outline=color, width=int(0.008 * S))
        dot = 0.016 * S * min(1.0, circle_r / r_focus)
        d.ellipse([C - dot, C - dot, C + dot, C + dot], fill=color)
    brackets(d, half, 0.07 * S, int(0.012 * S), color)
    if arc is not None:
        d.arc([C - r_arc, C - r_arc, C + r_arc, C + r_arc], arc[0], arc[1],
              fill=WHITE, width=arc_width)
    return img.resize((SIZE, SIZE), Image.LANCZOS)


def png(img):
    buf = io.BytesIO()
    img.save(buf, "PNG", optimize=True)
    return buf.getvalue()


def main():
    # The boot animation must be stored, not compressed
    with zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_STORED) as z:
        z.writestr("desc.txt", "%d %d %d\np 1 0 part0\np 0 0 part1\n" % (SIZE, SIZE, FPS))
        for i in range(N_OPEN):
            z.writestr("part0/%03d.png" % i, png(frame(t_open=i / (N_OPEN - 1))))
        for i in range(N_LOOP):
            z.writestr("part1/%03d.png" % i, png(frame(t_loop=i / N_LOOP)))


if __name__ == "__main__":
    main()
