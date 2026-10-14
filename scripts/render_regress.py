#!/usr/bin/env python3
"""Visual regression harness for the renderers (ref_gl, ref_vk).

Run it with `python3 -I scripts/render_regress.py <command>`. It only uses the
standard library.

Workflow:

    record                 record the scripted scene demos (once)
    capture --ref gl       render the capture frames of every demo
    compare gl vk          PSNR + diff heatmaps + HTML report
    bench --ref gl         timedemo frame rate of every demo

Scenes are short demos recorded from scripted camera moves. `timedemo` plays
exactly one demo message per rendered frame, and the engine's
`demo_capture_frames` cvar saves the listed frame numbers as PNG, so a frame
number designates the same picture on every run and with every renderer.

Everything produced (demos, captures, reports) contains Valve assets and stays
outside the repository, in --data (default ~/xash-dev/regress).

The engine rewrites the game's .cfg files on exit; they are saved before each
run and restored afterwards, so the harness never changes the player's
settings.
"""

import argparse
import html
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import zlib

HOME = os.path.expanduser("~")
DEFAULT_DEV_ROOT = os.path.join(HOME, "xash-dev", "amd64")
DEFAULT_DATA = os.path.join(HOME, "xash-dev", "regress")
DEFAULT_STEAM = os.path.join(
    HOME, ".local", "share", "Steam", "steamapps", "common", "Half-Life")

WIDTH, HEIGHT = 1280, 720
CAPTURES_PER_SCENE = 5
RUN_TIMEOUT = 300  # seconds per engine run
DEFAULT_THRESHOLD = 40.0  # dB, below this an image is reported as different

# Scripted scenes. "server" arguments go on the command line before +map.
# Recording starts once the map has loaded, then the "setup" commands run, so
# the demo contains the messages that describe the player (weapons, HUD
# state); the moves start once setup has settled. "moves" is a list of
# (console command, frames to wait).
TURN = [("+left", 90), ("-left", 1), ("+lookup", 15), ("-lookup", 1),
        ("+right", 140), ("-right", 1), ("+lookdown", 15), ("-lookdown", 1)]
WALK = [("+forward", 120), ("-forward", 1), ("+moveleft", 60),
        ("-moveleft", 1), ("+back", 60), ("-back", 1)]
HL_SETUP = ["sv_cheats 1", "impulse 101"]  # suit, HUD and all weapons
# ReGameDLL joins the team by itself: the team and class menus would
# otherwise stay open, and come back during playback. Auto-joined players
# spawn without weapons, hence the give.
CS_SERVER = ["+mp_auto_join_team", "1", "+humans_join_team", "CT",
             "+mp_freezetime", "0", "+mp_roundtime", "9"]
CS_SETUP = ["sv_cheats 1", "give weapon_ak47"]

SCENES = [
    # Half-Life: lightstyles and moving tram, outdoor sky, studio models
    {"name": "hl_c1a0", "game": "valve", "map": "c1a0",
     "server": [], "setup": [], "moves": TURN},
    {"name": "hl_c2a5", "game": "valve", "map": "c2a5",
     "server": [], "setup": HL_SETUP, "moves": TURN + WALK},
    {"name": "hl_c3a2", "game": "valve", "map": "c3a2",
     "server": [], "setup": HL_SETUP, "moves": TURN + WALK},
    # Counter-Strike 1.6
    {"name": "cs_de_dust2", "game": "cstrike", "map": "de_dust2",
     "server": CS_SERVER, "setup": CS_SETUP, "moves": WALK + TURN},
    {"name": "cs_de_inferno", "game": "cstrike", "map": "de_inferno",
     "server": CS_SERVER, "setup": CS_SETUP, "moves": WALK + TURN},
    {"name": "cs_cs_office", "game": "cstrike", "map": "cs_office",
     "server": CS_SERVER, "setup": CS_SETUP, "moves": WALK + TURN},
]

# Renderer settings both renderers share, set to their defaults before every
# capture: they are saved per renderer (opengl.cfg, vulkan.cfg), so the
# player's choices would otherwise make the renderers disagree.
RENDER_DEFAULTS = ["gl_overbright 1", "gl_anisotropy 8", "gl_texture_lodbias 0",
                   "gl_texture_nearest 0", "gl_lightmap_nearest 0", "gl_keeptjunctions 1",
                   "r_detailtextures 1", "gl_litwater_force 0"]

# Static scenes: menus, console and HUD rendered without a demo. They run
# with -fixedfps (every clock steps by 1/60 s per frame) and -fixedseed;
# "shot <name>" lines take a screenshot. r_norefresh 1 skips the 3D view so
# 2D can be compared before the world renders.
SHOT_FIXED_FPS = "60"
CONSOLE_TEXT = ["clear", "echo regression console test", "echo 0123456789 abcdefghijklmnopqrstuvwxyz",
                "echo ABCDEFGHIJKLMNOPQRSTUVWXYZ !@#$%^&*()"]

SHOT_SCENES = [
    {"name": "ui_valve", "game": "valve", "map": None, "server": [],
     "lines": ["wait 200", "shot menu"] + CONSOLE_TEXT + ["toggleconsole", "wait 100", "shot console"]},
    {"name": "ui_cstrike", "game": "cstrike", "map": None, "server": [],
     "lines": ["wait 200", "shot menu"]},
    # options page of the customizable crosshair (cs16-client mainui)
    {"name": "menu_crosshair_cstrike", "game": "cstrike", "map": None, "server": [],
     "lines": ["wait 200", "menu_options", "wait 60", "shot options",
               "xhair_enable 0", "menu_crosshair", "wait 60", "shot classic",
               "xhair_enable 1", "xhair_size 3.5", "xhair_gap 0.5", "xhair_thick 0.75", "xhair_dot 1",
               "xhair_pad 1", 'xhair_pad_color "255 0 255"', "menu_crosshair", "wait 60", "shot custom",
               # black background, to compare the 1:1 view with crosshair_cstrike's menu_match
               "ui_xhair_background 1", "menu_crosshair", "wait 60", "shot black",
               # a share code from the console (the built-in circle with dot), then the presets page
               "ui_xhair_background 0", "crosshair_import XH-04404-4607R-CEG1R-W08GC-0314T-07JM1-7ZZZZ-ZZ05T",
               "menu_crosshair", "wait 60", "shot imported", "menu_crosshair_presets", "wait 60", "shot presets",
               "crosshair_export"]},
    # envshot / skyshot write valve/gfx/env/cubetest*.tga; the shot is only there to end the run
    {"name": "cubemap_valve", "game": "valve", "map": "c2a5", "server": [],
     "lines": ["wait 100"] + HL_SETUP + ["wait 200", "envshot cubetest_env", "wait 20",
               "skyshot cubetest_sky", "wait 20", "shot view"]},
    {"name": "hud_valve", "game": "valve", "map": "c2a5", "server": [],
     "lines": ["gl_clear 1", "r_norefresh 1", "wait 100"] + HL_SETUP + ["wait 200", "shot hud"]},
    # customizable crosshair (cs16-client xhair_*), over a black screen
    {"name": "crosshair_cstrike", "game": "cstrike", "map": "de_dust2", "server": CS_SERVER,
     "lines": ["gl_clear 1", "r_norefresh 1", "wait 250"] + CS_SETUP
              + ["wait 150", "cl_dynamiccrosshair 0", "shot classic",
                 "xhair_enable 1", "xhair_gap 1.5", "xhair_size 4.25", "xhair_thick 0.75", "xhair_color \"0 255 0 255\"",
                 "wait 30", "shot cross",
                 "xhair_t 1", "xhair_dot 1", "xhair_dot_size 1.25", "wait 30", "shot tdot",
                 "xhair_t 0", "xhair_dot 0", "xhair_style 1", "xhair_gap 3.5", "xhair_thick 0.6", "xhair_pad 1.5",
                 "xhair_pad_color \"0 0 0 200\"", "wait 30", "shot circle",
                 "xhair_style 0", "xhair_gap 2", "xhair_thick 1", "xhair_pad 1", "xhair_pad_color \"255 0 255\"",
                 "xhair_aa 0", "wait 30", "shot outline",
                 # same crosshair as menu_crosshair_cstrike's preview
                 "xhair_aa 1", "xhair_style 0", "xhair_t 0", "xhair_size 3.5", "xhair_gap 0.5",
                 "xhair_thick 0.75", "xhair_dot 1", "xhair_dot_size 0", "xhair_pad 1", "xhair_scale 1",
                 'xhair_color "0 255 0 255"', "xhair_additive 0",
                 'xhair_pad_color "255 0 255"', "wait 30", "shot menu_match"]},
    # crosshair presets over the map, for side by side boards
    {"name": "crosshair_board", "game": "cstrike", "map": "de_dust2", "server": CS_SERVER,
     "lines": ["wait 250"] + CS_SETUP + ["wait 150", "cl_dynamiccrosshair 0", "xhair_dynamic_move 0",
              "wait 20",
              "shot default",
              "xhair_enable 1",
              "xhair_style 0",
              "xhair_size 2.5",
              "xhair_gap -1",
              "xhair_thick 0.6",
              'xhair_color "0 255 0 255"',
              "xhair_pad 1",
              'xhair_pad_color "0 0 0"',
              "wait 20",
              "shot custom1_cs2_small",
              "xhair_t 1",
              "xhair_dot 1",
              "xhair_dot_size 0.8",
              "xhair_size 3.5",
              "xhair_gap 0.5",
              'xhair_color "0 220 255 255"',
              "wait 20",
              "shot custom2_t_dot_cyan",
              "xhair_t 0",
              "xhair_style 1",
              "xhair_gap 4",
              "xhair_thick 0.5",
              "xhair_dot_size 0.7",
              'xhair_color "255 255 255 230"',
              "wait 20",
              "shot custom3_circle_dot",
              "xhair_style 0",
              "xhair_dot 0",
              "xhair_size 5",
              "xhair_gap 2",
              "xhair_thick 1.5",
              "xhair_pad 1.5",
              'xhair_color "255 230 0 255"',
              "wait 20",
              "shot custom4_thick_yellow",
              "xhair_pad 0",
              "xhair_size 4.25",
              "xhair_gap 1.75",
              "xhair_thick 0.35",
              'xhair_color "255 60 60 255"',
              "wait 20",
              "shot custom5_thin_aa",
              "xhair_aa 0",
              "wait 20",
              "shot custom6_thin_noaa"]},
    # firing: muzzle flash sprites, shell and smoke effects
    {"name": "fire_cstrike", "game": "cstrike", "map": "de_dust2", "server": CS_SERVER,
     "lines": ["wait 250"] + CS_SETUP + ["wait 150", "hud_draw 0", "+attack", "wait 3", "shot first",
                                          "wait 4", "shot burst", "-attack", "wait 20", "shot after"]},
    # egon and gauss beams
    {"name": "beams_valve", "game": "valve", "map": "c2a5", "server": [],
     "lines": ["wait 200"] + HL_SETUP + ["wait 30", "hud_draw 0", "weapon_egon", "wait 60", "+attack", "wait 30",
                                         "shot egon", "-attack", "wait 30", "weapon_gauss", "wait 60",
                                         "+attack", "wait 5", "-attack", "wait 2", "shot gauss"]},
    # c2a2a starts at the edge of a pool: world water (turbulent warp)
    {"name": "water_valve", "game": "valve", "map": "c2a2a", "server": [],
     "lines": ["wait 200"] + HL_SETUP + ["r_drawviewmodel 0", "hud_draw 0", "+lookdown", "wait 25", "-lookdown",
                                         "wait 60", "shot pool"]},
    # underwater fog: noclip from the c2a5 start into the river
    {"name": "underwater_valve", "game": "valve", "map": "c2a5", "server": [],
     "lines": ["wait 200"] + HL_SETUP + ["r_drawviewmodel 0", "hud_draw 0", "noclip", "+moveright", "wait 693",
                                         "-moveright", "+forward", "wait 18", "-forward", "+movedown", "wait 90",
                                         "-movedown", "noclip", "wait 60", "shot river"]},
    {"name": "hud_cstrike", "game": "cstrike", "map": "de_dust2", "server": CS_SERVER,
     "lines": ["gl_clear 1", "r_norefresh 1", "wait 250"] + CS_SETUP
              + ["wait 150", "shot hud", "+showscores", "wait 30", "shot scores", "-showscores",
                 "buy", "wait 60", "shot buymenu"]},
]

# Regions (x, y, width, height in 1280x720 pixels) ignored by compare. The
# Half-Life menu banner animates with libc rand(), which other threads and
# renderer code also consume: it differs between two runs of the same
# renderer.
COMPARE_MASKS = {
    "ui_valve_menu.png": [(0, 90, 1280, 210)],
}

LOAD_FRAMES = 250   # frames waited for the map load and the player spawn
SETUP_FRAMES = 150  # recorded frames given to the setup commands to settle


# --------------------------------------------------------------------------
# Minimal PNG codec (8-bit RGB / RGBA, non-interlaced), standard library only
# --------------------------------------------------------------------------

def png_read(path):
    """Return (width, height, rgb bytes)."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG file")
    pos, idat = 8, []
    width = height = color_type = None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, color_type, _, _, interlace = \
                struct.unpack(">IIBBBBB", chunk)
            if depth != 8 or color_type not in (2, 6) or interlace:
                raise ValueError(f"{path}: unsupported PNG format")
        elif kind == b"IDAT":
            idat.append(chunk)
        elif kind == b"IEND":
            break
    bpp = 3 if color_type == 2 else 4
    stride = width * bpp
    raw = zlib.decompress(b"".join(idat))
    out = bytearray(height * stride)
    prev = bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        ftype = raw[start]
        line = bytearray(raw[start + 1:start + 1 + stride])
        if ftype == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif ftype == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ftype == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif ftype == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line
    if bpp == 4:
        rgb = bytearray(width * height * 3)
        rgb[0::3] = out[0::4]
        rgb[1::3] = out[1::4]
        rgb[2::3] = out[2::4]
        out = rgb
    return width, height, bytes(out)


def png_write(path, width, height, rgb):
    def chunk(kind, payload):
        body = kind + payload
        return struct.pack(">I", len(payload)) + body + \
            struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
    stride = width * 3
    raw = b"".join(b"\x00" + rgb[y * stride:(y + 1) * stride]
                   for y in range(height))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height,
                                           8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        f.write(chunk(b"IEND", b""))


def apply_masks(width, height, data, masks):
    """Black out the masked regions of an RGB buffer."""
    if not masks:
        return data
    data = bytearray(data)
    for x, y, w, h in masks:
        for row in range(max(y, 0), min(y + h, height)):
            start = (row * width + max(x, 0)) * 3
            end = (row * width + min(x + w, width)) * 3
            data[start:end] = bytes(end - start)
    return bytes(data)


def compare_images(path_a, path_b, heatmap_path, masks=None):
    """Return the PSNR in dB (inf when identical) and write a diff heatmap."""
    wa, ha, a = png_read(path_a)
    wb, hb, b = png_read(path_b)
    if (wa, ha) != (wb, hb):
        raise ValueError(f"size mismatch: {path_a} {wa}x{ha}, "
                         f"{path_b} {wb}x{hb}")
    a = apply_masks(wa, ha, a, masks)
    b = apply_masks(wb, hb, b, masks)
    if a == b:
        png_write(heatmap_path, wa, ha, bytes(len(a)))
        return math.inf
    sq = 0
    heat = bytearray(len(a))
    for i in range(0, len(a), 3):
        d0 = a[i] - b[i]
        d1 = a[i + 1] - b[i + 1]
        d2 = a[i + 2] - b[i + 2]
        sq += d0 * d0 + d1 * d1 + d2 * d2
        m = max(abs(d0), abs(d1), abs(d2))
        if m:
            # black -> red -> yellow, amplified so one-step errors show
            v = min(m * 8, 510)
            heat[i] = min(v, 255)
            heat[i + 1] = max(v - 255, 0)
    png_write(heatmap_path, wa, ha, bytes(heat))
    mse = sq / len(a)
    return 10.0 * math.log10(255.0 * 255.0 / mse)


# --------------------------------------------------------------------------
# Engine runs
# --------------------------------------------------------------------------

class Engine:
    def __init__(self, args):
        self.dev_root = os.path.abspath(args.dev_root)
        self.steam = os.path.abspath(args.steam)
        self.data = os.path.abspath(args.data)
        self.exe = os.path.join(self.dev_root, "xash3d")
        self.extra_args = getattr(args, "engine_arg", None) or []
        if not os.path.isfile(self.exe):
            sys.exit(f"engine not found: {self.exe} "
                     "(run scripts/dev/build-amd64.sh)")

    def gamedir(self, game):
        return os.path.join(self.dev_root, game)

    def run(self, game, ref, engine_args, log_name, hide_logo=False):
        """Run the engine, keep its output in data/logs, restore configs.

        hide_logo puts an empty media/logo.avi in the writable game dir: the
        animated menu logo is a video whose decoding isn't deterministic,
        and failing to open it is the only way to turn it off."""
        # a missing renderer makes the engine wait on a modal error dialog
        lib = os.path.join(self.dev_root, f"libref_{ref}.so")
        if not os.path.isfile(lib):
            sys.exit(f"renderer not installed: {lib}")
        gamedir = self.gamedir(game)
        saved = {}
        for name in os.listdir(gamedir):
            path = os.path.join(gamedir, name)
            if name.endswith(".cfg") and os.path.isfile(path):
                with open(path, "rb") as f:
                    saved[path] = f.read()
        logo = os.path.join(gamedir, "media", "logo.avi")
        logo_created = False
        if hide_logo and not os.path.exists(logo):
            os.makedirs(os.path.dirname(logo), exist_ok=True)
            open(logo, "wb").close()
            logo_created = True
        cmd = [self.exe, "-rodir", self.steam, "-game", game,
               "-windowed", "-width", str(WIDTH), "-height", str(HEIGHT),
               "-ref", ref, "-dev", "1"] + self.extra_args + engine_args
        env = dict(os.environ, XASH3D_BASEDIR=self.dev_root)
        os.makedirs(os.path.join(self.data, "logs"), exist_ok=True)
        log_path = os.path.join(self.data, "logs", log_name + ".log")
        try:
            proc = subprocess.run(cmd, cwd=self.dev_root, env=env,
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT,
                                  timeout=RUN_TIMEOUT)
            output = proc.stdout.decode("utf-8", "replace")
        except subprocess.TimeoutExpired as e:
            output = (e.stdout or b"").decode("utf-8", "replace")
            output += f"\n*** killed after {RUN_TIMEOUT} s\n"
        finally:
            if logo_created:
                os.remove(logo)
            for path, content in saved.items():
                with open(path, "wb") as f:
                    f.write(content)
        output = re.sub(r"\x1b\[[0-9;]*m", "", output)
        with open(log_path, "w") as f:
            f.write(output)
        return output

    def write_cfg(self, game, name, lines):
        path = os.path.join(self.gamedir(game), name)
        with open(path, "w") as f:
            f.write("\n".join(lines) + "\n")
        return path

    def demo_dir(self):
        return os.path.join(self.data, "demos")

    def install_demo(self, scene):
        """Copy a recorded demo into the game directory for playback."""
        src = os.path.join(self.demo_dir(), scene["name"] + ".dem")
        if not os.path.isfile(src):
            sys.exit(f"missing demo {src} (run the record command)")
        shutil.copy(src, os.path.join(self.gamedir(scene["game"]),
                                      "regress_" + scene["name"] + ".dem"))


def select_scenes(names, shots=False):
    """Demo scenes (or static shot scenes) named on the command line, all
    of them when none is named."""
    pool = SHOT_SCENES if shots else SCENES
    if not names:
        return pool
    known = {s["name"] for s in SCENES + SHOT_SCENES}
    unknown = [n for n in names if n not in known]
    if unknown:
        sys.exit("unknown scene(s): " + ", ".join(unknown) +
                 "\navailable: " + ", ".join(sorted(known)))
    return [s for s in pool if s["name"] in names]


def load_manifest(engine):
    path = os.path.join(engine.demo_dir(), "manifest.json")
    if not os.path.isfile(path):
        return {}
    with open(path) as f:
        return json.load(f)


def capture_frames(total):
    """Evenly spaced frame numbers, after the setup phase."""
    first, last = SETUP_FRAMES, max(total - 5, SETUP_FRAMES + 1)
    step = (last - first) / CAPTURES_PER_SCENE
    return [int(first + step * (i + 1)) for i in range(CAPTURES_PER_SCENE)]


# --------------------------------------------------------------------------
# Commands
# --------------------------------------------------------------------------

def cmd_record(args):
    engine = Engine(args)
    os.makedirs(engine.demo_dir(), exist_ok=True)
    manifest = load_manifest(engine)
    for scene in select_scenes(args.scenes):
        demo = "regress_" + scene["name"]
        lines = ["developer 0", "m_ignore 1", "hud_draw 1", f"wait {LOAD_FRAMES}",
                 f"record {demo}"] + scene["setup"]
        lines.append(f"wait {SETUP_FRAMES}")
        for command, frames in scene["moves"]:
            lines.append(command)
            lines.append(f"wait {frames}")
        lines += ["wait 30", "stop", "quit"]
        engine.write_cfg(scene["game"], "regress_record.cfg", lines)
        print(f"recording {scene['name']} ...", flush=True)
        output = engine.run(scene["game"], "gl",
                            scene["server"] + ["+map", scene["map"], "+exec",
                                               "regress_record.cfg"],
                            "record_" + scene["name"])
        m = re.search(r"Recording time: \S+, frames (\d+)", output)
        src = os.path.join(engine.gamedir(scene["game"]), demo + ".dem")
        if not m or not os.path.isfile(src):
            print(f"  FAILED, see {args.data}/logs/record_{scene['name']}.log")
            continue
        shutil.move(src, os.path.join(engine.demo_dir(),
                                      scene["name"] + ".dem"))
        manifest[scene["name"]] = {"game": scene["game"],
                                   "map": scene["map"],
                                   "frames": int(m.group(1))}
        print(f"  {m.group(1)} frames")
    with open(os.path.join(engine.demo_dir(), "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2, sort_keys=True)


def cmd_capture(args):
    engine = Engine(args)
    manifest = load_manifest(engine)
    label = args.label or args.ref
    out_dir = os.path.join(engine.data, "captures", label)
    os.makedirs(out_dir, exist_ok=True)
    for scene in select_scenes(args.scenes):
        info = manifest.get(scene["name"])
        if not info:
            print(f"skipping {scene['name']}: not recorded")
            continue
        engine.install_demo(scene)
        frames = capture_frames(info["frames"])
        shot_dir = os.path.join(engine.gamedir(scene["game"]),
                                "regress_shots")
        shutil.rmtree(shot_dir, ignore_errors=True)
        engine.write_cfg(scene["game"], "regress_capture.cfg", RENDER_DEFAULTS + [
            "developer 0",
            f"demo_capture_frames \"{' '.join(map(str, frames))}\"",
            "demo_capture_dir regress_shots",
        ] + (args.set or []))
        print(f"capturing {scene['name']} with ref_{args.ref} "
              f"(frames {' '.join(map(str, frames))}) ...", flush=True)
        engine.run(scene["game"], args.ref,
                   ["-fixedseed", "+exec", "regress_capture.cfg",
                    "-timedemo", "regress_" + scene["name"]],
                   f"capture_{label}_{scene['name']}")
        written = 0
        for name in sorted(os.listdir(shot_dir)) if \
                os.path.isdir(shot_dir) else []:
            # regress_<scene>_<frame>.png -> <scene>_<frame>.png
            shutil.move(os.path.join(shot_dir, name),
                        os.path.join(out_dir, name[len("regress_"):]))
            written += 1
        shutil.rmtree(shot_dir, ignore_errors=True)
        print(f"  {written}/{len(frames)} images")
    capture_shots(engine, args, out_dir, label)
    print(f"captures in {out_dir}")


def capture_shots(engine, args, out_dir, label):
    for scene in select_scenes(args.scenes, shots=True):
        shot_dir = os.path.join(engine.gamedir(scene["game"]), "regress_shots")
        shutil.rmtree(shot_dir, ignore_errors=True)
        # the game window takes the focus: a mouse moved meanwhile turns the view
        lines = RENDER_DEFAULTS + ["developer 0", "m_ignore 1"] + (args.set or [])
        expected = 0
        for line in scene["lines"]:
            if line.startswith("shot "):
                lines.append(f"screenshot regress_shots/{scene['name']}_{line[5:]}.png")
                lines.append("wait 2")
                expected += 1
            else:
                lines.append(line)
        lines += ["quit"]
        engine.write_cfg(scene["game"], "regress_shots.cfg", lines)
        print(f"capturing {scene['name']} with ref_{args.ref} ...", flush=True)
        run_args = ["-fixedfps", SHOT_FIXED_FPS, "-fixedseed"] + scene["server"]
        if scene["map"]:
            run_args += ["+map", scene["map"]]
        engine.run(scene["game"], args.ref, run_args + ["+exec", "regress_shots.cfg"],
                   f"shots_{label}_{scene['name']}", hide_logo=True)
        written = 0
        if os.path.isdir(shot_dir):
            for name in sorted(os.listdir(shot_dir)):
                shutil.move(os.path.join(shot_dir, name), os.path.join(out_dir, name))
                written += 1
        shutil.rmtree(shot_dir, ignore_errors=True)
        print(f"  {written}/{expected} images")


def cmd_bench(args):
    engine = Engine(args)
    manifest = load_manifest(engine)
    results = {}
    for scene in select_scenes(args.scenes):
        if scene["name"] not in manifest:
            print(f"skipping {scene['name']}: not recorded")
            continue
        engine.install_demo(scene)
        fps = []
        for run in range(args.runs):
            output = engine.run(scene["game"], args.ref,
                                ["+gl_vsync", "0", "+fps_max", "0",
                                 "-timedemo", "regress_" + scene["name"]],
                                f"bench_{args.ref}_{scene['name']}_{run}")
            m = re.search(r"timedemo result: (\d+) frames ([\d.]+) seconds "
                          r"([\d.]+) fps", output)
            if m:
                fps.append(float(m.group(3)))
        if fps:
            results[scene["name"]] = {"runs": fps,
                                      "avg_fps": sum(fps) / len(fps)}
            print(f"{scene['name']:16} {results[scene['name']]['avg_fps']:9.1f}"
                  f" fps  (runs: {', '.join(f'{v:.1f}' for v in fps)})")
        else:
            print(f"{scene['name']:16} FAILED")
    out = os.path.join(engine.data, f"bench_{args.ref}.json")
    with open(out, "w") as f:
        json.dump(results, f, indent=2, sort_keys=True)
    print(f"results in {out}")


def cmd_compare(args):
    data = os.path.abspath(args.data)
    dir_a = os.path.join(data, "captures", args.reference)
    dir_b = os.path.join(data, "captures", args.test)
    for d in (dir_a, dir_b):
        if not os.path.isdir(d):
            sys.exit(f"no captures in {d}")
    report = os.path.join(data, "reports", f"{args.reference}-vs-{args.test}")
    os.makedirs(os.path.join(report, "diff"), exist_ok=True)
    rows, failures = [], 0
    for name in sorted(os.listdir(dir_a)):
        if not name.endswith(".png"):
            continue
        path_a = os.path.join(dir_a, name)
        path_b = os.path.join(dir_b, name)
        if not os.path.isfile(path_b):
            rows.append((name, None, "missing"))
            failures += 1
            continue
        psnr = compare_images(path_a, path_b,
                              os.path.join(report, "diff", name),
                              COMPARE_MASKS.get(name))
        ok = psnr >= args.threshold
        failures += not ok
        rows.append((name, psnr, "ok" if ok else "DIFFERENT"))
        shown = "identical" if math.isinf(psnr) else f"{psnr:.2f} dB"
        print(f"{name:32} {shown:>12}  {'' if ok else 'DIFFERENT'}")
    write_report(report, dir_a, dir_b, args, rows)
    print(f"\n{len(rows) - failures}/{len(rows)} images above "
          f"{args.threshold} dB, report: {report}/index.html")
    return 1 if failures else 0


def write_report(report, dir_a, dir_b, args, rows):
    def rel(path):
        return html.escape(os.path.relpath(path, report))
    out = ["<!doctype html><meta charset=utf-8>",
           f"<title>{html.escape(args.reference)} vs "
           f"{html.escape(args.test)}</title>",
           "<style>body{font:14px sans-serif;background:#111;color:#ddd}"
           "table{border-collapse:collapse}td,th{padding:4px;"
           "border-bottom:1px solid #333;vertical-align:top}"
           "img{width:320px}.bad{color:#f55}.ok{color:#5c5}</style>",
           f"<h1>{html.escape(args.reference)} vs "
           f"{html.escape(args.test)}</h1>",
           f"<p>Threshold: {args.threshold} dB PSNR. Heatmap: black = "
           "identical, red to yellow = growing difference (x8).</p>",
           "<table><tr><th>Image</th><th>PSNR</th>"
           f"<th>{html.escape(args.reference)}</th>"
           f"<th>{html.escape(args.test)}</th><th>Difference</th></tr>"]
    for name, psnr, status in rows:
        if psnr is None:
            value = "missing"
        elif math.isinf(psnr):
            value = "identical"
        else:
            value = f"{psnr:.2f} dB"
        cls = "ok" if status == "ok" else "bad"
        cells = [f"<td>{html.escape(name)}</td>",
                 f"<td class={cls}>{value}</td>",
                 f"<td><a href='{rel(os.path.join(dir_a, name))}'>"
                 f"<img src='{rel(os.path.join(dir_a, name))}'></a></td>"]
        if psnr is None:
            cells.append("<td></td><td></td>")
        else:
            diff = os.path.join(report, "diff", name)
            cells.append(f"<td><a href='{rel(os.path.join(dir_b, name))}'>"
                         f"<img src='{rel(os.path.join(dir_b, name))}'>"
                         "</a></td>")
            cells.append(f"<td><a href='{rel(diff)}'><img src='{rel(diff)}'>"
                         "</a></td>")
        out.append("<tr>" + "".join(cells) + "</tr>")
    out.append("</table>")
    with open(os.path.join(report, "index.html"), "w") as f:
        f.write("\n".join(out) + "\n")


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument("--dev-root", default=DEFAULT_DEV_ROOT,
                        help="engine install directory (%(default)s)")
    parser.add_argument("--steam", default=DEFAULT_STEAM,
                        help="Steam Half-Life directory (%(default)s)")
    parser.add_argument("--data", default=DEFAULT_DATA,
                        help="demos, captures and reports (%(default)s)")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("record", help="record the scene demos")
    p.add_argument("scenes", nargs="*", help="scene names (default: all)")
    p.set_defaults(func=cmd_record)

    p = sub.add_parser("capture", help="capture frames with a renderer")
    p.add_argument("--ref", default="gl", help="renderer (gl, vk, ...)")
    p.add_argument("--label", help="capture set name (default: --ref)")
    p.add_argument("--engine-arg", action="append", metavar="ARG",
                   help="extra engine command line argument, e.g. "
                        "--engine-arg=-vkvalidation (repeatable)")
    p.add_argument("--set", action="append", metavar="COMMAND",
                   help="extra console command run before the capture, "
                        "e.g. --set 'r_dynamic 0' (repeatable)")
    p.add_argument("scenes", nargs="*", help="scene names (default: all)")
    p.set_defaults(func=cmd_capture)

    p = sub.add_parser("compare", help="compare two capture sets")
    p.add_argument("reference", help="reference capture set, e.g. gl")
    p.add_argument("test", help="tested capture set, e.g. vk")
    p.add_argument("--threshold", type=float, default=DEFAULT_THRESHOLD,
                   help="minimum PSNR in dB (%(default)s)")
    p.set_defaults(func=cmd_compare)

    p = sub.add_parser("bench", help="timedemo frame rate")
    p.add_argument("--ref", default="gl", help="renderer (gl, vk, ...)")
    p.add_argument("--runs", type=int, default=3, help="runs per scene")
    p.add_argument("scenes", nargs="*", help="scene names (default: all)")
    p.set_defaults(func=cmd_bench)

    args = parser.parse_args()
    sys.exit(args.func(args) or 0)


if __name__ == "__main__":
    main()
