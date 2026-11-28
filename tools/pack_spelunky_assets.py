#!/usr/bin/env python3
"""Packs GDSpelunky sprites and sounds into godot4-retro atlases and C tables."""

import glob
import hashlib
import os
import re
import shutil
import sys
from PIL import Image

GD_ROOT = r"C:\Users\memor\.gemini\antigravity\brain\bed346dc-5c9f-41e3-a887-370223bde575\scratch\gdspelunky"
OUT_ASSETS = os.path.join("assets", "spelunky")
OUT_C_HDR = os.path.join("runtime", "game", "spelunky_sprites.h")
OUT_C_SRC = os.path.join("runtime", "game", "spelunky_sprites.c")

SKIP_FOLDERS = (
    "sprites/level editor/",
    "sprites/supersound/",
    "sprites/font/",
    "sprites/font_old/",
    "sprites/font_small/",
)

# Priority order so terrain/blocks/traps/items/hud fit cleanly in 16a, character/enemies/effects in 16b
PRIORITY_PREFIXES = (
    "sprites/blocks/",
    "sprites/traps/",
    "sprites/items/",
    "sprites/collectibles/",
    "sprites/hud/",
    "sprites/mushrooms/",
    "sprites/black",
    "sprites/red",
    "sprites/room",
    "sprites/lvl",
)


def parse_sprites_gd(path):
    txt = open(path, encoding="utf-8").read()
    blocks = re.findall(r'\t"([^"]+)"\s*:\s*\{(.*?)\n\t\}', txt, re.S)
    entries = []
    for name, body in blocks:
        m_org = re.search(r'"origin"\s*:\s*Vector2\(([^,]+),\s*([^)]+)\)', body)
        m_dir = re.search(r'"folder_path"\s*:\s*"res://([^"]+)"', body)
        m_bb = re.search(
            r'"bounding_box"\s*:\s*\[Vector2\(([^,]+),\s*([^)]+)\),\s*Vector2\(([^,]+),\s*([^)]+)\)\]',
            body,
        )
        if not m_org or not m_dir:
            continue
        folder = m_dir.group(1)
        if any(folder.startswith(s.rstrip("/")) for s in SKIP_FOLDERS):
            continue
        ox = int(round(float(m_org.group(1))))
        oy = int(round(float(m_org.group(2))))
        if m_bb:
            bx, by, bw, bh = [int(round(float(x))) for x in m_bb.groups()]
        else:
            bx, by, bw, bh = 0, 0, 16, 16
        entries.append((name, folder, ox, oy, bx, by, bw, bh))
    return entries


def frame_sort_key(path):
    base = os.path.splitext(os.path.basename(path))[0]
    m = re.search(r"_(\d+)$", base)
    return (int(m.group(1)) if m else 0, base)


def sanitize_ident(name):
    s = re.sub(r"[^A-Za-z0-9_]", "_", name).upper()
    if s and s[0].isdigit():
        s = "_" + s
    return s


def main():
    os.makedirs(OUT_ASSETS, exist_ok=True)
    os.makedirs(os.path.join(OUT_ASSETS, "sounds"), exist_ok=True)
    os.makedirs(os.path.join(OUT_ASSETS, "bg"), exist_ok=True)
    os.makedirs(os.path.join(OUT_ASSETS, "large"), exist_ok=True)

    entries = parse_sprites_gd(os.path.join(GD_ROOT, "resources", "sprites.gd"))
    # Sort so terrain/items/hud come first (keeping 16x16 tiles together in sheet 16A)
    def sort_key(e):
        folder = e[1]
        prio = 0 if any(folder.startswith(p.rstrip("/")) for p in PRIORITY_PREFIXES) else 1
        return (prio, e[0])

    entries.sort(key=sort_key)

    # Sheet definitions: (id, filename, width, height, cell_size)
    sheets_cfg = [
        ("SP_SHEET_16A", "atlas_16a.png", 512, 512, 16),   # 1024 slots
        ("SP_SHEET_16B", "atlas_16b.png", 512, 64, 16),    # 128 slots (saves 448 KB VRAM)
        ("SP_SHEET_32A", "atlas_32a.png", 512, 512, 32),   # 256 slots
        ("SP_SHEET_32B", "atlas_32b.png", 512, 512, 32),   # 256 slots
        ("SP_SHEET_64A", "atlas_64a.png", 512, 512, 64),   # 64 slots
        ("SP_SHEET_64B", "atlas_64b.png", 512, 256, 64),   # 32 slots
    ]
    sheets = []
    for sid, fname, sw, sh, cell in sheets_cfg:
        sheets.append({
            "id": sid,
            "fname": fname,
            "w": sw,
            "h": sh,
            "cell": cell,
            "cols": sw // cell,
            "cap": (sw // cell) * (sh // cell),
            "count": 0,
            "img": Image.new("RGBA", (sw, sh), (0, 0, 0, 0)),
        })

    dedup = {}  # (bucket_cell, md5) -> (sheet_idx, frame_idx)
    frames_out = []
    anims_out = []
    large_copied = []

    for name, folder, ox, oy, bx, by, bw, bh in entries:
        src_dir = os.path.join(GD_ROOT, folder.replace("/", os.sep))
        pngs = sorted(glob.glob(os.path.join(src_dir, "*.png")), key=frame_sort_key)
        if not pngs:
            continue

        # Determine max size across frames of this animation
        imgs = []
        max_dim = 0
        for p in pngs:
            im = Image.open(p).convert("RGBA")
            imgs.append((p, im))
            max_dim = max(max_dim, im.width, im.height)

        if max_dim > 64:
            # Copy first frame (or all frames) to large/ for direct Sprite2D/TextureRect use
            for idx, (p, im) in enumerate(imgs):
                out_name = f"{sanitize_ident(name).lower()}_{idx}.png"
                im.save(os.path.join(OUT_ASSETS, "large", out_name))
                large_copied.append(out_name)
                im.close()
            continue

        bucket = 16 if max_dim <= 16 else (32 if max_dim <= 32 else 64)
        first_frame = len(frames_out)
        for p, im in imgs:
            h = hashlib.md5(im.tobytes() + f"{im.size}".encode()).hexdigest()
            key = (bucket, h)
            if key in dedup:
                sh_idx, fr_idx = dedup[key]
            else:
                # Find sheet with capacity matching bucket
                sh_idx = None
                for i, s in enumerate(sheets):
                    if s["cell"] == bucket and s["count"] < s["cap"]:
                        sh_idx = i
                        break
                if sh_idx is None:
                    raise RuntimeError(f"Out of atlas space for bucket {bucket} at {name}")
                s = sheets[sh_idx]
                fr_idx = s["count"]
                s["count"] += 1
                cx = (fr_idx % s["cols"]) * s["cell"]
                cy = (fr_idx // s["cols"]) * s["cell"]
                s["img"].paste(im, (cx, cy))
                dedup[key] = (sh_idx, fr_idx)
            frames_out.append((sh_idx, fr_idx, im.width, im.height, ox, oy))
            im.close()

        anims_out.append((name, first_frame, len(imgs), bx, by, bw, bh))

    for s in sheets:
        out_path = os.path.join(OUT_ASSETS, s["fname"])
        s["img"].save(out_path)
        print(f"Saved {out_path}: {s['count']}/{s['cap']} cells ({s['cell']}x{s['cell']})")

    # Copy backgrounds and pre-tile small area BGs so scenes need ~98 sprites instead of ~1,107
    bg_src = os.path.join(GD_ROOT, "backgrounds")
    for f in sorted(glob.glob(os.path.join(bg_src, "*.png"))):
        shutil.copy2(f, os.path.join(OUT_ASSETS, "bg", os.path.basename(f)))

    bg_tiles = [
        ("bg_cave.png", "bg_cave_tile.png", 2, 2),       # 64x64 -> 128x128
        ("bg_trees.png", "bg_trees_tile.png", 8, 4),     # 16x48 -> 128x192
        ("bg_sky.png", "bg_sky_tile.png", 2, 4),         # 96x32 -> 192x128
        ("bg_temple.png", "bg_temple_tile.png", 4, 4),   # 32x32 -> 128x128
    ]
    for src_name, dst_name, nx, ny in bg_tiles:
        src_p = os.path.join(OUT_ASSETS, "bg", src_name)
        if os.path.isfile(src_p):
            im = Image.open(src_p).convert("RGBA")
            tiled = Image.new("RGBA", (im.width * nx, im.height * ny), (0, 0, 0, 0))
            for ty in range(ny):
                for tx in range(nx):
                    tiled.paste(im, (tx * im.width, ty * im.height))
            tiled.save(os.path.join(OUT_ASSETS, "bg", dst_name))
            im.close()
            tiled.close()

    # Copy sounds and generate .wav.import with compress/mode=0 (PCM uncompressed)
    snd_src = os.path.join(GD_ROOT, "sounds")
    wav_count = 0
    ogg_count = 0
    for f in sorted(os.listdir(snd_src)):
        full = os.path.join(snd_src, f)
        if not os.path.isfile(full):
            continue
        dst = os.path.join(OUT_ASSETS, "sounds", f)
        if f.endswith(".wav"):
            shutil.copy2(full, dst)
            wav_count += 1
        elif f.endswith(".ogg"):
            shutil.copy2(full, dst)
            ogg_count += 1

    print(f"Copied {wav_count} WAVs, {ogg_count} OGGs, {len(large_copied)} large sprites")
    print(f"Packed {len(anims_out)} animations, {len(frames_out)} frame refs ({len(dedup)} unique cells)")

    # Write C header & source
    seen_ids = {}
    with open(OUT_C_HDR, "w", encoding="utf-8", newline="\n") as hf:
        hf.write("/* Auto-generated by tools/pack_spelunky_assets.py. Do not edit by hand. */\n")
        hf.write("#ifndef SPELUNKY_SPRITES_H\n")
        hf.write("#define SPELUNKY_SPRITES_H\n\n")
        hf.write("#include <stdint.h>\n\n")
        hf.write("enum {\n")
        for i, s in enumerate(sheets):
            hf.write(f"    {s['id']} = {i},\n")
        hf.write(f"    SP_SHEET_COUNT = {len(sheets)}\n")
        hf.write("};\n\n")
        hf.write("typedef struct {\n")
        hf.write("    uint8_t  sheet;\n")
        hf.write("    uint16_t frame;\n")
        hf.write("    uint16_t w, h;\n")
        hf.write("    int16_t  ox, oy;\n")
        hf.write("} SpelunkyFrame;\n\n")
        hf.write("typedef struct {\n")
        hf.write("    uint16_t first_frame;\n")
        hf.write("    uint16_t frame_count;\n")
        hf.write("    int16_t  bx, by, bw, bh;\n")
        hf.write("} SpelunkyAnim;\n\n")
        hf.write("enum {\n")
        for idx, (name, *_rest) in enumerate(anims_out):
            ident = "SP_ANIM_" + sanitize_ident(name)
            if ident in seen_ids:
                ident = f"{ident}_{idx}"
            seen_ids[ident] = idx
            hf.write(f"    {ident} = {idx},\n")
        hf.write(f"    SP_ANIM_COUNT = {len(anims_out)}\n")
        hf.write("};\n\n")
        hf.write("extern const SpelunkyFrame g_sp_frames[];\n")
        hf.write("extern const SpelunkyAnim  g_sp_anims[SP_ANIM_COUNT];\n\n")
        hf.write("#endif\n")

    with open(OUT_C_SRC, "w", encoding="utf-8", newline="\n") as cf:
        cf.write("/* Auto-generated by tools/pack_spelunky_assets.py. Do not edit by hand. */\n")
        cf.write('#include "spelunky_sprites.h"\n\n')
        cf.write("const SpelunkyFrame g_sp_frames[] = {\n")
        for sh_idx, fr_idx, w, h, ox, oy in frames_out:
            cf.write(f"    {{ {sh_idx}, {fr_idx}, {w}, {h}, {ox}, {oy} }},\n")
        cf.write("};\n\n")
        cf.write("const SpelunkyAnim g_sp_anims[SP_ANIM_COUNT] = {\n")
        for _name, ff, fc, bx, by, bw, bh in anims_out:
            cf.write(f"    {{ {ff}, {fc}, {bx}, {by}, {bw}, {bh} }},\n")
        cf.write("};\n")


if __name__ == "__main__":
    main()
