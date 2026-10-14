#!/usr/bin/env python3
"""Material pack builder: game textures to .mat files with KTX2 maps.

What it packs:

  world textures   of the maps given with --map, or of every map of the
                   game with --all-maps; a texture used by several maps
                   goes to materials/common/, one whose content differs
                   between maps to materials/maps/<map>.bsp/
  model skins      with --models: every models/**.mdl of the game (the
                   textures of *T.mdl files included), into
                   materials/models/<model>/; player colour textures are
                   left out, they are recoloured from their pixels
  sprites          with --sprites: the single frames of every sprites/*.spr,
                   as TGA files the engine loads by itself
                   (materials/sprites/<sprite>/frameNN.tga)

Each texture goes through:

  1. extract     8-bit pixels and palette to RGBA ('{' textures and masked
                 model textures: the last palette entry is transparent)
  2. derive      a normal map and a roughness / metalness / occlusion map
                 from the luminance (heuristics, a starting point to edit)
  3. upscale     to --size (world), --model-size, --sprite-scale: Lanczos,
                 or any command given with --upscaler "cmd {input}
                 {output}" (a Real-ESRGAN class model) for the colour.
                 World textures repeat: they are upscaled with a border
                 taken from their other side, then cropped, so their edges
                 still meet. Transparent pixels get the colour of their
                 neighbours first (no dark fringes), and the alpha mask is
                 upscaled apart then thresholded again (crisp edges).
  4. compress    with ktx2enc (utils/ktx2enc): BC7 colour and masks, BC5
                 normals, every mip level
  5. write       the .mat files

Hand made maps win: with --overrides DIR, DIR/<texture>.png,
<texture>_n.png and <texture>_rma.png replace the extracted colour and the
derived maps (at any size); a .mat edited by hand (without the texpack.py
line at its top) is never overwritten.

A manifest (<out>/texpack.json) keeps a hash of each texture, of its
overrides and of the settings: a second run only redoes what changed.

usage: texpack.py --game-dir STEAM/Half-Life --game cstrike --all-maps
                  --models --sprites --out ~/xash-dev/amd64/cstrike
                  --ktx2enc build-utils/utils/ktx2enc/ktx2enc

Packs made from Valve's textures are for personal use only: never commit
them nor share them. The tool only reads the game directory.

Needs Pillow (python3-pil).
"""

import argparse
import glob
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

STUDIO_NF_MASKED = 0x0040


# --------------------------------------------------------------------------
# Extraction
# --------------------------------------------------------------------------

def indexed_to_rgba(pixels, width, height, palette, transparent_last):
    img = Image.frombytes("P", (width, height), pixels)
    img.putpalette(palette)
    img = img.convert("RGBA")
    if transparent_last:
        img.putalpha(Image.frombytes("L", (width, height), bytes(0 if p == 255 else 255 for p in pixels)))
    return img


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
    return name, indexed_to_rgba(pixels, width, height, data[pal_ofs: pal_ofs + 768], name.startswith("{"))


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


def studio_textures(path):
    """[(name without extension, RGBA image)] of a model, its *T.mdl too."""
    data = open(path, "rb").read()
    if data[:4] != b"IDST":
        return []
    numtextures, textureindex = struct.unpack_from("<ii", data, 180)
    if numtextures == 0:
        tpath = path[:-4] + "T.mdl"
        if not os.path.isfile(tpath):
            tpath = path[:-4] + "t.mdl"
        if not os.path.isfile(tpath):
            return []
        data = open(tpath, "rb").read()
        numtextures, textureindex = struct.unpack_from("<ii", data, 180)
    out = []
    for i in range(numtextures):
        ofs = textureindex + i * 80
        name = data[ofs:ofs + 64].split(b"\0")[0].decode("latin1")
        flags, width, height, index = struct.unpack_from("<iiii", data, ofs + 64)
        base = os.path.splitext(name)[0]
        # the engine recolours these with the player colours
        if base.lower().startswith(("dm_base", "remap")):
            continue
        pixels = data[index:index + width * height]
        palette = data[index + width * height: index + width * height + 768]
        if len(pixels) == width * height and len(palette) == 768:
            out.append((base, indexed_to_rgba(pixels, width, height, palette, bool(flags & STUDIO_NF_MASKED))))
    return out


def sprite_frames(path):
    """[(frame number, RGBA image)] of the single frames of a GoldSrc sprite."""
    data = open(path, "rb").read()
    if data[:4] != b"IDSP" or struct.unpack_from("<i", data, 4)[0] != 2:
        return []
    tex_format, = struct.unpack_from("<i", data, 12)
    num_frames, = struct.unpack_from("<i", data, 28)
    pal_count, = struct.unpack_from("<h", data, 40)
    palette = data[42:42 + pal_count * 3].ljust(768, b"\0")
    ofs = 42 + pal_count * 3
    frames = []
    for num in range(num_frames):
        frame_type, = struct.unpack_from("<i", data, ofs)
        if frame_type != 0:
            break  # groups: left as they are
        _, _, width, height = struct.unpack_from("<iiii", data, ofs + 4)
        pixels = data[ofs + 20: ofs + 20 + width * height]
        ofs += 20 + width * height
        if tex_format == 2:  # SPR_INDEXALPHA: the last colour, the index as alpha
            img = Image.new("RGBA", (width, height), tuple(palette[765:768]) + (255,))
            img.putalpha(Image.frombytes("L", (width, height), pixels))
        else:
            img = indexed_to_rgba(pixels, width, height, palette, tex_format == 3)  # SPR_ALPHTEST
        frames.append((num, img))
    return frames


def find_file(game_dir, game, name):
    for d in (game, "valve"):
        p = os.path.join(game_dir, d, name)
        if os.path.isfile(p):
            return p
    return None


# --------------------------------------------------------------------------
# Image processing
# --------------------------------------------------------------------------

def derive_maps(albedo, tiling, strength=3.0):
    """Normal map (green up) and roughness / metalness / occlusion from the
    luminance; a tiling texture wraps around, others clamp."""
    lum = albedo.convert("L").filter(ImageFilter.GaussianBlur(0.8))
    w, h = lum.size
    px = lum.load()
    normal = Image.new("RGB", (w, h))
    rma = Image.new("RGB", (w, h))
    n_px, r_px = normal.load(), rma.load()

    def at(x, y):
        if tiling:
            return px[x % w, y % h]
        return px[min(max(x, 0), w - 1), min(max(y, 0), h - 1)]

    for y in range(h):
        for x in range(w):
            dx = (at(x + 1, y) - at(x - 1, y)) / 255.0 * strength
            dy = (at(x, y + 1) - at(x, y - 1)) / 255.0 * strength
            nx, ny, nz = -dx, dy, 1.0
            length = (nx * nx + ny * ny + nz * nz) ** 0.5
            n_px[x, y] = tuple(int((c / length * 0.5 + 0.5) * 255) for c in (nx, ny, nz))
            v = px[x, y] / 255.0
            # brighter is smoother and less occluded
            r_px[x, y] = (int((0.95 - 0.35 * v) * 255), 0, int((0.65 + 0.35 * v) * 255))
    return normal, rma


def bleed_colours(img, passes=16):
    """Transparent pixels take the colour of the opaque ones around them, so
    filtering and upscaling don't pull black into the edges."""
    w, h = img.size
    px = img.load()
    filled = [[px[x, y][3] >= 128 for x in range(w)] for y in range(h)]
    for _ in range(passes):
        grown = []
        for y in range(h):
            for x in range(w):
                if filled[y][x]:
                    continue
                total, count = [0, 0, 0], 0
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        nx, ny = x + dx, y + dy
                        if 0 <= nx < w and 0 <= ny < h and filled[ny][nx]:
                            c = px[nx, ny]
                            total[0] += c[0]; total[1] += c[1]; total[2] += c[2]
                            count += 1
                if count:
                    grown.append((x, y, tuple(t // count for t in total)))
        if not grown:
            break
        for x, y, c in grown:
            px[x, y] = c + (px[x, y][3],)
            filled[y][x] = True
    return img


def resize(img, target, tiling, upscaler, tmp):
    """img to target size; a tiling texture with a border from its other
    side, cropped back, so the edges keep meeting."""
    w, h = img.size
    if tiling:
        pad_x, pad_y = max(2, w // 8), max(2, h // 8)
        padded = Image.new(img.mode, (w + 2 * pad_x, h + 2 * pad_y))
        for oy in (-1, 0, 1):
            for ox in (-1, 0, 1):
                padded.paste(img, (pad_x + ox * w, pad_y + oy * h))
        sx, sy = target[0] / w, target[1] / h
        big = scale(padded, (round(padded.width * sx), round(padded.height * sy)), upscaler, tmp)
        left, top = round(pad_x * sx), round(pad_y * sy)
        return big.crop((left, top, left + target[0], top + target[1]))
    return scale(img, target, upscaler, tmp)


def scale(img, target, upscaler, tmp):
    if upscaler:
        src, dst = os.path.join(tmp, "in.png"), os.path.join(tmp, "out.png")
        img.save(src)
        subprocess.run(upscaler.format(input=src, output=dst), shell=True, check=True)
        out = Image.open(dst).convert(img.mode)
        return out if out.size == target else out.resize(target, Image.LANCZOS)
    return img.resize(target, Image.LANCZOS)


def upscale_colour(img, target, tiling, upscaler, tmp):
    """Colour with its alpha: the mask upscaled apart and thresholded."""
    has_alpha = img.getchannel("A").getextrema()[0] < 255
    if not has_alpha:
        return resize(img.convert("RGB"), target, tiling, upscaler, tmp)
    img = bleed_colours(img.copy())
    rgb = resize(img.convert("RGB"), target, tiling, upscaler, tmp)
    mask = resize(img.getchannel("A"), target, tiling, None, tmp).point(lambda a: 255 if a >= 128 else 0)
    rgb.putalpha(mask)
    return rgb


def fit(size, longest):
    w, h = size
    s = longest / max(w, h)
    return max(1, round(w * s)), max(1, round(h * s))


# --------------------------------------------------------------------------
# Pack
# --------------------------------------------------------------------------

class Pack:
    def __init__(self, args):
        self.args = args
        self.manifest_path = os.path.join(args.out, "texpack.json")
        try:
            self.manifest = json.load(open(self.manifest_path))
        except (OSError, ValueError):
            self.manifest = {}

    def save(self):
        json.dump(self.manifest, open(self.manifest_path, "w"), indent=1, sort_keys=True)

    def digest(self, img, settings):
        return hashlib.sha1(img.tobytes() + img.mode.encode() + settings.encode()).hexdigest()

    def override(self, name, suffix):
        if not self.args.overrides:
            return None
        path = os.path.join(self.args.overrides, f"{name}{suffix}.png")
        return Image.open(path) if os.path.isfile(path) else None

    def material(self, out_dir, name, img, longest, tiling, key):
        """KTX2 maps and the .mat of one texture, unless already done."""
        overrides = [self.override(name, s) for s in ("", "_n", "_rma")]
        settings = f"{longest}|{tiling}|{self.args.upscaler}|{self.args.quality}|2"
        for o in overrides:
            settings += "|" + (self.digest(o, "") if o else "-")
        digest = self.digest(img, settings)
        mat_path = os.path.join(out_dir, f"{name}.mat")
        if self.manifest.get(key) == digest and os.path.isfile(mat_path):
            return
        if os.path.isfile(mat_path) and not open(mat_path).readline().startswith("// texpack.py"):
            print(f"{key}: {mat_path} edited by hand, left as it is")
            return
        os.makedirs(out_dir, exist_ok=True)
        target = fit(img.size, longest)
        print(f"{key} {img.size[0]}x{img.size[1]} -> {target[0]}x{target[1]}", flush=True)
        normal, rma = derive_maps(img, tiling)
        with tempfile.TemporaryDirectory() as tmp:
            colour = overrides[0].convert("RGBA").resize(target, Image.LANCZOS) if overrides[0] \
                else upscale_colour(img, target, tiling, self.args.upscaler, tmp)
            normal = overrides[1].convert("RGB").resize(target, Image.LANCZOS) if overrides[1] \
                else resize(normal, target, tiling, None, tmp)
            rma = overrides[2].convert("RGB").resize(target, Image.LANCZOS) if overrides[2] \
                else resize(rma, target, tiling, None, tmp)
            maps = (("", colour, "color"), ("_n", normal, "normal"), ("_rma", rma, "linear"))
            for suffix, image, kind in maps:
                png = os.path.join(tmp, f"map{suffix}.png")
                image.save(png)
                subprocess.run([self.args.ktx2enc, "-t", kind, "-q", str(self.args.quality), png,
                                os.path.join(out_dir, f"{name}{suffix}.ktx2")], check=True)
        with open(mat_path, "w") as f:
            f.write(f"// texpack.py: {key}\nalbedo {name}.ktx2\nnormal {name}_n.ktx2\nrma {name}_rma.ktx2\n")
        self.manifest[key] = digest
        self.save()

    def world(self, maps):
        """World textures; shared ones once in materials/common."""
        args = self.args
        seen = {}  # lower name -> digest of the version in common
        for map_name in maps:
            bsp = find_file(args.game_dir, args.game, f"maps/{map_name}.bsp")
            if not bsp:
                print(f"maps/{map_name}.bsp not found")
                continue
            textures, wad_names = bsp_textures(bsp)
            wads = [wad_textures(w) for w in (find_file(args.game_dir, args.game, n) for n in wad_names) if w]
            for key, (name, img) in sorted(textures.items()):
                if key.lstrip("{!+-0123456789~") in SKIPPED or key in SKIPPED:
                    continue
                if img is None:
                    for data, lumps in wads:
                        if key in lumps:
                            _, img = miptex_to_image(data, lumps[key])
                            break
                if img is None:
                    print(f"{map_name}: {name} not in its WADs")
                    continue
                pixels = self.digest(img, "")
                common = len(maps) > 1 and seen.setdefault(key, pixels) == pixels
                if common:
                    out_dir = os.path.join(args.out, "materials", "common")
                    entry = f"common/{key}"
                else:
                    out_dir = os.path.join(args.out, "materials", "maps", f"{map_name}.bsp")
                    entry = f"{map_name}/{key}"
                self.material(out_dir, name, img, args.size, True, entry)

    def models(self):
        args = self.args
        root = os.path.join(args.game_dir, args.game)
        for path in sorted(glob.glob(os.path.join(root, "models", "**", "*.mdl"), recursive=True)):
            rel = os.path.relpath(path, root)[:-4].replace(os.sep, "/")
            if rel.lower().endswith("t") and os.path.isfile(path[:-5] + ".mdl"):
                continue  # the textures of another model
            for name, img in studio_textures(path):
                self.material(os.path.join(args.out, "materials", rel), name, img, args.model_size, False, f"{rel}/{name}")

    def sprites(self):
        args = self.args
        root = os.path.join(args.game_dir, args.game)
        for path in sorted(glob.glob(os.path.join(root, "sprites", "*.spr"))):
            rel = os.path.relpath(path, root)[:-4].replace(os.sep, "/")
            for num, img in sprite_frames(path):
                key = f"{rel}/frame{num // 10}{num % 10}"
                digest = self.digest(img, f"{args.sprite_scale}|{args.upscaler}|2")
                out = os.path.join(args.out, "materials", f"{key}.tga")
                if self.manifest.get(key) == digest and os.path.isfile(out):
                    continue
                os.makedirs(os.path.dirname(out), exist_ok=True)
                target = (img.width * args.sprite_scale, img.height * args.sprite_scale)
                with tempfile.TemporaryDirectory() as tmp:
                    if img.getchannel("A").getextrema()[0] < 255 and img.getchannel("A").getextrema() != (0, 255):
                        big = resize(img, target, False, None, tmp)  # alpha ramps (SPR_INDEXALPHA)
                    else:
                        big = upscale_colour(img, target, False, args.upscaler, tmp)
                big.convert("RGBA").save(out)
                print(f"{key} {img.size[0]}x{img.size[1]} -> {target[0]}x{target[1]}", flush=True)
                self.manifest[key] = digest
                self.save()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--game-dir", required=True, help="the Half-Life directory (read only)")
    parser.add_argument("--game", default="cstrike")
    parser.add_argument("--map", action="append", default=[], help="map name (repeatable)")
    parser.add_argument("--all-maps", action="store_true", help="every map of the game")
    parser.add_argument("--models", action="store_true", help="model skins")
    parser.add_argument("--sprites", action="store_true", help="sprite frames")
    parser.add_argument("--out", required=True, help="game directory the materials/ tree goes into")
    parser.add_argument("--size", type=int, default=4096, help="longer side of world maps")
    parser.add_argument("--model-size", type=int, default=2048, help="longer side of model maps")
    parser.add_argument("--sprite-scale", type=int, default=4, help="sprite upscale factor")
    parser.add_argument("--upscaler", help='upscaling command, "{input}" and "{output}" PNG files')
    parser.add_argument("--ktx2enc", required=True, help="path to the ktx2enc tool")
    parser.add_argument("--overrides", help="directory of hand made <texture>[_n|_rma].png maps")
    parser.add_argument("--quality", type=int, default=1, help="BC7 quality 0-6 (default 1)")
    args = parser.parse_args()

    maps = list(args.map)
    if args.all_maps:
        root = os.path.join(args.game_dir, args.game, "maps")
        maps += sorted(os.path.basename(p)[:-4] for p in glob.glob(os.path.join(root, "*.bsp")))
    if not maps and not args.models and not args.sprites:
        sys.exit("nothing to pack: --map, --all-maps, --models or --sprites")

    pack = Pack(args)
    if maps:
        pack.world(maps)
    if args.models:
        pack.models()
    if args.sprites:
        pack.sprites()
    print(f"pack in {args.out}/materials, manifest {pack.manifest_path}")


if __name__ == "__main__":
    main()
