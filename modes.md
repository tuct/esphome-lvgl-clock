# What it does — modes and patterns

A ClockClock 24 spends most of its time telling the time. The rest of it is
**choreographies**: the 48 hands doing something that is not a clock, for 35
seconds, before sweeping back. This page is what they all are, and how to make
one of your own.

Everything here is driven by [`cycle_modes:`](./components/lvgl_clock/README.md#style-clockclock24)
— a list walked in order, one window per interval, opening at `:10` past.
Repeats count: listing a mode twice gives it twice the slots.

> **Try them first.** [**Open the sandbox →**](https://tuct.github.io/esphome-lvgl-clock/)
> It is the same engine as the firmware, running in your browser — no flashing,
> no hardware.

## The built-in choreographies

| Mode | What it does |
| --- | --- |
| **`rotate_left`** | Every hand sweeping counter-clockwise in unison. The plainest one, and what the wall shows while it waits for Wi-Fi |
| **`flying_birds`** *(`birds`)* | Hands opening and closing like wings. The flock lifts off column by column, left to right, each beating up to speed rather than snapping into it |
| **`wave`** | Both hands on one line, so each clock is a single stroke. Every clock starts on the 10:30–4:30 diagonal, the left column sets off first, and the start ripples right — then they all turn at one rate for ever, holding a fixed 15° fan |
| **`spiral`** | Both hands together on 7:30. The bottom-left corner sets off first and the start rolls out along the diagonal to the top-right, counter-clockwise |
| **`wind`** | Read a column top to bottom and its three clocks are one continuous stalk. A gust from the left shears the two free ends past each other — the top tip sweeps right over the top, the bottom tip left underneath — while the middle row stays put |
| **`rotating_maze`** | A chevron per clock, alternating by column, rows counter-rotating. The turn is **not constant**: it eases to 40% of pace each time the wall lands on an aligned figure, so the lattice reads as forming and dissolving rather than spinning |
| **`zipper`** | A field of `\` diagonals with a front running across it, unzipping each column into a pair of mirrored chevrons and doing it up behind |
| **`mirror_wave`** | Every clock rests as one vertical stroke and scissors open, mirrored about the wall's centre. Starts in the middle and spreads outwards; the top and bottom rows run at 75% of the middle's rate, so the three beat against each other on a 36 s cycle |
| **`love`** | Spells **LOVE** across the four digit positions and holds it |
| **`temp`** | The temperature, as two digits plus `°C`. One sensor on the master; the reading rides the sync packet to every board |
| **`pattern`** | Plays a pattern you drew yourself — see below |

`time` is what every window returns to. `demo` is a bring-up aid — a fake minute
every 5 s — and is deliberately not allowed in a cycle list.

## Patterns — choreographies that are data

The eleven above are code, compiled into all eight boards. A **pattern** is the
same idea as data: 24 per-clock **poses** and **motions** — a direction per hand
and a speed — that the wall reads out of a text field. Every hand is
`pose + direction × speed × rate × t`, which is continuous whatever the numbers
are, so a pattern cannot make a hand jump however badly it is drawn.

That is what lets it skip the whole firmware loop. A pattern is not compiled and
not flashed; it is **one line of text**, and the master takes it over the
network at runtime.

### Draw one

[**The pattern editor →**](./homeassistant/DOCS.md#the-pattern-editor), in the
Home Assistant add-on. Pose each clock by dragging its hands, give each hand a
direction and a speed, then press **Send** — the master saves it to flash,
pushes it down the sync bus, and 24 real clocks are running it a second later.

A speed can be fixed, or *"the same as my neighbour, ± a bit"*, so a gradient
across the whole wall is one number instead of eight:

```
row 0 resolved speeds:  1.00  0.88  0.76  0.64  0.52  0.40  0.28  0.16
```

Eight patterns live on the master at a time, under names you choose, and those
names go straight into a cycle list beside `wave` and `spiral`.

That **nothing is reflashed, not even the master** is the point of the whole
master/slave split: the seven listeners carry no network stack precisely so they
never need one, and patterns are the thing you actually iterate on.

### Write one — a choreography in code

A choreography is a function of time, not a table of frames:

```js
function tickMyMode(cur, t, { modeSpeed }) {
  const ts = t * modeSpeed;
  for (let c = 0; c < NUM_CLOCKS; c++) {
    const { col, row } = wallPos(c);
    cur[c * 2 + 0] = wrap360(/* hand 0 */);
    cur[c * 2 + 1] = wrap360(/* hand 1 */);
  }
}
```

Prototype it in the sandbox, watch it, tune it — then transcribe it into
[`lvgl_clock.cpp`](./components/lvgl_clock/README.md). The sandbox is a
deliberate line-for-line port of the firmware, so the translation is syntax
only. Full guidance in the [sandbox README](./tools/clockclock24-sim/README.md).

## The one rule

> **It is an analogue clock. It cannot jump.**

Real hands sweep; they do not teleport. Every mode is written so that a hand's
angle is a *continuous* function of time, and switching modes fades each hand
from where it was into the choreography rather than cutting to it.

This is easier to break than it sounds. A `%` that wraps 360 → 0 in the wrong
place is a hand crossing the dial in one frame. So the sandbox ships a
regression check that drives every mode through its whole lifecycle and reports
the largest single-frame movement of any of the 48 hands:

```
transition 5 s, mode_speed 1.0
  ok  wave         enter+run 1.69    settle 3.94   deg/frame
  ok  wind         enter+run 2.81    settle 6.05   deg/frame
  ...
PASS — nothing jumped
```

A normal sweep is a few degrees per frame. A jump is 90–180.

## Where to go next

- [**Run it on a screen**](./screens.md) — tablet, dashboard, any browser. No hardware
- [**Build the 24-screen wall**](./digital_clock_clock_24_24_round_screens/README.md) — BOM, PCB, wiring, assembly
- [**The cheap 4-screen version**](./digital_clock_clock_24_4_screens/README.md)
- [**Control it from Home Assistant**](./homeassistant/README.md) — and draw patterns
- [**The component**](./components/lvgl_clock/README.md) — every option, all five clock styles
