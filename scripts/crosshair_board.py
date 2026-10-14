#!/usr/bin/env python3
"""Side by side board of crosshair screenshots.

Each shot of a scene captured by render_regress.py (for example
crosshair_board) becomes a tile: its name, the screen centre at 1:1 over
the map, and the 40x40 centre pixels enlarged six times. "default" comes
first, then the others by name.

usage: crosshair_board.py <capture dir> <scene> <output.png> [--columns N]

Needs Pillow (python3-pil), unlike render_regress.py.
"""

import argparse
import glob
import os

from PIL import Image, ImageDraw, ImageFont

TILE_W, TILE_H = 420, 270


def make_tile(path, name, font):
    im = Image.open(path).convert("RGB")
    cx, cy = im.width // 2, im.height // 2
    real = im.crop((cx - 80, cy - 60, cx + 80, cy + 60))
    zoom = im.crop((cx - 20, cy - 20, cx + 20, cy + 20)).resize((240, 240), Image.NEAREST)
    tile = Image.new("RGB", (TILE_W, TILE_H), (24, 24, 24))
    tile.paste(real, (10, 90))
    tile.paste(zoom, (175, 25))
    draw = ImageDraw.Draw(tile)
    draw.text((10, 6), name, fill=(230, 230, 230), font=font)
    draw.text((10, 74), "1:1", fill=(150, 150, 150), font=font)
    draw.text((375, 255), "x6", fill=(150, 150, 150), font=font)
    return tile


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("captures")
    parser.add_argument("scene")
    parser.add_argument("output")
    parser.add_argument("--columns", type=int, default=2)
    args = parser.parse_args()

    prefix = args.scene + "_"
    shots = {}
    for path in glob.glob(os.path.join(args.captures, prefix + "*.png")):
        shots[os.path.basename(path)[len(prefix):-len(".png")]] = path
    if not shots:
        raise SystemExit(f"no {prefix}*.png in {args.captures}")

    names = sorted(shots, key=lambda n: (n != "default", n))
    font = ImageFont.load_default()
    tiles = [make_tile(shots[n], n, font) for n in names]

    rows = (len(tiles) + args.columns - 1) // args.columns
    board = Image.new("RGB", (args.columns * TILE_W, rows * TILE_H), (10, 10, 10))
    for i, tile in enumerate(tiles):
        board.paste(tile, ((i % args.columns) * TILE_W, (i // args.columns) * TILE_H))
    board.save(args.output)
    print(f"{args.output}: {len(tiles)} crosshairs")


if __name__ == "__main__":
    main()
