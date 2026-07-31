# gifgen — render lvgl_clock styles to GIFs

Generates animated GIFs of the clock **by running the real `lvgl_clock`
rendering code** against desktop LVGL — no ESPHome, no device, no SDL. The
component's `lvgl_clock.cpp` is compiled unchanged, so frames are
pixel-identical to the hardware (same LVGL rasterizer, caps, anti-aliasing).

## How it works

1. `main.cpp` is a tiny headless harness: it `lv_init()`s, creates a canvas
   exactly like the ESPHome codegen does, drives the clock frame-by-frame off a
   **virtual clock** (deterministic, so animations loop cleanly), and dumps one
   PPM per frame — reading straight from the canvas buffer (the display is never
   flushed, so no SDL/window is needed).
2. The `shim/` tree provides the ~6 ESPHome symbols the component references
   (`Color`, `Component`, `millis()`, no-op logging, `RealTimeClock`/`ESPTime`,
   `LvCompound`) so it links without pulling in ESPHome.
3. `make_gif.py` runs the binary and stitches the frames into a looping GIF
   with Pillow.

LVGL itself is compiled from the source in the examples' `.esphome`
build tree, using a host `lv_conf.h` (a copy of the ESPHome one with the custom
allocator swapped for the C library).

## Requirements

- `clang++` (already on macOS). **No** cmake, sdl2, or ffmpeg.
- Python **Pillow** (`pip install pillow`).
- The component built at least once so LVGL source exists:
  `esphome config examples/example_analog.yaml`.

## Usage

```bash
cd tools/gifgen
./build.sh                     # one-time ~5s (LVGL lib is cached afterwards)
python3 make_gif.py --style clockclock24 --width 480 --height 320 \
    --frames 150 --dt 33 --out out/clockclock24.gif
```

Options: `--style` (`clockclock24` | `analog` | `digital`), `--width/--height`,
`--frames`, `--dt` (ms/frame = virtual clock step), `--fps` (playback, default
`1000/dt`), `--scale` (downscale the GIF), `--colors` (palette size), `--out`.

Build artifacts (the LVGL lib, objects, binary) go to `build/` by default, or
`$BUILD_DIR` if set. `./build.sh` caches `liblvgl_host.a` — delete it to force a
full LVGL rebuild.

## Per-style look

The harness sets sensible demo parameters per style in `main.cpp` (e.g.
clockclock24 runs in `demo` mode with the `long` sweep). Tweak that `if
(style == ...)` block to change colours, movement, ticks, etc. Only
clockclock24/analog/digital are wired so far; add a branch for others.
