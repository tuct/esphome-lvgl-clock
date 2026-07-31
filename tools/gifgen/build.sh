#!/usr/bin/env bash
# Builds the headless gifgen binary: compiles desktop LVGL to a cached static
# lib, then the real lvgl_clock.cpp + the harness against the shim tree.
#
#   BUILD_DIR=/path ./build.sh        # override where objects/lib/binary go
#
# Requires: clang++ (no cmake/sdl2/ffmpeg). LVGL source is taken from the
# examples' .esphome build tree.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"                # repo root
COMPONENT="$ROOT/components/lvgl_clock"
SHIM="$HERE/shim"
CONFDIR="$HERE"                                  # holds lv_conf.h

# LVGL source, from any esphome build tree the examples produced.
LV="$(ls -d "$ROOT"/examples/.esphome/build/*/managed_components/lvgl__lvgl 2>/dev/null | head -1)"
if [[ -z "${LV:-}" || ! -f "$LV/lvgl.h" ]]; then
  echo "error: LVGL source not found under examples/.esphome/build/*/managed_components/lvgl__lvgl" >&2
  echo "       run 'esphome config examples/example_analog.yaml' once to populate it." >&2
  exit 1
fi

BUILD_DIR="${BUILD_DIR:-$HERE/build}"
OBJ="$BUILD_DIR/lvgl_obj"
LIB="$BUILD_DIR/liblvgl_host.a"
mkdir -p "$OBJ"

CONF_FLAGS=(-DLV_CONF_INCLUDE_SIMPLE "-I$CONFDIR" "-I$LV")

# --- LVGL static lib (cached; delete $LIB to force a rebuild) ----------------
if [[ ! -f "$LIB" ]]; then
  echo ">> compiling LVGL for the host (one-time, ~1-2 min)..."
  # Pure-C only; the .cpp files under src/libs are optional third-party
  # (thorvg etc.) and disabled in lv_conf, so we skip them.
  find "$LV/src" -name '*.c' -print0 | \
    xargs -0 -P "$(sysctl -n hw.ncpu)" -I{} bash -c '
      src="$1"; obj="$2/$(echo "$1" | md5).o"; shift 2
      clang -std=c11 -O2 -w "$@" -c "$src" -o "$obj"
    ' _ {} "$OBJ" "${CONF_FLAGS[@]}"
  ar rcs "$LIB" "$OBJ"/*.o
  echo ">> LVGL lib built: $LIB"
fi

# --- component + harness -----------------------------------------------------
echo ">> compiling lvgl_clock + harness..."
CXXFLAGS=(-std=gnu++17 -O2 -w "-I$SHIM" "-I$COMPONENT" "${CONF_FLAGS[@]}")
clang++ "${CXXFLAGS[@]}" -c "$COMPONENT/lvgl_clock.cpp" -o "$BUILD_DIR/lvgl_clock.o"
clang++ "${CXXFLAGS[@]}" -c "$HERE/main.cpp" -o "$BUILD_DIR/main.o"
clang++ -O2 "$BUILD_DIR/lvgl_clock.o" "$BUILD_DIR/main.o" "$LIB" -o "$BUILD_DIR/gifgen"
echo ">> built $BUILD_DIR/gifgen"
