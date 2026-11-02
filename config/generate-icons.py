#!/usr/bin/env python3
"""phone-wm App-Icons generieren.

Erzeugt 128x128 PNG-Icons mit Glasmorphismus-Look.
Gerendert wird auf der SSFACH-fachen Aufloesung (Supersampling) und
anschliessend per LANCZOS heruntergerechnet -> weiche Kanten.

Ausgabe: /home/imgsh/phone/wm-vulkan/icons
"""

import math
import os
from PIL import Image, ImageDraw, ImageFilter

SIZE = 128
SS = 4                        # Supersampling-Faktor (512x512 intern)
OUT = os.path.expanduser("~/phone/wm-vulkan/icons")
os.makedirs(OUT, exist_ok=True)


def make_icon(name, bg_color, draw_func):
    R = SIZE * SS
    img = Image.new("RGBA", (R, R), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    r, g, b = bg_color

    # Abgerundete Grundflaeche mit vertikalem Farbverlauf
    radius = 28 * SS
    for y in range(R):
        t = y / R
        cr = int(r * (0.65 + 0.35 * t))
        cg = int(g * (0.65 + 0.35 * t))
        cb = int(b * (0.65 + 0.35 * t))
        draw.line([(0, y), (R - 1, y)], fill=(cr, cg, cb, 255))

    # Weiche Lichtfalle am oberen Rand (Glas-Effekt)
    glass = Image.new("RGBA", (R, R), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glass)
    gd.rounded_rectangle([2 * SS, 2 * SS, R - 2 * SS, R - 2 * SS],
                         radius=radius - 2 * SS, fill=(255, 255, 255, 28))
    glass = glass.filter(ImageFilter.GaussianBlur(3 * SS))
    img.alpha_composite(glass)

    # Abgerundete Maske + Rand
    mask = Image.new("L", (R, R), 0)
    md = ImageDraw.Draw(mask)
    md.rounded_rectangle([0, 0, R - 1, R - 1], radius=radius, fill=255)
    img.putalpha(mask)

    # Subtiler Innenrand
    draw.rounded_rectangle([2, 2, R - 3, R - 3], radius=radius - 2,
                           outline=(255, 255, 255, 36), width=2)

    # App-Symbol zeichnen
    draw_func(img, draw)

    # Herunterskalieren
    img = img.resize((SIZE, SIZE), Image.LANCZOS)
    img.save(f"{OUT}/{name}.png")


def draw_terminal(img, draw):
    s = SS
    cx, cy = SIZE * s // 2, SIZE * s // 2
    # Terminal-Fenster
    draw.rounded_rectangle([30 * s, 30 * s, 98 * s, 98 * s], radius=6 * s,
                           fill=(0, 0, 0, 90),
                           outline=(255, 255, 255, 210), width=2 * s)
    # Titelleiste
    draw.rectangle([32 * s, 30 * s, 96 * s, 44 * s], fill=(255, 255, 255, 26))
    # Punkte in Titelleiste
    for i, c in enumerate([(255, 80, 80), (255, 200, 60), (80, 220, 80)]):
        draw.ellipse([36 * s + i * 14 * s, 35 * s, 44 * s + i * 14 * s, 43 * s],
                     fill=c + (230,))
    # >_
    f = ImageFont.truetype("DejaVuSansMono-Bold", 20 * s) if s > 1 else None
    draw.text((42 * s, 56 * s), ">_", fill=(255, 255, 255, 235), font=f)


def draw_browser(img, draw):
    s = SS
    cx, cy = SIZE * s // 2, SIZE * s // 2
    r = 30 * s
    # Globus
    draw.ellipse([cx - r, cy - r, cx + r, cy + r],
                 outline=(255, 255, 255, 230), width=2 * s)
    # Horizontale Linien (Breitenkreise)
    for dy in [-15 * s, 0, 15 * s]:
        draw.line([(cx - r, cy + dy), (cx + r, cy + dy)],
                  fill=(255, 255, 255, 160), width=1 * s)
    # Vertikale Ellipse (Längenkreis)
    draw.ellipse([cx - 12 * s, cy - r, cx + 12 * s, cy + r],
                 outline=(255, 255, 255, 170), width=1 * s)
    # Zusätzliche Längenkreise
    for off in (-5 * s, 5 * s):
        draw.ellipse([cx - 12 * s + off, cy - r, cx + 12 * s + off, cy + r],
                     outline=(255, 255, 255, 90), width=1 * s)
    # Äquator-Strich durch Mitte
    draw.line([(cx - r, cy), (cx + r, cy)], fill=(255, 255, 255, 230), width=1 * s)


def draw_files(img, draw):
    s = SS
    cx, cy = SIZE * s // 2, SIZE * s // 2
    # Ordner-Hintergrund
    draw.rounded_rectangle([24 * s, 40 * s, 104 * s, 96 * s], radius=5 * s,
                           fill=(255, 255, 255, 46),
                           outline=(255, 255, 255, 190), width=2 * s)
    # Register-Blatt
    draw.polygon([(28 * s, 42 * s), (44 * s, 36 * s), (56 * s, 36 * s)],
                 fill=(255, 255, 255, 80))
    draw.polygon([(44 * s, 36 * s), (56 * s, 36 * s), (56 * s, 42 * s),
                  (44 * s, 42 * s)], fill=(255, 255, 255, 80))
    # Blatt-Linien
    for dy in [54 * s, 64 * s, 74 * s]:
        draw.line([(36 * s, dy), (92 * s, dy)], fill=(255, 255, 255, 140), width=1 * s)
    # Kleines Dokument rechts
    draw.rounded_rectangle([70 * s, 50 * s, 92 * s, 82 * s], radius=3 * s,
                           fill=(255, 255, 255, 90),
                           outline=(255, 255, 255, 180), width=1 * s)


def draw_calculator(img, draw):
    s = SS
    cx, cy = SIZE * s // 2, SIZE * s // 2
    # Gehäuse
    draw.rounded_rectangle([26 * s, 24 * s, 102 * s, 104 * s], radius=7 * s,
                           fill=(0, 0, 0, 70),
                           outline=(255, 255, 255, 190), width=2 * s)
    # Display
    draw.rounded_rectangle([33 * s, 32 * s, 95 * s, 56 * s], radius=4 * s,
                           fill=(0, 0, 0, 120),
                           outline=(255, 255, 255, 150), width=1 * s)
    # Tasten 4x3
    for row in range(4):
        for col in range(3):
            x = 33 * s + col * 22 * s
            y = 62 * s + row * 10 * s
            draw.rounded_rectangle([x, y, x + 18 * s, y + 8 * s], radius=2 * s,
                                   fill=(255, 255, 255, 46),
                                   outline=(255, 255, 255, 120), width=1 * s)
    # Rechte Spalte farbig markieren
    for row in range(4):
        x = 77 * s
        y = 62 * s + row * 10 * s
        draw.rounded_rectangle([x, y, x + 18 * s, y + 8 * s], radius=2 * s,
                               fill=(240, 160, 60, 120), width=0)


def draw_settings(img, draw):
    s = SS
    cx, cy = SIZE * s // 2, SIZE * s // 2
    # Zahnrad
    r_out, r_in = 31 * s, 19 * s
    teeth = 8
    pts = []
    for i in range(teeth * 2):
        angle = i * math.pi / teeth - math.pi / 2
        r = r_out if i % 2 == 0 else r_in
        pts.append((cx + r * math.cos(angle), cy + r * math.sin(angle)))
    draw.polygon(pts, fill=(255, 255, 255, 80),
                 outline=(255, 255, 255, 210), width=2 * s)
    # Innerer Kreis
    draw.ellipse([cx - 10 * s, cy - 10 * s, cx + 10 * s, cy + 10 * s],
                 fill=(60, 60, 60, 200), outline=(255, 255, 255, 210), width=2 * s)
    # Mittelpunkt-Glanz
    draw.ellipse([cx - 4 * s, cy - 4 * s, cx + 4 * s, cy + 4 * s],
                 fill=(255, 255, 255, 130))


def draw_calendar(img, draw):
    s = SS
    cx, cy = SIZE * s // 2, SIZE * s // 2
    # Kalender-Rahmen
    draw.rounded_rectangle([26 * s, 34 * s, 102 * s, 98 * s], radius=6 * s,
                           fill=(255, 255, 255, 30),
                           outline=(255, 255, 255, 200), width=2 * s)
    # Kopf (rot)
    draw.rounded_rectangle([28 * s, 36 * s, 100 * s, 56 * s], radius=4 * s,
                           fill=(220, 60, 60, 210))
    # Halterungen
    for x in [44 * s, 84 * s]:
        draw.line([(x, 26 * s), (x, 40 * s)], fill=(255, 255, 255, 210), width=3 * s)
    # Datum
    draw.text((44 * s, 60 * s), "17", fill=(255, 255, 255, 235))
    # Heutiger Tag hervorgehoben
    draw.rounded_rectangle([58 * s, 72 * s, 84 * s, 88 * s], radius=3 * s,
                           fill=(220, 60, 60, 160))
    # Gitter
    for row in range(2):
        for col in range(4):
            x = 34 * s + col * 17 * s
            y = 78 * s + row * 9 * s
            draw.rounded_rectangle([x, y, x + 12 * s, y + 7 * s], radius=2 * s,
                                   fill=(255, 255, 255, 40))


def draw_music(img, draw):
    s = SS
    cx, cy = SIZE * s // 2, SIZE * s // 2
    # Noten-Kopf 1
    draw.ellipse([cx - 18 * s, cy + 4 * s, cx - 4 * s, cy + 18 * s],
                 fill=(255, 255, 255, 215))
    # Noten-Kopf 2
    draw.ellipse([cx + 10 * s, cy - 4 * s, cx + 24 * s, cy + 10 * s],
                 fill=(255, 255, 255, 215))
    # Hälse
    draw.line([(cx - 4 * s, cy + 11 * s), (cx - 6 * s, cy - 28 * s)],
              fill=(255, 255, 255, 215), width=3 * s)
    draw.line([(cx + 24 * s, cy + 3 * s), (cx + 22 * s, cy - 36 * s)],
              fill=(255, 255, 255, 215), width=3 * s)
    # Banner (Balken)
    draw.polygon([(cx - 6 * s, cy - 28 * s), (cx + 22 * s, cy - 36 * s),
                  (cx + 22 * s, cy - 30 * s), (cx - 6 * s, cy - 22 * s)],
                 fill=(255, 255, 255, 200))


def draw_maps(img, draw):
    s = SS
    cx, cy = SIZE * s // 2, SIZE * s // 2
    # Nadel/Marker mit Schattenwurf
    shadow = Image.new("RGBA", img.size, (0, 0, 0, 0))
    sd = ImageDraw.Draw(shadow)
    sd.ellipse([cx - 12 * s, cy + 12 * s, cx + 12 * s, cy + 20 * s],
               fill=(0, 0, 0, 80))
    shadow = shadow.filter(ImageFilter.GaussianBlur(2 * s))
    img.alpha_composite(shadow)
    # Nadel
    draw.polygon([(cx, cy - 30 * s), (cx - 15 * s, cy + 6 * s),
                  (cx + 15 * s, cy + 6 * s)],
                 fill=(255, 255, 255, 215),
                 outline=(255, 255, 255, 235))
    # Loch in der Mitte
    draw.ellipse([cx - 7 * s, cy - 16 * s, cx + 7 * s, cy - 2 * s],
                 fill=(50, 50, 50, 200))


from PIL import ImageFont

icons = [
    ("terminal",   (26,  26,  26),   draw_terminal),
    ("browser",    (0,   102, 204),  draw_browser),
    ("files",      (51,  153, 51),   draw_files),
    ("calculator", (204, 102, 0),    draw_calculator),
    ("settings",   (153, 153, 153),  draw_settings),
    ("calendar",   (153, 51,  153),  draw_calendar),
    ("music",      (26,  204, 51),   draw_music),
    ("maps",       (0,   153, 102),  draw_maps),
]

for name, color, func in icons:
    make_icon(name, color, func)
    print(f"  {OUT}/{name}.png")

print(f"\n{len(icons)} Icons erzeugt.")
