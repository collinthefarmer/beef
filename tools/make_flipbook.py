#!/usr/bin/env python3
"""Bake an EFSH fill texture into scrolled flipbook frames for the plugin.

Frame i is the fill texture shifted by i/N of one tile along the scroll axis,
then tiled by the EFSH texture scale and resampled to the output size. Because
the fill texture is tileable, shifting before tiling keeps every frame seamless.

Output: <out>/Textures/<plugin>/<name>/frame_<i>.dds with a full mip
chain, written by this script (no texconv needed): BC1/DXT1 by default (needs
numpy; the shader reads only RGB), or uncompressed BGRA8 with --format bgra8.
Pillow reads the source (DDS incl. DXT1/3/5, PNG, TGA, ...).

Frame count sets smoothness: the step between frames is (tile texels / N), so
a 512 px frame at texture scale 3 needs about 128 frames for ~1.3 texel steps.

On NixOS run it as
  nix-shell -p 'python313.withPackages (ps: with ps; [numpy pillow])' --run 'python tools/make_flipbook.py ...'

Sampling convention: the membrane shader samples at uv + offset with offset
growing over time, so frame i corresponds to offset i/N and the image content
moves toward -u (left) / -v (up). When an EFSH scrolls on both axes the
plugin uses the U offset only; bake with --axis u in that case.
"""

from __future__ import annotations

import argparse
import math
import struct
import sys
from pathlib import Path

PLUGIN_NAME = "BetterEnchantmentEffects"

try:
    from PIL import Image, ImageChops
except ImportError:
    sys.exit("Pillow is required: pip install pillow")

DDS_MAGIC = b"DDS "
DDSD_CAPS = 0x1
DDSD_HEIGHT = 0x2
DDSD_WIDTH = 0x4
DDSD_PITCH = 0x8
DDSD_PIXELFORMAT = 0x1000
DDSD_MIPMAPCOUNT = 0x20000
DDSD_LINEARSIZE = 0x80000
DDPF_ALPHAPIXELS = 0x1
DDPF_FOURCC = 0x4
DDPF_RGB = 0x40
DDSCAPS_COMPLEX = 0x8
DDSCAPS_TEXTURE = 0x1000
DDSCAPS_MIPMAP = 0x400000

def mip_chain(image: Image.Image) -> list[Image.Image]:
    mips = [image]
    while mips[-1].width > 1 or mips[-1].height > 1:
        prev = mips[-1]
        mips.append(prev.resize((max(1, prev.width // 2), max(1, prev.height // 2)), Image.LANCZOS))
    return mips

def dds_header(width: int, height: int, mip_count: int, pixel_format: bytes, size_flag: int, pitch_or_size: int) -> bytes:
    header = struct.pack(
        "<IIIIIII44s32sIIII4s",
        124,
        DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | size_flag | DDSD_PIXELFORMAT | DDSD_MIPMAPCOUNT,
        height,
        width,
        pitch_or_size,
        0,
        mip_count,
        b"\0" * 44,
        pixel_format,
        DDSCAPS_COMPLEX | DDSCAPS_TEXTURE | DDSCAPS_MIPMAP,
        0,
        0,
        0,
        b"\0\0\0\0",
    )
    assert len(header) == 124
    return DDS_MAGIC + header

def write_dds_bgra8(path: Path, image: Image.Image) -> None:
    mips = mip_chain(image.convert("RGBA"))
    pixel_format = struct.pack("<II4sIIIII", 32, DDPF_RGB | DDPF_ALPHAPIXELS, b"\0\0\0\0", 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as f:
        f.write(dds_header(image.width, image.height, len(mips), pixel_format, DDSD_PITCH, image.width * 4))
        for mip in mips:
            f.write(mip.tobytes("raw", "BGRA"))

def encode_bc1(image: Image.Image) -> bytes:
    """Opaque BC1: endpoints from the extremes along the block's dominant colour axis."""
    import numpy as np

    rgb = np.asarray(image.convert("RGB"), dtype=np.float32)
    h, w = rgb.shape[:2]
    bw, bh = (w + 3) // 4, (h + 3) // 4
    padded = np.zeros((bh * 4, bw * 4, 3), dtype=np.float32)
    padded[:h, :w] = rgb
    blocks = padded.reshape(bh, 4, bw, 4, 3).transpose(0, 2, 1, 3, 4).reshape(-1, 16, 3)

    mean = blocks.mean(axis=1, keepdims=True)
    centered = blocks - mean
    cov = np.einsum("bij,bik->bjk", centered, centered)
    axis = np.ones((blocks.shape[0], 3), dtype=np.float32)
    for _ in range(8):
        axis = np.einsum("bjk,bk->bj", cov, axis)
        norm = np.linalg.norm(axis, axis=1, keepdims=True)
        axis = np.where(norm > 1e-6, axis / np.maximum(norm, 1e-6), np.array([1.0, 1.0, 1.0], dtype=np.float32) / np.sqrt(3))
    proj = np.einsum("bij,bj->bi", centered, axis)
    hi = blocks[np.arange(blocks.shape[0]), proj.argmax(axis=1)]
    lo = blocks[np.arange(blocks.shape[0]), proj.argmin(axis=1)]

    def to565(c):
        r = np.clip(np.rint(c[:, 0] / 255 * 31), 0, 31).astype(np.uint16)
        g = np.clip(np.rint(c[:, 1] / 255 * 63), 0, 63).astype(np.uint16)
        b = np.clip(np.rint(c[:, 2] / 255 * 31), 0, 31).astype(np.uint16)
        return (r << 11) | (g << 5) | b

    def from565(v):
        r = ((v >> 11) & 31).astype(np.float32) * (255 / 31)
        g = ((v >> 5) & 63).astype(np.float32) * (255 / 63)
        b = (v & 31).astype(np.float32) * (255 / 31)
        return np.stack([r, g, b], axis=1)

    c0, c1 = to565(hi), to565(lo)
    swap = c0 < c1
    c0, c1 = np.where(swap, c1, c0), np.where(swap, c0, c1)
    p0, p1 = from565(c0), from565(c1)
    palette = np.stack([p0, p1, (2 * p0 + p1) / 3, (p0 + 2 * p1) / 3], axis=1)
    dist = ((blocks[:, :, None, :] - palette[:, None, :, :]) ** 2).sum(axis=3)
    idx = dist.argmin(axis=2).astype(np.uint32)
    equal = c0 == c1
    idx[equal] = 0
    shifts = (np.arange(16, dtype=np.uint32) * 2)[None, :]
    packed = (idx << shifts).sum(axis=1).astype(np.uint32)
    out = np.empty((blocks.shape[0], 8), dtype=np.uint8)
    out[:, 0:2] = c0.astype("<u2").view(np.uint8).reshape(-1, 2)
    out[:, 2:4] = c1.astype("<u2").view(np.uint8).reshape(-1, 2)
    out[:, 4:8] = packed.astype("<u4").view(np.uint8).reshape(-1, 4)
    return out.tobytes()

def write_dds_bc1(path: Path, image: Image.Image) -> None:
    mips = mip_chain(image.convert("RGBA"))
    pixel_format = struct.pack("<II4sIIIII", 32, DDPF_FOURCC, b"DXT1", 0, 0, 0, 0, 0)
    linear_size = max(1, (image.width + 3) // 4) * max(1, (image.height + 3) // 4) * 8
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as f:
        f.write(dds_header(image.width, image.height, len(mips), pixel_format, DDSD_LINEARSIZE, linear_size))
        for mip in mips:
            f.write(encode_bc1(mip))

def tile_and_resize(tile: Image.Image, scale_u: float, scale_v: float, out_size: tuple[int, int]) -> Image.Image:
    reps_u = math.ceil(scale_u)
    reps_v = math.ceil(scale_v)
    canvas = Image.new("RGBA", (tile.width * reps_u, tile.height * reps_v))
    for y in range(reps_v):
        for x in range(reps_u):
            canvas.paste(tile, (x * tile.width, y * tile.height))
    crop_w = max(1, round(tile.width * scale_u))
    crop_h = max(1, round(tile.height * scale_v))
    canvas = canvas.crop((0, 0, crop_w, crop_h))
    return canvas.resize(out_size, Image.LANCZOS)

def apply_palette(tile: Image.Image, palette_path: Path) -> Image.Image:
    """Greyscale-to-palette-colour (EFSH flag bit 1): each texel's grey looks up
    the palette gradient along U (middle row), which becomes its RGB."""
    palette = Image.open(palette_path).convert("RGB")
    row = palette.height // 2
    lut = [palette.getpixel((min(palette.width - 1, round(g / 255 * (palette.width - 1))), row)) for g in range(256)]
    grey = tile.convert("L")
    out = Image.new("RGBA", tile.size)
    alpha = tile.getchannel("A")
    r = grey.point([lut[g][0] for g in range(256)])
    gch = grey.point([lut[g][1] for g in range(256)])
    b = grey.point([lut[g][2] for g in range(256)])
    out = Image.merge("RGBA", (r, gch, b, alpha))
    return out

def build_frames(tile: Image.Image, frames: int, axis: str, scale_u: float, scale_v: float, out_size: tuple[int, int]) -> list[Image.Image]:
    result = []
    for i in range(frames):
        fraction = i / frames
        if axis == "u":
            shifted = ImageChops.offset(tile, -round(fraction * tile.width), 0)
        else:
            shifted = ImageChops.offset(tile, 0, -round(fraction * tile.height))
        result.append(tile_and_resize(shifted, scale_u, scale_v, out_size))
    return result

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("texture", type=Path, help="EFSH fill texture (DDS/PNG/TGA)")
    parser.add_argument("--out", type=Path, default=Path("dist") / PLUGIN_NAME, help="mod root to write Textures/ under (default: dist/<plugin>)")
    parser.add_argument("--name", help="flipbook directory name (default: lowercase file stem of the texture, which is what the plugin looks up)")
    parser.add_argument("--frames", type=int, default=128, help="frame count N (default 128)")
    parser.add_argument("--format", choices=("bc1", "bgra8"), default="bc1", help="DDS format (default bc1; bgra8 needs no numpy)")
    parser.add_argument("--axis", choices=("u", "v"), default="u", help="scroll axis to bake (default u)")
    parser.add_argument("--scale-u", type=float, default=1.0, help="EFSH Fill Texture Scale U")
    parser.add_argument("--scale-v", type=float, default=1.0, help="EFSH Fill Texture Scale V")
    parser.add_argument("--max-size", type=int, default=512, help="cap on output width/height (default 512)")
    parser.add_argument("--palette", type=Path, help="EFSH membrane palette (NAM8) for records with Greyscale To Palette Color; bakes the tint into the frames. Pair with --name <plugin>~<id> so only that record uses them")
    args = parser.parse_args()

    if args.frames < 1 or args.frames > 256:
        parser.error("--frames must be 1..256 (the plugin stops scanning at 256)")
    if args.scale_u <= 0 or args.scale_v <= 0:
        parser.error("scales must be positive")

    tile = Image.open(args.texture).convert("RGBA")
    if args.palette:
        tile = apply_palette(tile, args.palette)
    out_w = min(args.max_size, tile.width)
    out_h = min(args.max_size, tile.height)
    name = args.name or args.texture.stem.lower()
    out_dir = args.out / "Textures" / PLUGIN_NAME / name

    frames = build_frames(tile, args.frames, args.axis, args.scale_u, args.scale_v, (out_w, out_h))
    lum = frames[0].convert("L")
    hist = lum.histogram()
    mean_luminance = sum(i * n for i, n in enumerate(hist)) / max(1, sum(hist)) / 255.0
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "meta.ini").write_text(f"luminance={mean_luminance:.4f}\nframes={args.frames}\naxis={args.axis}\nscale={args.scale_u}x{args.scale_v}\nsource={args.texture.name}\n")

    writer = write_dds_bc1 if args.format == "bc1" else write_dds_bgra8
    for existing in out_dir.glob("frame_*.dds"):
        existing.unlink()
    for i, frame in enumerate(frames):
        writer(out_dir / f"frame_{i}.dds", frame)

    print(f"wrote {len(frames)} {args.format} frames ({out_w}x{out_h}, axis {args.axis}, scale {args.scale_u}x{args.scale_v}, mean luminance {mean_luminance:.3f}) to {out_dir}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
