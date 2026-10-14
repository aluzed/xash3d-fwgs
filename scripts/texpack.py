#!/usr/bin/env python3
"""Material pack builder: game textures to .mat files with KTX2 maps.

For each texture a map uses (embedded in the BSP or found in its WADs):

  1. extract     the 8-bit texture to RGBA (palette, '{' transparency)
  2. derive      a normal map and a roughness / metalness / occlusion map
                 from the luminance (heuristics, a starting point to edit)
  3. upscale     to --size: Lanczos by default, or any command given with
                 --upscaler "cmd {input} {output}" (e.g. a Real-ESRGAN
                 class model); the derived maps follow with Lanczos
  4. compress    with ktx2enc (utils/ktx2enc): BC7 albedo and masks, BC5
                 normals, every mip level
  5. write       <out>/materials/maps/<map>.bsp/<texture>.mat

A manifest (<out>/texpack.json) keeps a hash of each texture and of the
settings: a second run only redoes what changed.

usage: texpack.py --game-dir STEAM/Half-Life --game cstrike --map de_dust2 \\
                  --out ~/xash-dev/amd64/cstrike --size 4096 --ktx2enc PATH

Packs made from Valve's textures are for personal use only: never commit
them nor share them. The tool only reads the game directory.

Needs Pillow (python3-pil).
"""

import argparse
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile

from PIL import Image, ImageFilter

# tool textures: never drawn, or drawn by special code
SKIPPED = {"aaatrigger", "clip", "null", "sky", "origin", "hint", "skip", "bevel", "black", "white"}


# --------------------------------------------------------------------------
# Extraction: BSP version 30 textures and WAD3 lumps
# --------------------------------------------------------------------------

def miptex_to_image(data, ofs):
    """A miptex at ofs: (name, RGBA image), None when it has no pixels."""
    name = data[ofs:ofs + 16].split(b"\0")[0].decode("latin1")
    width, height = struct.unpack_from("<II", data, ofs + 16)
    pix_ofs, = struct.unpack_from("<I", data, ofs + 24)
    if not pix_ofs:
        return name, None
    count = width * height
    pixels = data[ofs + pix_ofs: ofs + pix_ofs + count]
    pal_ofs = ofs + pix_ofs + count + count // 4 + count // 16 + count // 64 + 2
    palette = data[pal_ofs: pal_ofs + 768]
    img = Image.frombytes("P", (width, height), pixels)
    img.putpalette(palette)
    img = img.convert("RGBA")
    if name.startswith("{"):
        # the last palette entry is transparent
        alpha = Image.frombytes("L", (width, height), bytes(0 if p == 255 else 255 for p in pixels))
        img.putalpha(alpha)
    return name, img


def bsp_textures(path):
    """{lower name: (name, image or None)} and the WAD names of a BSP."""
    data = open(path, "rb").read()
    tex_ofs, _ = struct.unpack_from("<ii", data, 4 + 2 * 8)
    count, = struct.unpack_from("<i", data, tex_ofs)
    offsets = struct.unpack_from("<%di" % count, data, tex_ofs + 4)
    textures = {}
    for o in offsets:
        if o >= 0:
            name, img = miptex_to_image(data, tex_ofs + o)
            textures[name.lower()] = (name, img)
    ent_ofs, ent_len = struct.unpack_from("<ii", data, 4)
    entities = data[ent_ofs:ent_ofs + ent_len].decode("latin1")
    wads = []
    for line in entities.splitlines():
        if line.startswith('"wad"'):
            value = line.split('"')[3]
            wads = [os.path.basename(w.replace("\\", "/")) for w in value.split(";") if w]
            break
    return textures, wads


def wad_textures(path):
    data = open(path, "rb").read()
    _, count, dir_ofs = struct.unpack_from("<4sii", data, 0)
    lumps = {}
    for i in range(count):
        filepos, _, _, _, _, _, _, name = struct.unpack_from("<iiibbbb16s", data, dir_ofs + i * 32)
        lumps[name.split(b"\0")[0].decode("latin1").lower()] = filepos
    return data, lumps


def find_file(game_dir, game, name):
    for d in (game, "valve"):
        p = os.path.join(game_dir, d, name)
        if os.path.isfile(p):
            return p
    return None


# --------------------------------------------------------------------------
# Derived maps
# --------------------------------------------------------------------------

def derive_maps(albedo, strength=3.0):
    """Normal map (green up) and roughness / metalness / occlusion from the
    luminance, wrapped like a tiling texture."""
    lum = albedo.convert("L").filter(ImageFilter.GaussianBlur(0.8))
    w, h = lum.size
    px = lum.load()
    normal = Image.new("RGB", (w, h))
    rma = Image.new("RGB", (w, h))
    n_px, r_px = normal.load(), rma.load()
    for y in range(h):
        for x in range(w):
            dx = (px[(x + 1) % w, y] - px[(x - 1) % w, y]) / 255.0 * strength
            dy = (px[x, (y + 1) % h] - px[x, (y - 1) % h]) / 255.0 * strength
            nx, ny, nz = -dx, dy, 1.0
            length = (nx * nx + ny * ny + nz * nz) ** 0.5
            n_px[x, y] = tuple(int((c / length * 0.5 + 0.5) * 255) for c in (nx, ny, nz))
            v = px[x, y] / 255.0
            # brighter is smoother and less occluded
            r_px[x, y] = (int((0.95 - 0.35 * v) * 255), 0, int((0.65 + 0.35 * v) * 255))
    return normal, rma


def upscale(img, size, upscaler, tmp):
    """Fit the longer side to size, keeping the aspect ratio."""
    w, h = img.size
    scale = size / max(w, h)
    target = (max(1, round(w * scale)), max(1, round(h * scale)))
    if upscaler:
        src, dst = os.path.join(tmp, "in.png"), os.path.join(tmp, "out.png")
        img.save(src)
        subprocess.run(upscaler.format(input=src, output=dst), shell=True, check=True)
        out = Image.open(dst).convert(img.mode)
        return out if out.size == target else out.resize(target, Image.LANCZOS)
    return img.resize(target, Image.LANCZOS)


# --------------------------------------------------------------------------
# Pack
# --------------------------------------------------------------------------

def compress(ktx2enc, png, ktx2, kind, quality):
    subprocess.run([ktx2enc, "-t", kind, "-q", str(quality), png, ktx2], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--game-dir", required=True, help="the Half-Life directory (read only)")
    parser.add_argument("--game", default="cstrike")
    parser.add_argument("--map", action="append", required=True, help="map name (repeatable)")
    parser.add_argument("--out", required=True, help="game directory the materials/ tree goes into")
    parser.add_argument("--size", type=int, default=4096, help="longer side of the maps")
    parser.add_argument("--upscaler", help='upscaling command, "{input}" and "{output}" PNG files')
    parser.add_argument("--ktx2enc", required=True, help="path to the ktx2enc tool")
    parser.add_argument("--quality", type=int, default=1, help="BC7 quality 0-6 (default 1)")
    args = parser.parse_args()

    manifest_path = os.path.join(args.out, "texpack.json")
    try:
        manifest = json.load(open(manifest_path))
    except (OSError, ValueError):
        manifest = {}
    settings = f"{args.size}|{args.upscaler}|{args.quality}|1"

    for map_name in args.map:
        bsp = find_file(args.game_dir, args.game, f"maps/{map_name}.bsp")
        if not bsp:
            sys.exit(f"maps/{map_name}.bsp not found")
        textures, wad_names = bsp_textures(bsp)
        wads = [w for w in (find_file(args.game_dir, args.game, n) for n in wad_names) if w]
        wad_data = [wad_textures(w) for w in wads]
        out_dir = os.path.join(args.out, "materials", "maps", f"{map_name}.bsp")
        os.makedirs(out_dir, exist_ok=True)

        for key, (name, img) in sorted(textures.items()):
            if key.lstrip("{!+-0123456789~") in SKIPPED or key in SKIPPED:
                continue
            if img is None:
                for data, lumps in wad_data:
                    if key in lumps:
                        _, img = miptex_to_image(data, lumps[key])
                        break
            if img is None:
                print(f"{name}: not found in the WADs")
                continue

            digest = hashlib.sha1(img.tobytes() + settings.encode()).hexdigest()
            mat_path = os.path.join(out_dir, f"{name}.mat")
            entry = f"{map_name}/{key}"
            if manifest.get(entry) == digest and os.path.isfile(mat_path):
                continue

            print(f"{name} {img.size[0]}x{img.size[1]} -> {args.size}", flush=True)
            normal, rma = derive_maps(img)
            with tempfile.TemporaryDirectory() as tmp:
                for suffix, image, kind in (("", img, "color"), ("_n", normal, "normal"), ("_rma", rma, "linear")):
                    big = upscale(image, args.size, args.upscaler if not suffix else None, tmp)
                    png = os.path.join(tmp, f"map{suffix}.png")
                    big.save(png)
                    compress(args.ktx2enc, png, os.path.join(out_dir, f"{name}{suffix}.ktx2"), kind, args.quality)
            with open(mat_path, "w") as f:
                f.write(f"// texpack.py: {name} at {args.size}\n"
                        f"albedo {name}.ktx2\nnormal {name}_n.ktx2\nrma {name}_rma.ktx2\n")
            manifest[entry] = digest
            json.dump(manifest, open(manifest_path, "w"), indent=1, sort_keys=True)

    print(f"pack in {args.out}/materials, manifest {manifest_path}")


if __name__ == "__main__":
    main()
