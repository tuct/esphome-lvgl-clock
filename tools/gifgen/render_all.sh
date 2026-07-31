#!/usr/bin/env bash
# Build the harness and render a GIF for every style into ../../docs/.
# Per-style size/length/palette are tuned for a good README look.
#
#   ./render_all.sh            # all styles
#   ./render_all.sh digital    # just one (or several) styles
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOCS="$(cd "$HERE/../.." && pwd)/docs"
mkdir -p "$DOCS"

"$HERE/build.sh"

gif() { python3 "$HERE/make_gif.py" "$@"; }

# style           W    H   frames dt  scale colors  ->  out
render() {
  local style="$1"
  case "$style" in
    clockclock24) gif --style clockclock24 --width 480 --height 320 --frames 280 --dt 45 --scale 0.5 --colors 32 --out "$DOCS/clockclock24.gif" ;;
    analog)       gif --style analog       --width 320 --height 320 --frames 180 --dt 40 --scale 0.6 --colors 48 --out "$DOCS/analog.gif" ;;
    digital)      gif --style digital      --width 480 --height 320 --frames 160 --dt 50 --scale 0.5 --colors 24 --out "$DOCS/digital.gif" ;;
    flipclock)    gif --style flipclock    --width 480 --height 320 --frames 200 --dt 40 --scale 0.5 --colors 48 --out "$DOCS/flipclock.gif" ;;
    digital_12h)  gif --style digital_12h  --width 480 --height 320 --frames 220 --dt 45 --scale 0.5 --colors 24 --out "$DOCS/digital_12h.gif" ;;
    flipclock_12h) gif --style flipclock_12h --width 480 --height 320 --frames 240 --dt 40 --scale 0.5 --colors 48 --out "$DOCS/flipclock_12h.gif" ;;
    seg_matrix)   gif --style seg_matrix   --width 960 --height 240 --frames 240 --dt 45 --scale 0.45 --colors 24 --out "$DOCS/seg_matrix.gif" ;;
    *) echo "unknown style: $style" >&2; return 1 ;;
  esac
}

styles=("$@")
if [[ ${#styles[@]} -eq 0 ]]; then
  styles=(clockclock24 analog digital flipclock digital_12h flipclock_12h seg_matrix)
fi
for s in "${styles[@]}"; do
  echo ">> rendering $s"
  render "$s"
done
echo ">> all done -> $DOCS"
