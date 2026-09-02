#!/usr/bin/env python3
"""Packs the four full-screen backgrounds into one APNG.

They are all 320x200, none of them uses alpha, and between them they use
74 colours - so one indexed frame format holds all four exactly, and the
result is smaller than the four PNGs it replaces. Converting them to
RGBA instead nearly doubles the size, because two of the four are
indexed today.

Frame order is the order the .game file refers to them by:
    0 game_bg, 1 end_bg, 2 menu_bg, 3 title

usage: tests/make-bg-apng.py dinothawr/assets
"""
import os
import sys

from PIL import Image

ORDER = ["bg.png", "ending.png", "level_select_bg.png", "titlescreen.png"]


def main(assets):
    rgb = [Image.open(os.path.join(assets, p)).convert("RGB") for p in ORDER]

    if len({im.size for im in rgb}) != 1:
        sys.exit("frames must all be the same size: %s"
                 % [im.size for im in rgb])

    colours = sorted({px for im in rgb for px in im.getdata()})
    if len(colours) > 256:
        sys.exit("%d colours between them; too many for one palette"
                 % len(colours))

    palette = [c for colour in colours for c in colour]
    palette += [0] * (768 - len(palette))
    index = {c: i for i, c in enumerate(colours)}

    frames = []
    for im in rgb:
        p = Image.new("P", im.size)
        p.putpalette(palette)
        p.putdata([index[px] for px in im.getdata()])
        assert list(p.convert("RGB").getdata()) == list(im.getdata())
        frames.append(p)

    out = os.path.join(assets, "bg.apng")
    frames[0].save(out, save_all=True, append_images=frames[1:],
                   duration=1000, loop=0)

    was = sum(os.path.getsize(os.path.join(assets, p)) for p in ORDER)
    now = os.path.getsize(out)
    print("%s: %d B, from %d B of PNG (%.0f%% smaller), %d colours"
          % (out, now, was, 100 * (was - now) / was, len(colours)))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "dinothawr/assets")
