# Digital Clock Clock 24

A **physical [ClockClock 24](https://clockclock.com/)** — a digital clock built
out of twenty-four little analogue ones, where the hands sweep into position to
form the digits and spend the time in between doing something else entirely.

[![ClockClock 24 — the full 24-screen wall running](https://img.youtube.com/vi/BnIoumtDO5s/maxresdefault.jpg)](https://www.youtube.com/watch?v=BnIoumtDO5s)

*↑ The finished wall, running. [Watch it](https://www.youtube.com/watch?v=BnIoumtDO5s).*

**[See it move in your browser →](https://tuct.github.io/esphome-lvgl-clock/)**
— the project site runs the real firmware engine live, and its
[sandbox](./tools/clockclock24-sim/) lets you draw your own patterns.

The original is a beautiful, expensive piece of kinetic art. This is the same
idea built from **24 round LCD panels and eight £5 microcontrollers**, driven by
[ESPHome](https://esphome.io/) — about **€210**, and it fits on two screws.

| | |
| --- | --- |
| [**Build it — 24 round screens**](#the-build) | The real thing. 24 panels, 8 boards, no gaps. **Running on hardware** |
| [**Build it — 4 screens**](#the-cheap-way-in) | A sixth of the displays, at the cost of a gap between the digits |
| [**What it does**](#what-it-does--the-modes) | The choreographies, and drawing your own in the browser |

## The build

<img src="./digital_clock_clock_24_24_round_screens/images/half_screens_testing.jpg" width="80%">

Twenty-four 1.28″ round panels on eight XIAO ESP32-S3 boards — **one board per
column of the wall**, three panels each. Evenly spaced across all 24, so
`HH:MM` reads as a single continuous field of clocks, which is the original's
whole point.

One board has Wi-Fi and SNTP and broadcasts the time down a **one-wire UART
bus**; the other seven listen. That is what keeps 24 clocks on the same
millisecond — and it means only one board is ever reflashed to change what the
wall does.

| | |
| --- | --- |
| **Cost** | **€207 – €374** for the whole wall |
| **Size** | ≈ **27 × 13 cm**, **730 g** assembled |
| **Effort** | A project. One custom PCB, 24 panels to mount, 8 boards to flash |
| **Status** | **Running on hardware** — all eight boards built |

→ [**The full build**](./digital_clock_clock_24_24_round_screens) — costed BOM,
carrier PCB and gerbers, wiring, assembly order, bring-up and power.

### The cheap way in

<img src="./digital_clock_clock_24_4_screens/images/PXL_20260820_181724499~2.jpg" width="60%">

*A printed mock-up of the layout — four digit blocks, and the gaps between them.*

The same clock on **four rectangular panels and two boards**. Each panel draws a
whole digit rather than one mini-clock, so it is a sixth of the displays and an
afternoon rather than a project. The trade is the gaps: four blocks of six
instead of one grid of 24.

Roughly **€70 – €160**, ≈ 17–20 × 6–7 cm. Builds and validates; untested on
hardware.

→ [**The 4-screen build**](./digital_clock_clock_24_4_screens)

## What it does — the modes

Telling the time is the easy part. Every minute the wall breaks into a
**choreography** for 35 seconds and then sweeps back:

| | |
| --- | --- |
| **`wave`** | Each clock is one stroke; the turn rolls across the wall left to right, holding a fixed 15° fan |
| **`wind`** | A column is one stalk. A gust shears its two free ends past each other while the middle stays put |
| **`rotating_maze`** | A chevron lattice that eases almost to a stop every time it lands on an aligned figure |
| **`zipper`** | A front runs across a field of diagonals, unzipping each column into mirrored chevrons |
| **`mirror_wave`** | Vertical strokes scissor open, mirrored about the centre, spreading outwards |
| **`spiral`** · **`flying_birds`** · **`love`** · **`temp`** | …and the rest |

→ [**All of them, in detail**](./modes.md)

### Draw your own — no code, no reflash

[**Open the sandbox →**](./tools/clockclock24-sim/) It is the same engine as the
firmware, running in a browser.

Its **Motion Pattern Editor** lets you set a pose and a motion for each of the
24 clocks — a direction per hand and a speed, either fixed or *"the same as my
neighbour, ± a bit"* so a gradient across the wall is one number instead of
eight. Export it, paste it into a text field in Home Assistant, and the master
pushes it down the bus: **the whole wall has it in a second, with nothing
reflashed.**

Or write a choreography in JavaScript, watch it, and transcribe it into the
firmware — the sandbox is a line-for-line port, so the translation is syntax
only.

> **One rule holds everywhere:** it is an analogue clock, so it cannot jump.
> Every mode is a continuous function of time, and there is a regression check
> that drives all of them through their whole lifecycle to prove it.

## The component underneath

All of this is one ESPHome LVGL widget,
[`lvgl_clock`](./components/lvgl_clock/README.md). It draws a whole ClockClock
24 on a single screen out of the box — `partial:` is what slices the same
24-clock choreography across many displays instead. It also does four other
clock styles, if you just want a clock on a panel:

<img src="./images/clockclock24.gif" width="140"> <img src="./images/analog.gif" width="110"> <img src="./images/digital.gif" width="140"> <img src="./images/flipclock.gif" width="140"> <img src="./images/seg_matrix.gif" width="140">

→ [**Component reference**](./components/lvgl_clock/README.md) ·
[**`examples/`**](./examples) — ready-to-flash single-board configs
