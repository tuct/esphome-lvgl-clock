#!/usr/bin/env python3
"""Render a lvgl_clock style to an animated GIF.

Runs the headless `gifgen` binary (build it first with ./build.sh) to dump one
PPM per frame, then assembles them into a looping GIF with Pillow. The frames
are the *real* component output rendered by desktop LVGL - pixel-identical to
the device.

  python3 make_gif.py --style clockclock24 --width 480 --height 320 \
      --frames 120 --dt 33 --out clockclock24.gif
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.environ.get("BUILD_DIR", os.path.join(HERE, "build"))
BIN = os.path.join(BUILD_DIR, "gifgen")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--style", default="clockclock24")
    ap.add_argument("--width", type=int, default=480)
    ap.add_argument("--height", type=int, default=320)
    ap.add_argument("--frames", type=int, default=120)
    ap.add_argument("--dt", type=int, default=33, help="ms per frame (virtual clock step)")
    ap.add_argument("--fps", type=int, default=0, help="GIF playback fps (default: 1000/dt)")
    ap.add_argument("--scale", type=float, default=1.0, help="downscale factor for the GIF")
    ap.add_argument("--colors", type=int, default=64, help="GIF palette size")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    if not os.path.exists(BIN):
        sys.exit(f"binary not found: {BIN}\n  build it first:  ./build.sh")

    frames_dir = tempfile.mkdtemp(prefix="gifgen_frames_")
    try:
        subprocess.run(
            [BIN, args.style, str(args.width), str(args.height),
             str(args.frames), str(args.dt), frames_dir],
            check=True,
        )
        paths = sorted(
            os.path.join(frames_dir, f)
            for f in os.listdir(frames_dir) if f.endswith(".ppm")
        )
        if not paths:
            sys.exit("no frames produced")

        imgs = []
        for p in paths:
            im = Image.open(p).convert("RGB")
            if args.scale != 1.0:
                im = im.resize(
                    (int(im.width * args.scale), int(im.height * args.scale)),
                    Image.LANCZOS,
                )
            # quantize per frame to a compact, consistent palette
            imgs.append(im.quantize(colors=args.colors, method=Image.MEDIANCUT))

        fps = args.fps or max(1, round(1000 / args.dt))
        duration = round(1000 / fps)
        imgs[0].save(
            args.out,
            save_all=True,
            append_images=imgs[1:],
            duration=duration,
            loop=0,
            optimize=True,
            disposal=2,
        )
        size_kb = os.path.getsize(args.out) / 1024
        print(f"wrote {args.out}  ({len(imgs)} frames, {size_kb:.0f} KB, {fps} fps)")
    finally:
        shutil.rmtree(frames_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
