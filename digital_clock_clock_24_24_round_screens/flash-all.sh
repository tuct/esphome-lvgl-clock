#!/usr/bin/env bash
#
# Build and flash the whole ClockClock 24 wall, one board at a time.
# macOS and Linux. Written for bash 3.2, which is what macOS still ships.
#
# Eight boards, and only one of them has OTA. The other seven go over USB, which
# means moving the lead between each - so this compiles everything first, then
# walks you through the uploads with a pause at each board.
#
# Two things it does deliberately:
#
#   COMPILE EVERYTHING FIRST.  A compile error found halfway through flashing
#   leaves you with a wall running two firmwares and a board in your hand. All
#   eight are built before anything is uploaded.
#
#   THE ORDER YOU ASK FOR IS THE ORDER IT USES.  a,b,c,d,e,f,g,h by default -
#   the order the boards sit on the wall, which is the order you have them in
#   your hand.
#
# When the order DOES matter: updating a wall that stays powered while you work
# through it. The mode is an integer on the sync bus and new modes are appended,
# so a NEWER master can broadcast a mode an OLDER listener does not know, and
# that listener silently drops the mode field and holds its last animation. The
# other way round is harmless. If that is what you are doing, put the master
# last: `-b a,b,c,e,f,g,h,d`. Flashing everything in one go with the wall down -
# or with the modules out of their carriers - has no such window.
#
# Usage:
#   ./flash-all.sh                      # all eight, over USB, in order
#   ./flash-all.sh -b a,b,c             # just these
#   ./flash-all.sh -b a,b,c,e,f,g,h,d   # listeners first, master last
#   ./flash-all.sh -p /dev/ttyACM0      # skip the port prompt
#   ./flash-all.sh --build-only         # check a change compiles for all eight
#   ./flash-all.sh --skip-build         # upload what is already built
#   ./flash-all.sh -m cc24-board-d.local  # master over the network, not USB
#   ./flash-all.sh -m 192.168.1.42        # ...by IP instead of mDNS

set -euo pipefail
# `readlink -f` is GNU-only; BSD readlink on macOS has no -f. dirname of the
# invocation path is enough for a script that lives beside its configs.
cd "$(dirname "${BASH_SOURCE[0]}")"

BOARDS="a,b,c,d,e,f,g,h"
# Empty = the master goes over USB like everything else, which is what you want
# when the modules are out of the wall in front of you. -m opts into OTA.
MASTER_HOST=""
PORT=""
BUILD_ONLY=0
SKIP_BUILD=0

while [ $# -gt 0 ]; do
  case "$1" in
    -b|--boards)      BOARDS="$2"; shift 2 ;;
    -p|--port)        PORT="$2"; shift 2 ;;
    -m|--master-host) MASTER_HOST="$2"; shift 2 ;;
    --build-only)     BUILD_ONLY=1; shift ;;
    --skip-build)     SKIP_BUILD=1; shift ;;
    -h|--help)        sed -n '2,30p' "$0" | sed 's/^# \?//'; exit 0 ;;
    *) echo "Unknown option: $1  (try --help)" >&2; exit 2 ;;
  esac
done

# Board letter -> column and clock indices. Printed as we go, because a board
# flashed into the wrong column is the one mistake that looks like a hardware
# fault: the wall lights up, the time is simply wrong.
board_info() {
  case "$1" in
    a) echo "column 0, clocks 0 / 2 / 4" ;;
    b) echo "column 1, clocks 1 / 3 / 5" ;;
    c) echo "column 2, clocks 6 / 8 / 10" ;;
    d) echo "column 3, clocks 7 / 9 / 11  [MASTER]" ;;
    e) echo "column 4, clocks 12 / 14 / 16" ;;
    f) echo "column 5, clocks 13 / 15 / 17" ;;
    g) echo "column 6, clocks 18 / 20 / 22" ;;
    h) echo "column 7, clocks 19 / 21 / 23" ;;
    *) echo "unknown board '$1'" >&2; return 1 ;;
  esac
}

# The wall, drawn as you are actually looking at it: from BEHIND, where the USB
# sockets are - so board A is on the RIGHT. Columns are zero-based (A = 0), and
# "column 4" read as the fourth one along lands on D, which is exactly the kind
# of thing a picture settles and a number does not.
BACK_ORDER="h g f e d c b a"

col_of() {
  case "$1" in
    a) echo 0 ;; b) echo 1 ;; c) echo 2 ;; d) echo 3 ;;
    e) echo 4 ;; f) echo 5 ;; g) echo 6 ;; h) echo 7 ;;
  esac
}

wall_map() {
  local target="$1" col row L c cell line head pad k=0 tcol=""
  head="          "
  for L in $BACK_ORDER; do
    head="$head $(echo "$L" | tr '[:lower:]' '[:upper:]')   "
    [ "$L" = "$target" ] && tcol=$k
    k=$((k + 1))
  done
  printf '\n        %sthe wall from BEHIND - the side you plug into%s\n\n' "$C_DIM" "$C_OFF"
  printf '%s\n' "$head"
  printf '%s\n' "         ┌────┬────┬────┬────┬────┬────┬────┬────┐"
  for row in 0 1 2; do
    line="    row${row} │"
    for L in $BACK_ORDER; do
      col="$(col_of "$L")"
      c=$(( (col / 2) * 6 + row * 2 + (col % 2) ))
      if [ "$L" = "$target" ]; then
        # Reverse video on a terminal, *nn* when piped - the marker has to
        # survive being pasted into an issue.
        if [ -n "$C_REV" ]; then cell="$(printf '%s %2d %s' "$C_REV" "$c" "$C_OFF")"
        else                     cell="$(printf '*%2d*' "$c")"; fi
      else
        cell="$(printf ' %2d ' "$c")"
      fi
      line="${line}${cell}│"
    done
    printf '%s\n' "$line"
  done
  printf '%s\n' "         └────┴────┴────┴────┴────┴────┴────┴────┘"
  if [ -n "$tcol" ]; then
    pad="$(printf '%*s' $((11 + tcol * 5)) '')"
    printf '%s└─ BOARD %s\n\n' "$pad" "$(echo "$target" | tr '[:lower:]' '[:upper:]')"
  fi
}

if [ -t 1 ]; then
  C_STEP=$'\e[36m'; C_OK=$'\e[32m'; C_WARN=$'\e[33m'; C_DIM=$'\e[90m'; C_OFF=$'\e[0m'
  C_REV=$'\e[7m'
else
  C_STEP=""; C_OK=""; C_WARN=""; C_DIM=""; C_OFF=""
  C_REV=""
fi
step() { printf '\n%s=== %s%s\n' "$C_STEP" "$1" "$C_OFF"; }
# mm:ss. Compiling eight boards is minutes, not seconds, and "has it hung?" is
# the question these numbers exist to answer.
fmt_dur() { printf '%dm%02ds' $(( $1 / 60 )) $(( $1 % 60 )); }
ok()   { printf '%s    %s%s\n'   "$C_OK"   "$1" "$C_OFF"; }
warn() { printf '%s    %s%s\n'   "$C_WARN" "$1" "$C_OFF"; }
dim()  { printf '%s    %s%s\n'   "$C_DIM"  "$1" "$C_OFF"; }

command -v esphome >/dev/null 2>&1 || {
  echo "esphome is not on PATH. Install it with:  pip install esphome" >&2; exit 1; }
ok "$(esphome version 2>&1 | head -1)"

# No reordering. An order that quietly differs from the one you typed is worse
# than one that is occasionally wrong - see the note at the top for when it
# matters.
IFS=',' read -r -a REQUESTED <<< "$BOARDS"
ORDER=()
WANT_MASTER=0
for b in "${REQUESTED[@]}"; do
  b="$(echo "$b" | tr -d '[:space:]' | tr '[:upper:]' '[:lower:]')"
  [ -z "$b" ] && continue
  board_info "$b" >/dev/null || exit 2
  [ "$b" = "d" ] && WANT_MASTER=1
  ORDER+=("$b")
done
[ ${#ORDER[@]} -eq 0 ] && { echo "No boards selected." >&2; exit 2; }

# ------------------------------------------------------------------ build ---
# Phase numbering adapts, so --skip-build does not announce a phase 1 that
# never runs.
PHASES=2
[ "$SKIP_BUILD" -eq 1 ] && PHASES=1
[ "$BUILD_ONLY" -eq 1 ] && PHASES=1
PHASE=0
RUN_START=$SECONDS
COMPILE_SECS=0
UPLOAD_SECS=0
UPLOADS=0

if [ "$SKIP_BUILD" -eq 0 ]; then
  PHASE=$((PHASE + 1))
  step "Phase $PHASE/$PHASES - compiling ${#ORDER[@]} board(s)"
  dim "Nothing is uploaded until they all build."
  n=0
  phase_start=$SECONDS
  for b in "${ORDER[@]}"; do
    n=$((n + 1))
    printf '\n  [%d/%d] compiling board_%s.yaml  (%s)\n' \
           "$n" "${#ORDER[@]}" "$b" "$(board_info "$b")"
    t0=$SECONDS
    esphome compile "board_$b.yaml"
    ok "[$n/${#ORDER[@]}] board_$b built in $(fmt_dur $((SECONDS - t0)))"
  done
  COMPILE_SECS=$((SECONDS - phase_start))
  ok "All ${#ORDER[@]} compiled in $(fmt_dur $COMPILE_SECS)."
fi
if [ "$BUILD_ONLY" -eq 1 ]; then
  step "Finished in $(fmt_dur $((SECONDS - RUN_START))) - --build-only, nothing uploaded"
  exit 0
fi

# Pick a serial port: use the only one if there is only one, else ask. An empty
# answer is fine - esphome will choose.
pick_port() {
  [ -n "$PORT" ] && { echo "$PORT"; return; }
  local found=()
  # macOS calls them /dev/cu.*; Linux /dev/ttyUSB* or /dev/ttyACM*. Use cu.
  # rather than tty. on macOS: tty. blocks on open waiting for carrier detect,
  # which a USB-serial bridge never asserts.
  for dev in /dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.wchusbserial* \
             /dev/cu.SLAB_USBtoUART* /dev/ttyUSB* /dev/ttyACM*; do
    [ -e "$dev" ] && found+=("$dev")
  done
  if [ ${#found[@]} -eq 1 ]; then
    warn "Using ${found[0]} (the only port present)" >&2
    echo "${found[0]}"; return
  fi
  if [ ${#found[@]} -eq 0 ]; then
    warn "No serial ports found - is the board plugged in?" >&2
  else
    warn "Ports: ${found[*]}" >&2
  fi
  local answer
  read -r -p "    Serial port (blank to let esphome choose): " answer </dev/tty
  echo "$answer"
}

# ----------------------------------------------------------------- upload ---
# Everything over USB, in the order given. The master is only different when
# -m gave it a host: then it goes over the network wherever it falls in the
# order, and needs no lead moved.
UPLOADS=${#ORDER[@]}
PHASE=$((PHASE + 1))
step "Phase $PHASE/$PHASES - uploading $UPLOADS board(s)"
if [ -n "$MASTER_HOST" ] && [ "$WANT_MASTER" -eq 1 ]; then
  dim "Board D goes over the network ($MASTER_HOST); the rest over USB."
else
  warn "All over USB - the listeners have no OTA, which is the trade for carrying no Wi-Fi stack."
fi
# Only worth saying when the wall could actually be live: the master is being
# updated before boards that come after it in the order.
if [ "$WANT_MASTER" -eq 1 ] && [ "${ORDER[$((${#ORDER[@]} - 1))]}" != "d" ]; then
  dim "Master is not last. Fine when the wall is down or the modules are out;"
  dim "on a live wall use  -b a,b,c,e,f,g,h,d  so no listener is older than it."
fi
u=0
upload_start=$SECONDS

for b in "${ORDER[@]}"; do
  u=$((u + 1))
  printf '\n  [%d/%d] BOARD %s - %s\n' \
         "$u" "$UPLOADS" "$(echo "$b" | tr '[:lower:]' '[:upper:]')" "$(board_info "$b")"
  wall_map "$b"
  t0=$SECONDS
  if [ "$b" = "d" ] && [ -n "$MASTER_HOST" ]; then
    dim "      Target: $MASTER_HOST - it must be powered and on Wi-Fi."
    read -r -p "      Press Enter when ready " _ </dev/tty
    esphome upload board_d.yaml --device "$MASTER_HOST"
  else
    read -r -p "      Plug in this board over USB, then press Enter (Ctrl+C to stop) " _ </dev/tty
    p="$(pick_port)"
    if [ -n "$p" ]; then
      esphome upload "board_$b.yaml" --device "$p"
    else
      esphome upload "board_$b.yaml"
    fi
  fi
  ok "[$u/$UPLOADS] board_$b flashed in $(fmt_dur $((SECONDS - t0)))"
done
UPLOAD_SECS=$((SECONDS - upload_start))

step "Finished in $(fmt_dur $((SECONDS - RUN_START)))"
[ "$COMPILE_SECS" -gt 0 ] && dim "compiled ${#ORDER[@]} board(s) in $(fmt_dur $COMPILE_SECS)"
[ "$UPLOAD_SECS"  -gt 0 ] && dim "uploaded $UPLOADS board(s) in $(fmt_dur $UPLOAD_SECS)"
cat <<'EOT'
    Check the wall:
      - all 24 sweep to 12 together during the 10 s startup_align
      - every sync dot goes dark within a second of the master coming up
      - the time reads correctly (a wrong digit is a clock_index mistake, not wiring)
      - at :10 a choreography crosses all eight columns in order
EOT
