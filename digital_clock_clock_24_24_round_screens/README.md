# digital_clock_clock_24 — the path to this project

[![ClockClock 24 — the full 24-screen wall running](https://img.youtube.com/vi/BnIoumtDO5s/maxresdefault.jpg)](https://www.youtube.com/watch?v=BnIoumtDO5s)

*↑ The finished 24-screen wall, running. [Watch it](https://www.youtube.com/watch?v=BnIoumtDO5s).*

Years ago I stumbled on [ClockClock 24](https://clockclock.com/) by Humans
since 1982 and immediately fell in love with the concept: a digital clock made
out of 24 analogue clocks. Genius.

The original is wonderful and well out of my budget, so I started coding a
version for an ESP32 with ESPHome. Here is one in use — a "call button" plus a
clock in ClockClock 24 style:

<img src="./images/cc_24_a.jpg" width="70%">

I also run versions on tablets: a full-screen browser showing a JS version
hosted on my Home Assistant.

<img src="./images/cc24_b.jpg" width="70%">

Those are fine, but I wanted to get closer to the original without using real
analogue clocks.

The next step was four displays, one per two columns, in a 3D-printed frame.
Quite nice — but the gap between the digits ends up wider than the gaps between
the clocks inside a digit:

<img src="../digital_clock_clock_24_4_screens/images/cc_24_c.jpg" width="70%">

→ [the four-screen version](../digital_clock_clock_24_4_screens)

To get rid of that gap I tried the crazy approach: 24 round displays, one per
clock. It worked.

## digital_clock_clock_24 — a physical ClockClock 24, on 24 round screens

24 round displays forming one [ClockClock 24](https://clockclock.com/), driven
by **8 XIAO ESP32-S3 boards with 3 panels each**. Every board runs the same
[`lvgl_clock`](../components/lvgl_clock/README.md) `clockclock24` engine, and each panel renders
**one** of the 24 mini-clocks (`partial: 0…23`), so the digit sweeps,
`movement:` directions and idle animations stay identical across the wall by
construction.

One board has Wi-Fi and SNTP and broadcasts the time over a one-wire UART bus;
the other seven listen. The master drives three panels too — it is board D, not
a ninth box.

> **Finished wall:** ≈ **27 × 13 cm** and **730 g** — light enough to hang on
> two screws.
>
> **Status:** running on hardware — **all eight boards built**, the full
> 24-clock wall. [Watch it run](https://www.youtube.com/watch?v=BnIoumtDO5s) · see
> [First full build](#first-full-build), and [`PROTOTYPE.md`](./PROTOTYPE.md)
> for how it got there.
>
> **Cost:** roughly **€207–€374** for the full 24-clock wall — the spread is
> almost entirely where you buy the XIAOs. See [BOM and cost](#bom-and-cost).

## First full build

All eight boards, all 24 clocks, in a printed case.

[**Watch it run**](https://www.youtube.com/watch?v=BnIoumtDO5s) — the digit sweeps and the choreographies, at speed.

<img src="./images/half_screens_testing.jpg" width="100%">

Front on, mid-test: all 24 panels mounted, four columns driven. The case is one
printed part with 24 round cutouts; the panels sit behind it so only the round
glass shows.

<img src="./images/screens_in_case.jpg" width="100%">

The same case from behind, with all 24 panels seated — eight rows of three, one
row per carrier board. Each panel's 8-pin header points inwards, ready for the
carrier to drop onto it.

All eight carriers then push onto those headers and chain together — see
[step 11](#hardware-assembly).

## What you need

### BOM and cost

Everything for the full eight-board, 24-clock wall. Quantities come from the
[netlist](./PCB/Netlist_Schematic_24_screens_2026-08-25.tel); prices are what the parts
cost when they were last checked, so treat them as a starting point rather than
a quote.

| Part | Qty | € / unit | € total | Where / note |
| --- | ---: | ---: | ---: | --- |
| 1.28″ 240×240 round GC9A01A panel | 25 | 4.7&nbsp;–&nbsp;5.6 | 118&nbsp;–&nbsp;140 | [5-pack, €23.49](https://amzn.to/4xJpK79) — 5 packs: 24 used, 1 spare |
| Seeed XIAO ESP32-S3 | 8 | 5.6&nbsp;–&nbsp;15.2 | 45&nbsp;–&nbsp;122 | [3-pack ~€15 at Seeed](https://www.seeedstudio.com/Seeed-Studio-XIAO-ESP32S3-3PCS-p-5919.html) vs [€15.16 each on Amazon](https://amzn.to/4zm4KF8) |
| Custom PCB — [gerbers](./PCB/Gerber_PCB3_2026-08-25.zip) | 8 | 2.5&nbsp;–&nbsp;3.8 | 20&nbsp;–&nbsp;30 | one fab order, 2-layer 34 × 131.6 mm |
| MP1584EN buck module — `U7` | 2 | 1&nbsp;–&nbsp;5 | 2&nbsp;–&nbsp;10 | [10-pack, €10](https://amzn.to/4g2qbn7). **Not one per board** — see [How many regulators](#how-many-regulators-and-where) |
| JST-XH 4-pin connector — `CN1`, `CN2` | 16 | 0.2&nbsp;–&nbsp;0.4 | 3&nbsp;–&nbsp;6 | chain in / chain out; sold in 20/50-packs |
| JST-XH 2-pin connector — `U8` | 0&nbsp;–&nbsp;1 | 0.2&nbsp;–&nbsp;0.4 | 0&nbsp;–&nbsp;1 | **Optional**, and only on the board you feed — skip it if you power the wall by plugging USB-C into that board's XIAO |
| 4-way chain cable — housing, crimps, wire | 7 | 0.7&nbsp;–&nbsp;1.6 | 5&nbsp;–&nbsp;11 | one per hop between boards |
| 8-pin female header 2.54 mm — `U3`/`U4`/`U5` | 24 | 0.2&nbsp;–&nbsp;0.4 | 5&nbsp;–&nbsp;10 | one per panel; or cut from 40-pin strips |
| 7-pin female header 2.54 mm — `U6` | 16 | 0.2&nbsp;–&nbsp;0.4 | 3&nbsp;–&nbsp;6 | the XIAO socket, 2 × 7; also cut from strips |
| 100 nF 0805 — `C1`–`C3` | 24 | <&nbsp;0.1 | 1&nbsp;–&nbsp;2 | **All 24 fitted** on the built wall. **The only SMD part** — sold in 100-packs |
| 220 µF electrolytic — `C6` | 0&nbsp;–&nbsp;8 | 0.1&nbsp;–&nbsp;0.3 | 0&nbsp;–&nbsp;3 | **Optional** — **none fitted** on the built wall. Bulk on the 5 V rail; through-hole, 6.3 mm |
| 100 µF electrolytic — `C5` | 1&nbsp;–&nbsp;8 | 0.2&nbsp;–&nbsp;0.4 | 0&nbsp;–&nbsp;3 | **One** was enough for the whole wall. Bulk on the 3.3 V rail; through-hole, 6.3 mm |
| 5 V 2 A USB supply | 1 | 0&nbsp;–&nbsp;15 | 0&nbsp;–&nbsp;15 | one for the whole wall; €0 if you have one |
| [3D-printed frame](#3d-printed-frame) | 1 | 5&nbsp;–&nbsp;15 | 5&nbsp;–&nbsp;15 | filament only, if you print it yourself |
| **Total** | | | **€&nbsp;207&nbsp;–&nbsp;374** | |

**Where the money goes.** The panels are the floor — €118 of it, and there is
no way around 24 displays. The one real decision is the **XIAO**: buying
3-packs from Seeed instead of singles from Amazon saves about **€77**, a third
of the cheap build. Everything else together is under €60.

**Reading the table.** `Qty` is what the finished wall holds, and every row
multiplies out — `qty × unit = total`. The unit prices for passives and headers
are **pack prices divided down**, because nobody sells 24 individual 0805s: you
buy a 100-pack, a couple of 40-pin header strips and a bag of XH housings, pay
the pack price once and keep the remainder. So the small rows are what those
parts *cost you here*, not what you will spend at the checkout — expect a few
euro more and a drawer of spares.

**What the built wall actually has fitted**, which is less than the table
allows for:

- **All 24 × 100 nF** (`C1`–`C3`, one per panel header). These are the ones
  worth having — they sit right at the panels and cost almost nothing.
- **One 100 µF** on the 3.3 V rail, for the whole wall rather than one per
  board.
- **No 220 µF at all.** `C6` is empty on every board.

That runs fine, so treat the `0` low bounds in the table as real rather than
theoretical. The reasoning holds: bulk on the rails is there to stop 24
backlights striking at once from dragging the supply down, and one bulk cap
near the single regulator does that as well as sixteen spread along the chain.

Three things that are *not* eight-off, and are easy to over-order:

- **The MP1584EN.** One or two modules feed the whole chain; the rest of the
  boards leave the footprint empty. A 10-pack is already several builds' worth.
- **The 5 V supply.** A single **5 V 2 A USB supply is fine for the whole
  wall** — all eight boards together draw ~1.2 A. One supply, fed into the
  middle of the row; not one per board.
- **The `U8` 2-pin connector.** Only the board you feed needs one, and even
  that one is optional: USB-C into that board's XIAO lands on the same `+5 V`
  net, so you can populate none of them.

### 3D-printed frame

The wall needs a frame: a face plate with 24 round cutouts, and something
behind it to hold the eight carrier boards at the right spacing.

*Print settings, STLs and assembly notes to follow.*

## How to build

### Hardware assembly

**Build and prove one board before you build eight.** A single XIAO tests every
carrier: solder a board, plug the XIAO into it, confirm all three screens come
up, then move that XIAO to the next board. Eight boards assembled and only then
powered is eight boards to debug at once.

**Build board D first — it is your test rig.** D is the master *and* the
regulator board: it is the only one with `wifi:`, and it carries the `U7`
MP1584 and the 5 V input. That is deliberate — the middle of the row, so the 3.3 V it makes has at most four boards to reach in either
direction. That module is the *only* source of the 3.3 V the panels run on: a
board without one has no panel supply of its own, so testing it means chaining
it to the regulator board with a 4-pin cable and letting `+3.3 V` come down the
chain. Keep that board on the bench for the rest of the build.

**1. Solder one board, lowest parts first.**

| Order | Parts | Note |
| --- | --- | --- |
| 1 | `C1`–`C3`, 100 nF 0805 | Only if you are fitting them — see [the BOM](#bom-and-cost). SMD, so they go on while the board is still flat |
| 2 | `U7`, the MP1584 module | Only the one or two boards that carry a regulator. It lies flat on its footprint and is soldered through its pads — poor-man's SMD, and much easier before the tall parts |
| 3 | `U3`/`U4`/`U5`, `U6` | The three 8-pin panel headers and the 2 × 7 XIAO socket |
| 4 | `CN1`, `CN2`, `U8` | The 4-pin chain connectors, plus the 2-pin power input if you are using one |
| 5 | `C5`, `C6` | The electrolytics: tallest, so last. Watch polarity |

**2. Set the MP1584 to 3.3 V — before any panel is plugged in.** These modules
ship adjustable and usually well above 3.3 V. Power the board with the panel
headers **empty**, turn the trimmer until pin 2 of a panel header reads 3.3 V,
then fit the panels. Getting this wrong once costs three displays.

**3. Flash one XIAO as the master and test three screens.** Do this on the
board you fitted the MP1584 to: `board_d.yaml` is the only config that brings
up Wi-Fi and SNTP, so that one board needs nothing else — no bus, no second
board:

```bash
esphome run board_d.yaml
```

All three panels should light, the right way up, sweep to 12 together during
the 10 s `startup_align`, then show the time and break into a choreography at
`:10`. **Nothing special is needed to see movement** — the stock config cycles
`birds`, `wave`, `spiral`, `wind`, `love` and `temp` on its own, so a board that
is working is obvious from across the room. If one panel stays dark it is that
panel's chip select or its header joints; the other two prove the shared SPI,
DC and reset lines are fine.

**4. Build the remaining seven boards**, testing each as you go by moving that
same XIAO across. Tick the board's letter in the silkscreen
**`BOARD A B C D E F G H`** row as you finish it.

> **These boards have no regulator, so they cannot power their own panels.**
> Hook the board under test to the MP1584 board with a 4-pin chain cable before
> you plug the XIAO in — `+3.3 V` is one of the four nets, so the regulator
> board feeds the panels across the link exactly as it will in the finished
> wall. Three dark screens on an unchained board is the expected result, not a
> fault.

**5. Flash a second XIAO as a listener and test the bus.** With `board_b.yaml`
on it, join it to C with one 4-pin chain cable — which also gives it the 3.3 V
its panels need:

```bash
esphome run board_b.yaml
```

**The sync dots are the test.** A listener draws a dot on each face until it
has heard a valid packet, and within a second of the master coming up all three
should go dark. If they stay, the bus is not working — see
[Debugging the bus](#debugging-the-bus). The listener also picks up the
master's choreography, so the two boards animating in step is the second half
of the proof. Walk that listener XIAO down the chain and repeat for every
board, so each carrier's `CN1`/`CN2` is proven before anything goes into the
frame.

**6. Wire the chain.** One 4-pin cable per hop, seven in total, fed in the
middle. The diagram and the per-hop detail are in [Wiring](#wiring) below.

**7. Run the whole wall on the bench, before anything goes into the frame.**
Flash all eight (`board_d.yaml` over the network, the rest over USB), chain
them, and power the middle. Everything up to here has been tested
two boards at a time; this is the first time the wall is a wall, and it is far
easier to fix flat on a table than screwed to a frame.

What to look for, in order:

- **All 24 sweep to 12 together** during the 10 s `startup_align`, then land on
  the time together. A column that lags or never arrives is a bus problem at
  that board's `CN1`.
- **Every sync dot is dark.** One board still dotted means it is not hearing
  the master — that hop's cable, or its `CN1`/`CN2` joints.
- **The time reads correctly.** A board plugged into the wrong column shows a
  scrambled digit, and that is a `clock_index_*` mistake in its YAML, not a
  wiring one.
- **Wait for `:10` and watch a choreography cross the wall.** `wave` and `wind`
  travel left to right across all eight columns, so they are the only test that
  proves the column ordering and the shared animation clock end to end. If the
  wave arrives at a column out of turn, that board has the wrong indices.

**8. Prove every panel works before any of it is glued.** Hot glue is not
meant to come back out, so a panel that turns out to be dead after mounting is
a real problem. Walk a carrier along and light all 24 first — three at a time,
watching for a panel that stays dark, comes up the wrong way round, or shows
tearing. Steps 3 to 7 already do most of this; the point here is that **no
panel goes into the case until you have seen it draw a clock.**

**9. Fit the panels into the printed case — and check the orientation.** The
case is one printed part with 24 round cutouts; the panels sit behind it so
only the round glass shows through. Every panel goes in **the same way round,
with its driver IC at the top**:

<img src="./images/screens_in_case.jpg" width="100%">

Eight rows of three, one row per carrier board. Orientation is the mistake to
watch for: a round panel mounted a quarter-turn out puts that clock's 12 where
its 3 should be, and nothing in the config can correct it per panel — the whole
board shares one `rotation:`. It is also invisible until the hands move,
because a round display looks identical whichever way up it is.

**10. Glue them in.** Hot glue is enough — a bead at two or three points on
each panel's edge, not over the header:

<img src="./images/screens_one_pcb.jpg" width="100%">

Each panel's 8-pin header points inwards so a carrier lands straight onto its
three. Lay one bare carrier on before gluing the rest of a row, to check the
fit.

**11. Push the finished carriers onto the panels and wire the chain.** Each
carrier presses onto the three headers of its row; then one cable per hop, all
the way along, as in [Wiring](#wiring):

<img src="./images/screens_and_pcb_in_case.jpg" width="100%">

**Note this is PCB v1.0.** Its pass-through is a pair of **3-pin** headers, so
the chain is two runs — the red/blue pairs carrying `GND`, `+5 V` and `UART`,
and the separate yellow lead carrying `+3.3 V` the length of the wall. **v1.1,
the revision in [`PCB/`](./PCB), replaces both with a single 4-pin XH per
hop**, so a finished board of that revision has one cable in and one out and
nothing else.

What the photo does show, and still holds on v1.1: **only one `MP1584EN` is
fitted** for the whole wall — the other seven footprints are empty, exactly as
[How many regulators](#how-many-regulators-and-where) describes.

> **Powering over USB-C rather than `U8`? Use a right-angle USB-C cable.** A
> straight plug does not clear the frame.

### Wiring

One 4-way cable per hop, and that is the entire harness. `CN1` and `CN2` carry
the same four nets, so each board loops straight through to the next — power
and the sync bus in one run:

```
   4-pin XH chain cable:  +3.3 V . GND . +5 V . UART     7 cables, 8 boards

                 5 V IN — XH-2 on U8, or a
               90-degree USB-C into D's XIAO
                             |
                             v
  +-----+ +-----+ +-----+ +-----+ +-----+ +-----+ +-----+ +-----+
  |  A  | |  B  | |  C  | |  D  | |  E  | |  F  | |  G  | |  H  |
  +----o+-+o---o+-+o---o+-+o---o+-+o---o+-+o---o+-+o---o+-+o----+
   col 0   col 1   col 2   col 3   col 4   col 5   col 6   col 7

   D = master (Wi-Fi), the MP1584, and 5 V in   o = CN1 / CN2, 4-pin XH
   C = a 2nd MP1584, only if you fit one        end boards use only one
```

- **One cable per hop, seven in total.** The two end boards leave one
  connector unpopulated. One board is one wall column, left to right, and its
  three panels are that column top to bottom.
- **Feed the middle, not an end** — board D or C. That keeps every run to four
  boards or fewer, which is the length actually measured
  ([0.01 V of drop](#how-many-regulators-and-where)).
- **Power enters either through `U8` or over USB-C.** The XIAO's `VBUS` pin
  sits on the same `+5 V` net, so plugging one USB supply into the middle
  board's XIAO runs the whole wall and `U8` need not be fitted at all.
- **Board D is the master**, and the natural place for the MP1584 and the 5 V
  feed as well — one board in the middle carrying the regulator, the power
  input and the network. It drives `partial: 7 / 9 / 11`, its own column, like
  every other board.
- **Fitting a second MP1584? Split the 3.3 V rail.** Populate `U7` on D *and*
  C, then leave the `+3.3 V` wire out of the one cable joining them, so each
  module feeds its own half of the wall. `GND`, `+5 V` and `UART` still pass
  through. Never tie the two outputs together —
  [why](#how-many-regulators-and-where).

The pin map itself — which XIAO pin does what, and why the bus cannot sit on
D6 — is in [Per-board pin budget](#per-board-pin-budget) under Technical
details.

**The sync bus rides the same cable.** `UART` is the fourth wire, so it chains
along with power instead of needing its own run, and the master's TX reaches
every board through the daisy chain — in both directions, since the master sits
in the middle. It is still one TX driving high-impedance RX inputs: master to
slaves only, so a slave sends nothing back. Same silkscreen pin, **D1**, on
every board, with the role deciding direction. At 115200 baud on a bench that
is comfortable; across a frame with metres of cable, use RS-485 transceivers.

### Flash the firmware

```bash
esphome run board_d.yaml      # master, over the network once it is on Wi-Fi
esphome run board_a.yaml      # …and the other seven, over USB
```

[`flash-all.sh`](./flash-all.sh) does the whole wall and pauses between boards
so you can move the USB lead. macOS and Linux:

```bash
./flash-all.sh                    # all eight, over USB, in order
./flash-all.sh -b a,b,c           # some of them
./flash-all.sh -b a,b,c,e,f,g,h,d # listeners first, master last
./flash-all.sh -p /dev/cu.usbmodem1101   # skip the port prompt
./flash-all.sh --build-only       # check a change compiles for all eight
./flash-all.sh -m cc24-board-d.local     # master over the network, not USB
```

Before each board it draws the wall **as you are looking at it — from behind,
where the USB sockets are, so board A is on the right** — with that board's
column picked out:

<img src="./images/flash-all-wall-map.png" width="90%">

Columns are **zero-based**. Board D above is *column 3* and the *fourth* one
along — so "column 4" counted the natural way lands on D when it means E. That
mismatch is the whole reason the picture is there: the highlighted block is
unambiguous in a way the number is not.

It also **compiles all eight before uploading anything**. A compile error found
halfway through leaves you with a wall running two firmwares and a board in
your hand.

### When the flashing order matters

It does not, if the wall is powered down or the modules are out of their
carriers — which is the normal case, and why the default order is simply
`a,b,c,d,e,f,g,h`.

It does if you are updating a wall that **stays running** while you work
through it. The mode is an integer on the sync bus and new modes are
*appended*, so a newer master can broadcast a mode an older listener does not
know — and that listener silently ignores the mode field and holds its last
animation. The other way round is harmless: an older master only ever sends
modes a newer listener already understands. So for a live wall, put the master
last:

```bash
./flash-all.sh -b a,b,c,e,f,g,h,d
```

The script says so itself when the master is not last.

Only board D has `ota:`, so by default everything goes over USB. Give it
`-m cc24-board-d.local` and the master goes over the network instead — useful
once the wall is mounted and D is the only board you can still reach.

Each board gets its own hostname and build directory, so the eight builds don't
collide.

`clock_mode` is a **master-only** knob — it lives in `board_d.yaml`, because
the master owns the wall's mode and the other seven follow whatever it
broadcasts. There is nothing to set on a slave, and nothing to keep in step by
hand. The default is `time`, which is all you need: the stock config already
animates on its own, cycling the choreographies once a minute at `:10`.

`demo` is there only if you want a fake minute every 5 s — useful when you are
watching the hours-tens column, which otherwise changes twice a day. Pass it
rather than editing the file, and set it on the master alone; the rest of the
wall adopts it off the bus, minute counter included:

```bash
esphome -s clock_mode demo run board_d.yaml
```

## How to configure

Almost everything you would want to change is in **[`panel.yaml`](./panel.yaml)
— written once and applied to all 24 clocks**, because every panel is that same
file included with different vars. Colours live in
[`common_base_esp32_s3_xiao.yaml`](./common_base_esp32_s3_xiao.yaml), and the
timezone in the role files. Nothing here needs editing a board file.

> **Most of it you no longer reflash for.** The mode, the cycle list and its
> interval, the eight pattern slots, and now `movement`, `transition_length`
> and `mode_speed` are all set on the **master** at runtime and broadcast down
> the bus. The values below are the compile-time defaults — where the wall
> starts, not where it is stuck. See
> [From Home Assistant](#from-home-assistant) below.
>
> They are broadcast rather than set per board for a reason: anything that
> affects timing **must be identical everywhere**, because each board runs the
> choreography from its own clock. Two boards on different values drift out of
> phase rather than merely out of step. Sending them from one place is what
> makes that impossible to get wrong.
>
> Genuinely compile-time — reflash all eight to change these: colours,
> `hand_width`, `startup_align`, `sync_dot`, `show_face`, and the pin map.

### Which choreographies play, and when

In `panel.yaml`, under `clockclock24:`:

```yaml
cycle_modes:
  interval: 1min      # how often to move to the next entry in the list
  modes:
    - birds
    - temp
    - wave
    - temp
```

The list is walked **in order** and wraps, so repeating an entry — as `temp`
does above — simply shows it more often. Each window runs from **:10 to :45**
past the minute and then settles back to the time; that timing is fixed, and
`interval:` only sets the cadence. Delete the whole `cycle_modes:` block and
the wall just tells the time.

| Mode | What it does |
| --- | --- |
| `birds` (`flying_birds`) | Hands sweep across the wall like a flock, left to right, ramping up |
| `wave` | A rotation travelling left to right, each column 15° behind the last |
| `spiral` | Every clock turning, offset along the diagonal |
| `wind` | Hands stand like grass; wind bends the top row right, the bottom row left, then releases |
| `rotating_maze` | A chevron field turning, rows counter-rotating, easing almost to a stop on each aligned figure |
| `zipper` | A front runs across a field of diagonals, unzipping each column into mirrored chevrons |
| `mirror_wave` | Vertical strokes scissor open, mirrored about the wall's centre, spreading outwards from the middle |
| `pattern` | Plays a pattern from [`patterns/`](./patterns) — authored in the sim, pushed to every board over the bus |
| `love` | Spells **LOVE** across the four digits |
| `temp` | The temperature, as `-9`…`99` plus `°C` |
| `rotate_left` | A plain continuous rotation |
| `time` | The clock. What every window returns to |
| `demo` | A fake minute every 5 s, for bring-up. Not valid in `cycle_modes:` |

### Look and feel

```yaml
clockclock24:
  movement: opposite        # opposite | clockwise | counter | long
  transition_length: 5s     # how long a digit change takes to sweep
  mode_speed: 1             # idle-animation speed, ×1.0 = the base rates
  startup_align: 10s        # hold every hand at 12 for this long after boot
  sync_dot: true            # blink a dot while a board is out of sync
  show_face: false          # draw the little clock faces behind the hands
  hand_width: 1             # base hand thickness, px
```

- **`movement:`** decides which way a hand travels to its new position.
  `opposite` sends the two hands of a clock in opposite directions, which is
  the ClockClock look; `long` takes the scenic route.
- **`transition_length:`** is also the fade in and out of a choreography, so
  raising it makes mode changes gentler as well as digit changes slower.
- **`mode_speed:`** scales the idle animations only. The base rates are already
  unhurried — a `wave` revolution takes 15.3 s — so this is rarely worth
  touching.
- **`movement`, `transition_length` and `mode_speed` are also Home Assistant
  entities on the master**, and what you set here is only the starting value.
  Changing the speed live is the interesting one: a choreography is evaluated
  at `t × mode_speed`, so a new multiplier moves *where the animation is*, not
  just how fast it runs from there. The wall blends into the new position
  rather than snapping — the same thing it does entering a mode — and every
  board applies the same number from the same packet, so it eases across
  together instead of falling out of phase.
- **`show_face: true`** draws a filled disc behind each pair of hands in
  `cc_faces`. It is off because a filled 240 px disc is the most expensive
  thing in the frame, and invisible in white-on-black anyway.
- **`sync_dot:`** is a diagnostic, not decoration: a healthy wall shows
  nothing. Leave it on — see
  [The sync dot](#the-sync-dot--reading-the-wall-without-a-laptop).

### From Home Assistant

The master is the only board with a network, and everything the wall does at
runtime is one of its entities:

| Entity | |
|---|---|
| `select.…_mode` | What the wall is doing. An **override** — with a cycle interval set, the next window takes it back. Set the interval to `off` to make a choice stick |
| `select.…_pattern` | Which slot `pattern` draws |
| `select.…_cycle_interval` | How often a window opens, or `off` |
| `select.…_movement` | `opposite` / `clockwise` / `counter` / `long` |
| `number.…_transition_length` | Sweep time, in seconds |
| `number.…_mode_speed` | Choreography rate, ×1 is the base |
| `text.…_hand_colour` | `#rrggbb`. Anything that is not a colour is refused and logged, not guessed at |
| `text.…_background_colour` | `#rrggbb` |
| `text.…_cycle_modes` | The rotation, in order — repeats count |
| `text.…_pattern_1` … | The pattern slots, read **and** write |
| `button.…_reload_patterns_from_firmware` | Back to the `patterns/` folder as compiled in |
| `button.…_reset_look_to_firmware` | Back to the look `panel.yaml` compiled in — see below |

All of it reaches the other seven boards over the bus. Nothing is recompiled
and nothing is reflashed.

#### Which means automations

They are ordinary entities, so the wall does what any other device in the house
does. A night mode is two service calls:

```yaml
automation:
  - alias: "ClockClock — night"
    trigger: { platform: sun, event: sunset, offset: "-00:30:00" }
    action:
      - service: text.set_value
        target: { entity_id: text.cc24_board_d_hand_colour }
        data: { value: "#ff7a2f" }          # warm amber, on the wire in one packet
      - service: number.set_value
        target: { entity_id: number.cc24_board_d_mode_speed }
        data: { value: 0.6 }                # slower, so it is not the brightest
                                            # moving thing in a dark room
  - alias: "ClockClock — day"
    trigger: { platform: sun, event: sunrise }
    action:
      - service: text.set_value
        target: { entity_id: text.cc24_board_d_hand_colour }
        data: { value: "#ffffff" }
      - service: number.set_value
        target: { entity_id: number.cc24_board_d_mode_speed }
        data: { value: 1.0 }
```

The colour change is one broadcast packet, so all 24 clocks turn amber on the
same frame rather than sweeping across the wall board by board. Same for a
`text.set_value` on `…_cycle_modes` — a quiet rotation at night and the lively
one by day is one automation, not a reflash.

#### It survives a restart

Movement, sweep length, speed, both colours, the cycle list and its interval
are written to flash about **ten seconds** after you change them — held that
long because NVS has a finite erase count and a colour picker fires all the way
round the wheel. The wall comes back the way you left it after a power cut,
exactly as patterns already did.

Which has a consequence worth knowing: **flash now wins over `panel.yaml`.**
Edit `mode_speed` there, reflash, and nothing visible happens, because the
saved value is applied over it at boot. `Reset look to firmware` is the way
back — the same role `Reload patterns from firmware` plays for patterns. The
compiled-in values are captured at startup *before* flash is read, so the
button always has something true to restore.

The mode itself is deliberately **not** saved: the wall should come back
telling the time, not stuck in whatever choreography was running when the power
went.

`board_d.yaml` also declares a project marker, which is how anything looking
for the wall finds it rather than guessing at entity names:

```yaml
esphome:
  name: cc24-board-d
  project:
    name: "tuct.digitalclockclock24"      # manufacturer / model in HA
    version: "1.1"
```

**[The add-on](../homeassistant/README.md)** puts all of this on one page, so
none of it needs an entity id typed out: a live preview of what the wall is
showing, the mode — **with your own patterns in the same list, by name** — the
cycle list as chips you drag, the interval, the movement, the sweep length, the
choreography speed and both colours. The **pattern editor** is on the same
page, wired to a slot: draw a pattern, watch it, press **Send**, and 24 real
clocks are running it a second later. Add this repository under **Settings →
Add-ons → Repositories**.

### Motion patterns — design them in the browser

Beyond the built-in choreographies, `mode: pattern` plays a **pattern**: 24
per-clock poses and speeds that are *data*, not code. You draw them in
[**`tools/clockclock24-sim`**](../tools/clockclock24-sim) — the same engine as
the firmware, running in a browser, so what you see there is what the wall
does.

Open [`index.html`](../tools/clockclock24-sim/index.html) (no server, no build
step) and:

1. Pick **Motion Pattern Editor Mode**. Edit and Play come on together.
2. **Click a clock** to select it; **shift-click** for several — every edit then
   applies to all of them.
3. **Drag** its hands to set the pose. Give each hand a direction (`←` `—` `→`)
   and a speed, either fixed or *"same as my neighbour ±"* so a gradient across
   the wall is one number rather than eight.
4. **Copy** a clock and paste it to its row, its column or all 24.
5. **Export**, and save the JSON into [`patterns/`](./patterns) as
   `<name>.json`. The filename becomes the pattern's name in the logs.

```bash
esphome run board_d.yaml       # the master only, over Wi-Fi
```

**That is the whole update cycle, and it only touches the master.** Because the
master is the one board with `wifi:` and `ota:`, a new pattern goes out over the
network — no USB, no opening the frame. Thirty seconds after it reboots it
pushes the patterns down the sync bus, and all seven listeners pick them up
without being reflashed at all.

That asymmetry is the point of the whole design. The slaves carry no network
stack precisely so they never need one, and patterns are the thing you actually
iterate on — the mode you would otherwise be reflashing eight boards to try.

Up to **8** patterns; add `pattern` to `cycle_modes:` and the wall walks through
the folder.

#### …or skip the reflash entirely

With `api:` on, this board exposes the patterns and the rotation as Home
Assistant entities, and both are editable **while it runs**:

| | |
| --- | --- |
| `Pattern 1…8` | Paste a string from the sim's **Copy for ESPHome** button. Reads back too, so copying the state copies the pattern |
| `Mode` | What the wall is doing now, and a way to change it immediately |
| `Pattern` | Which pattern `mode: pattern` draws, 1–8 |
| `Cycle modes` | `birds,temp,wave,fan,shear` — modes **and pattern names**, in order; repeats count |
| `Cycle interval` | `off`, or 1…60 min |
| `Reload patterns from firmware` | Undo, back to [`patterns/`](./patterns) |

**A cycle list takes pattern names.** `wind,fan,love,shear` plays those two
patterns by name rather than leaving it to a round-robin — the list reads back
with the names too, so you can see what was accepted. A bare `pattern` still
means "the next one in the store", advanced **once per window**.

**`Mode` is an override, not a preference.** With a cycle interval set, the
next window opens on schedule and takes the wall back, and between windows the
wall goes back to **telling the time** — picking a choreography says "show me
this now", never "stop being a clock". Set the interval to **`off`** and the
rotation stops entirely — the wall then shows whatever `Mode` says and only
changes when you or an automation change it.

Edits are saved to flash, so the wall keeps them with Home Assistant off — the
seven listeners still need nothing but power and a wire. A pattern write is
pushed down the bus straight away rather than at the next repeat.

See [Motion patterns](../components/lvgl_clock/README.md#motion-patterns) for
the packing, the wire format and the timing.

### Colours

Three named colours in the base package, shared by every panel:

```yaml
color:
  - id: cc_hands      # the hands
    red: 100%
    green: 100%
    blue: 100%
  - id: cc_bg         # the background
    red: 0%
    green: 0%
    blue: 0%
  - id: cc_faces      # the face discs, only drawn when show_face: true
    red: 12%
    green: 12%
    blue: 14%
```

`panel.yaml` references them as `foreground: cc_hands` / `background: cc_bg`.
White on black is the original's look and the cheapest to draw, but nothing
stops you making the hands amber.

**These are only the starting values.** Both are Home Assistant entities on the
master and both go on the wire, so one automation changes all 24 clocks:

```yaml
automation:
  - alias: Warm the wall after sunset
    trigger: { platform: sun, event: sunset }
    action:
      - service: text.set_value
        target: { entity_id: text.cc24_board_d_hand_colour }
        data:   { value: "#ff8c3c" }
```

`cc_faces` is still compile-time — it is only drawn when `show_face: true`,
which is off.

### Timezone and temperature

Both are master-only, because both travel down the sync bus to the other seven
boards. In [`board_d.yaml`](./board_d.yaml):

```yaml
time:
  - platform: sntp
    id: clock_time
    timezone: "CET-1CEST,M3.5.0,M10.5.0/3"
```

`mode: temp` shows whatever sensor is named by `temperature_sensor_id:` on the
broadcaster. The shipped config uses a template sensor that walks 18→24 °C so
the digits visibly change; swap the platform for a real one and keep the id:

```yaml
sensor:
  - platform: dht          # or bme280, homeassistant, ...
    id: room_temp
```

Only the master needs one. The reading rides along in the sync packet, so the
other seven show the same number without a sensor of their own — eight sensors
would just be eight opinions about the same room.

## Technical details

### Per-board pin budget

One XIAO ESP32-S3 driving three GC9A01A panels, and what that leaves free. The
**PCB is the authority** here: every net on the board matches a substitution in
[`common_base_esp32_s3_xiao.yaml`](./common_base_esp32_s3_xiao.yaml), and the
YAML follows it, not the other way round.

| Pin | GPIO | PCB net | YAML | Used by | Note |
| --- | --- | --- | --- | --- | --- |
| D0 | 1 | — | | free | |
| **D1** | **2** | `UART` | `sync_pin` | **sync bus** | clean: no strapping function, no ROM UART |
| D2 | 3 | `RESET` | `reset_pin` | LCD reset, **all three panels** | strapping pin (JTAG source select); fine as a reset output, don't hold it low at boot |
| D3 | 4 | `DC` | `dc_pin` | LCD DC, **all three panels** | |
| D4 | 5 | `CS_A` | `cs_pin_a` | panel A chip select | |
| D5 | 6 | `CS_B` | `cs_pin_b` | panel B chip select | |
| D6 | 43 | `CS_C` | `cs_pin_c` | panel C chip select | also the ROM's UART0 TX — see below |
| D7 | 44 | — | | free | `BLK` is tied to +3.3 V on the PCB, so there is no backlight pin to drive. Also the ROM's UART0 RX (an input at boot, so nothing contends) |
| D8 | 7 | `SCL` | `clk_pin` | SPI SCK | |
| D9 | 8 | — | | free | no MISO: these panels are write-only, and it is a no-connect on the PCB |
| D10 | 9 | `SDA_MOSI` | `mosi_pin` | SPI MOSI | |

The panel headers carry `GND, 3V3, SCL, SDA, RESET, DC, CS, BLK` on pins 1–8,
so all three panels share the clock, data, reset and DC lines and differ only
in their chip select — which is what makes three panels cost three pins instead
of twelve.

#### Why the sync bus must not sit on D6

**D6/GPIO43 is the ROM's UART0 TX**, and every S3 drives it as a push-pull
output for the first ~200 ms of a boot, before ESPHome reconfigures the pin. A
slave with the bus on D6 would be fighting the master's TX driver on every
power-up — output against output, on the one wire the whole wall depends on.
That is exactly the "board comes up, never shows time" symptom.

As **panel C's chip select** the same pin is harmless: the boot chatter only
strobes CS while the panels are still held in reset, and they are initialised
from scratch afterwards.

Keep `logger:` on the USB CDC console (the configs do) so it never contends
with the sync UART either.

### The PCB

A carrier board — one per wall column — that takes a XIAO ESP32-S3, breaks out
the three panel headers, regulates the panel supply and passes the sync bus and
5 V through to the next board. Design files are in [`PCB/`](./PCB) — **board
revision v1.1**, **34 × 131.6 mm**, 2 layers.

<img src="./PCB/3D_PCB3_2026-08-25.png" width="260" align="right">

| File | What it is |
| --- | --- |
| [`3D_PCB3_2026-08-25.png`](./PCB/3D_PCB3_2026-08-25.png) | 3D render (right) |
| [`SCH_Schematic_24_screens_2026-08-25.pdf`](./PCB/SCH_Schematic_24_screens_2026-08-25.pdf) | Schematic |
| [`SCH_Schematic_24_screens_1-P1_2026-08-25.png`](./PCB/SCH_Schematic_24_screens_1-P1_2026-08-25.png) | The same schematic as an image, for a quick look |
| [`Gerber_PCB3_2026-08-25.zip`](./PCB/Gerber_PCB3_2026-08-25.zip) | Gerbers + drills, ready to upload |
| [`Netlist_Schematic_24_screens_2026-08-25.tel`](./PCB/Netlist_Schematic_24_screens_2026-08-25.tel) | Netlist |

> **v1.1, 2026-08-25.** The pass-through connectors are **4-pin XH** carrying
> `+3.3 V`, `GND`, `+5 V`, `UART` — the four wires the prototype was already
> chaining by hand off a pair of 3-pin headers — and the board lost 8 mm.
>
> This revision is otherwise a cleanup: the second bulk cap on the 5 V rail
> (`C4`) is gone, since one is plenty for a board drawing 150 mA, and the
> outgoing connector is now `CN2` rather than `CN3`. **Nothing on the XIAO
> moved**, so the pin map and every config are unchanged.
>
> **v1.0 boards still work** — they just need the two-run wiring in the
> [assembly photo](#hardware-assembly), because their pass-through is a pair of
> 3-pin headers rather than one 4-pin.

Superseded files are kept in [`PCB/old/`](./PCB/old).

Upload the gerber zip as-is to JLCPCB or any EasyEDA-compatible fab — 2-layer,
1.6 mm, no controlled impedance or other special process. The silkscreen has a
**`BOARD A B C D E F G H`** row: tick the board's letter as you build it, so a
mis-indexed board is identifiable without reading its logs.

#### What is on it

| Ref | Part | Role |
| --- | --- | --- |
| U6 | Seeed XIAO ESP32-S3 (DIP) | The MCU, on a 2×7 socket |
| U3 / U4 / U5 | 8-pin female headers | Panels A / B / C |
| U7 | MP1584EN module | 5 V → 3.3 V for the panels. **Not needed on every board** — see [How many regulators](#how-many-regulators-and-where) |
| U8 | JST-XH 2-pin | 5 V power in |
| CN1 / CN2 | JST-XH **4-pin** | The whole chain in and out: `+3.3 V`, `GND`, `+5 V`, `UART` |
| C1–C3 | 100 nF 0805 | Decoupling, one per panel header |
| C6 | 220 µF | Bulk on the 5 V rail |
| C5 | 100 µF | Bulk on the 3.3 V rail |

#### Power topology

5 V arrives on **U8** and goes two places: straight to the XIAO's `VBUS` pin
(which runs its own on-board regulator for the MCU), and into the **MP1584**,
which steps it down to **3.3 V for the three panels**. The panels are not run
off the XIAO's regulator — three GC9A01As would be well past what it can
supply. Because `VBUS` is on that same `+5 V` net, plugging USB-C into the XIAO
feeds the board and everything chained to it, and `U8` need not be fitted.

**CN1 and CN2 carry the same four nets** — `+3.3 V`, `GND`, `+5 V`, `UART` — so
a board loops straight through to the next on a single 4-way cable, panel
supply included. That is why most boards leave `U7` empty: see
[Power](#power) for how many regulators the wall actually needs and where to
feed it.

### How the 24 clocks are numbered

Each digit of `HH:MM` is a 2-wide × 3-tall block of mini-clocks, so the wall is
8 columns × 3 rows. Numbering runs left-to-right then top-to-bottom **within** a
digit, and the digits go left to right:

```
           digit 0      digit 1      digit 2      digit 3
          (HH tens)    (HH units)   (MM tens)    (MM units)
            A      B     C      D     E      F     G      H  ← board / wall column
        ┌────────────┬────────────┬────────────┬────────────┐
 row 0  │   0      1 │   6      7 │  12     13 │  18     19 │
 row 1  │   2      3 │   8      9 │  14     15 │  20     21 │
 row 2  │   4      5 │  10     11 │  16     17 │  22     23 │
        └────────────┴────────────┴────────────┴────────────┘
```

```
index = digit * 6 + cell        cell = row * 2 + col
```

and back the other way:

```
digit = index / 6      row = (index % 6) / 2      col = (index % 6) % 2
wall column = digit * 2 + col
```

#### Board → column → clocks

One board per column, so a board's three indices step by **2**, not by 1:

| Board | Wall column | Digit | Clocks (rows 0/1/2) | Role |
| --- | --- | --- | --- | --- |
| A | 0 | hours tens, left | 0 / 2 / 4 | listener |
| B | 1 | hours tens, right | 1 / 3 / 5 | listener |
| C | 2 | hours units, left | 6 / 8 / 10 | listener |
| **D** | 3 | hours units, right | **7 / 9 / 11** | **master** — Wi-Fi + SNTP, MP1584, 5 V in |
| E | 4 | minutes tens, left | 12 / 14 / 16 | listener |
| F | 5 | minutes tens, right | 13 / 15 / 17 | listener |
| G | 6 | minutes units, left | 18 / 20 / 22 | listener |
| H | 7 | minutes units, right | 19 / 21 / 23 | listener |

Every panel prints its own position at boot, so a mis-wired module is one log
line away:

```
[C][lvgl_clock]:   Partial: clock 18 of 24 (digit 3, row 0, col 0)
```

Label every panel on the back as you build it — a mis-indexed panel is
invisible until its digit happens to change.

#### Which clocks move, and how often

Worth knowing before you conclude a panel is dead. In `mode: demo` the fake
clock advances one minute every `demo_interval` (5 s), so:

| index | digit | changes every |
| --- | --- | --- |
| 18–23 | minutes, units | **5 s** |
| 12–17 | minutes, tens | 50 s |
| 6–11 | hours, units | 5 min |
| 0–5 | hours, tens | 50 min |

So the boards worth watching on the bench are **G and H** (the minutes-units
numeral, clocks 18–23): all six move on every demo tick, and any sync error
between the two shows up as a single digit visibly forming in two stages.

Board D, the master, is the right half of the *hours-units* numeral, which only
changes every 5 min in demo. For bring-up either borrow a minutes column,

```bash
esphome -s clock_index_a 19 -s clock_index_b 21 -s clock_index_c 23 \
        run board_d.yaml
```

or raise `demo_step:` (fake minutes per tick, default 1) in `common.yaml` —
e.g. `137` changes all four digits every tick.

### Time sync

The master broadcasts a line on the bus (default every second):

```
CC24 <epoch> <ms> <mode> <demo_min> <temp> <slot> <movement> <transition_ms> <speed_x100> <hand_rgb> <bg_rgb>\n
```

`<ms>` is the part that matters. Nodes only need to agree on which *minute* it
is, but a node whose clock sits a second off flips its digit a second late, and
on a wall of 24 that reads as a fault rather than as drift. ESPHome's own
`synchronize_epoch_()` deliberately ignores corrections under ±1 s and sets
whole seconds only, so the slave platform sets its clock with microsecond
precision instead and flips land within transport jitter. `<mode>` carries
`time` / `rotate_left` / `flying_birds` / `demo` so the whole wall animates as
one, and `<demo_min>` carries the master's fake-minute counter in demo mode
(-1 otherwise) — without it each board would count its own and the wall would
show 8 different times.

Every field after `<mode>` is **optional on the way in**: the parser fills a
default for anything the line does not carry, and ignores anything it does not
recognise. That is what makes a mixed-firmware wall survive — an older listener
reads a newer master's line and simply stops at the last field it knows. It is
also why the format only ever **grows**: fields are appended, never reordered
or repurposed, and the mode and movement enums are append-only for the same
reason.

The last five carry how the wall *moves* and what it is *drawn in* — the routing rule for a sweep, how
long a sweep takes, and the choreography speed multiplier. They are set on the
master (as Home Assistant entities) and broadcast, because they have to be the
same everywhere: `mode_speed` scales the time base, so two boards on different
values do not merely look different, they drift apart.


#### Only the master runs the boot-phase animation

`board_d.yaml` has the `interval:` that spins while connecting, flies birds
while waiting for NTP, then shows the time. The slaves deliberately have none:
the mode rides the sync packet, so a slave that also decided for itself would
fight the master once a second.

**There is no separate command channel, and there doesn't need to be** — the
mode *is* field 3 of the packet, and a slave applies it to every widget it
lists. What makes the boot phase work is the `<epoch> == 0` case: the master
starts broadcasting the moment it boots, before SNTP has given it a time, and
sends

```
CC24 0 0 1 -1        # no time yet, mode = rotate_left
```

A slave reads epoch 0 as "mode only — keep your clock", so it spins with the
master from the first second. Without that the master would broadcast nothing
until NTP landed, and the wall would spend its whole boot phase with one column
spinning and seven sitting on the boot default with nothing to follow.

It is also what lets **demo mode** run the whole wall **with no network at
all**: `<demo_min>` rides the same mode-only packet, so all eight boards count
the same fake minute straight off the bench supply.

#### Choreographies, and why they need no coordination

`common.yaml` sets the wall to break out into an animation from **:10 to :45**
of every minute:

```yaml
cycle_modes:
  interval: 1min
  # The 35s window, starting 10s past the minute so it stays clear of the
  # digit flip at :00, is fixed in the component - only the cadence is a knob.
  modes: [birds, wave, spiral, wind]
```

Each animation is a pure function of the synced clock and the mini-clock's
position in the 8×3 grid, so once two boards agree on the mode and the time
they are drawing the same frame of the same figure.

The list is walked **in order** and wraps, so a repeated entry comes round more
often. **Which** choreography plays is decided in exactly one place: `dc_a` on board D,
the first widget listed in that board's `lvgl_clock_id`. Everything else — board
D's other two panels included — is marked a follower, never runs
`cycle_modes:`, and is handed the mode in the sync packet. Letting all eight
boards pick for themselves would hold only as long as their clocks and their
config agreed to the second; one board a second out at :10 would start a
different animation from the other seven. So `cycle_modes:` stays in the shared
`common.yaml` and only the master acts on it.

A board that reboots at :25 is handed the running choreography within a second
and joins it mid-figure, rather than starting its own.

`wave` is the one that proves the wiring: the crest is defined to travel left
to right across all 8 columns, so if board E is showing the phase board D
should be, it is mis-indexed. A column-per-board layout makes that obvious.

The animations also run off the wall clock rather than `millis()`, which is
what makes any of this work — `millis()` starts at each board's own power-on.

That means a slave's `lvgl_clock_id` must list **all three** of its widgets:

```yaml
lvgl_clock_id: [dc_a, dc_b, dc_c]
```

The bus is the only thing that moves them, so one left out would sit on the
boot default forever.

#### Debugging the bus

Both roles log it. `logger: level: DEBUG` gets you a throttled health line
every 10 s from either end; `lvgl_clock.sync: VERBOSE` adds every packet:

```yaml
logger:
  level: DEBUG
  logs:
    lvgl_clock.sync: VERBOSE
```

The three states worth recognising:

| Log line | Means |
| --- | --- |
| `TX mode-only (…, no system time yet)` | the master is broadcasting, but has no SNTP — the wall will animate together and show no time |
| `RX no valid packets yet (0 bytes, …)` | nothing is arriving: wiring, ground, or the master is silent |
| `RX bad prefix, ignored: '…'` / `RX overlong line` | bytes are arriving but garbled: baud mismatch or signal integrity |
| `RX stalled: last packet … ms ago` | the bus was working and stopped: master rebooted, or a wire came off |

#### The sync dot — reading the wall without a laptop

`sync_dot: true` puts a dot at 1:30 on each panel that flashes for 120 ms every
second **while that board is out of sync**. It is a fault light, not a
heartbeat:

| What you see | What it means |
| --- | --- |
| No dots anywhere | The whole wall is synced. This is the healthy state |
| Every slave blinking, master clean | The master has no usable time yet, or its TX is dead |
| One column blinking | That board's RX drop, or its ground |
| Dots come back after working | The bus went quiet for 10 s — the board is now free-running and will drift |

The master never shows a dot: it is the time source, so it has nothing to be
out of sync with. That falls out of the default rather than being wired up —
a widget is "synced" unless a listening time platform tells it otherwise, so a
standalone clock never shows one either.

The blinking panels blink *together*, since the dot is driven off the shared
clock. A whole column coming up at once therefore reads as "the master is
late", while one odd panel in a column reads as "that node's wiring".

### Only the master has a network

`wifi:`, `api:` and `ota:` live in `board_d.yaml` alone. The other seven take the
time off the UART and have no network stack at all, which means:

- **One set of credentials on the wall**, and one board that cares whether the
  Wi-Fi is up. Seven boards that cannot fail to associate, cannot wait on DHCP,
  and boot to a spinning clock in a second.
- **No OTA on the listeners.** They are flashed over USB. With eight boards
  rather than 24 that is a much easier trade than it used to be, but it is the
  real cost — decide before the frame is glued shut.
- Their heap sensors still report to the USB console via `logger:`; they just
  have nowhere to publish.

If you would rather have OTA on all eight, add `wifi:` and `ota:` (and skip
`api:`) to `common_base_esp32_s3_xiao.yaml` instead — nothing else changes,
because the time still comes off the bus either way.

### Power

**A board draws about 150 mA at 5 V** — one XIAO ESP32-S3 plus its three
panels with backlights on. That scales linearly, because every board is
identical:

| | Current at 5 V | Power |
| --- | --- | --- |
| One board (3 panels) | ~150 mA | ~0.75 W |
| Full wall, 8 boards | **~1.2 A** | ~6 W |
| Supply | 2 A USB | ~40% headroom |

So a 2 A USB supply carries the whole wall with room to spare.

Worth knowing before wiring it:

- **Size for inrush, not the average.** All eight boards come up together, and
  24 backlights striking at once plus the master associating to Wi-Fi pulls
  well above the steady 1.2 A for a few hundred milliseconds. 2 A covers it;
  a 1 A supply would brown out at switch-on and present as "a random board
  won't boot", which is a power fault wearing a firmware costume.
- **The master draws more than the slaves.** It is the only board running a
  Wi-Fi stack, worth roughly 50–80 mA extra in bursts. If anything is marginal,
  it fails there first.
- **There is no low-power state.** `BLK` is tied to +3.3 V on the PCB, so the
  panels are always at full brightness. Dimming would be the obvious way to cut
  the wall's draw, since the panels are most of it, but it needs a board
  revision that routes D7 to `BLK`.

#### How many regulators, and where

**One MP1584 for the whole wall is enough, and the 3.3 V rail daisy-chains with
the 5 V.** The remaining seven boards leave `U7` empty. This is measured, not
estimated: across four chained boards the drop is **0.01 V on both rails**. The
one module then carries the whole wall's 3.3 V load, which an MP1584 — a 3 A
part — takes without heatsinking.

Two consequences worth designing around:

- **Feed the chain in the middle** — board D or C — so no run is longer than
  four boards in either direction, the length already measured. It costs
  nothing but where you put the connector.
- **A second module is optional, and if you fit one, give each its own rail.**
  Populate `U7` on D and C, then omit the `+3.3 V` wire from the single cable
  between them. Paralleled trimmer-set buck modules do not share a load — the
  one set a few millivolts higher simply takes all of it — so tying the two
  outputs together makes a two-regulator wall behave worse than a
  one-regulator wall, not better. `GND`, `+5 V` and `UART` still pass through
  that cable, so the chain and the sync bus are unbroken.

### Memory

The S3's 8 MB of octal PSRAM is what makes three panels per board unremarkable.
`direct_draw: true` also skips the widget's own canvas and renders straight
into LVGL's buffer — a 240×240 RGB565 canvas is 115 KB, so three of them would
have been 345 KB that the build simply never allocates.

The base package enables the memory debug sensors (`free`, `block`). Watch
`block`: a draw buffer needs one contiguous chunk, so it can fail while `free`
still looks fine. If a widget runs out it logs an error and disables itself — a
blank panel rather than a crash.

#### Frame rate

Each 240×240 panel needs ~11.5 ms for a full frame at 80 MHz, and all three
share one SPI bus, so ~34 ms is the floor for a full sweep. `render_interval:
33ms` (≈30 fps) matches that; asking for 60 just starves the next frame.

### How the configs fit together

Eight boards, two roles, and one copy of every setting. The build is layered so
that a board file contains **only what makes that board different** — which is
three clock indices, and on the master its network:

```
board_d.yaml  ── THE MASTER ───────────────────────────────────┐
  wifi: / ota: / uart: TX / sntp / broadcaster / temp sensor   │
  └─ common.yaml                                               │
                                                               │
board_a.yaml  ── A LISTENER (and b, c, e, f, g, h) ────────────┤
  three clock indices, nothing else                            │
  └─ common_slave.yaml                                         │
       uart: RX / lvgl_clock time platform / hostname          │
       └─ common.yaml                                          │
                                                               │
                     common.yaml  ◄────────────────────────────┘
                       spi: (one bus) · display: ×3
                       ├─ common_base_esp32_s3_xiao.yaml
                       │    esp32 · psram · logger · pin names · colours
                       ├─ panel.yaml   vars: lvgl_a, dc_a, ${clock_index_a}
                       ├─ panel.yaml   vars: lvgl_b, dc_b, ${clock_index_b}
                       └─ panel.yaml   vars: lvgl_c, dc_c, ${clock_index_c}
                            one LVGL instance + one lvgl_clock widget
```

| File | What it is |
| --- | --- |
| `common_base_esp32_s3_xiao.yaml` | Board, PSRAM, logger, the pin substitutions, colours, heap sensors. **No network** — that lives in `board_d.yaml` |
| `panel.yaml` | **One** panel: its LVGL instance and its single mini-clock, with every widget setting. Included three times |
| `common.yaml` | The SPI bus, the three `display:` entries, and the three `panel.yaml` includes |
| `common_slave.yaml` | The listener half: UART RX, the `lvgl_clock` time platform, and the hostname |
| `board_d.yaml` | **The master.** Wi-Fi, SNTP, UART TX, the boot-phase animation, the temperature sensor, clocks 7/9/11 |
| the other seven `board_*.yaml` | The listeners. Each is four lines: an include and its three clock indices |
| `secrets.yaml.example` | Copy to `secrets.yaml` (gitignored). Only `board_d.yaml` reads it |

#### Two different include mechanisms, and why both are needed

**`packages:` merges a file in wholesale.** That is how the role files pull in
`common.yaml` and how `common.yaml` pulls in the base — each layer adds keys,
and a later `substitutions:` block overrides an earlier one. It is composition,
and it happens once per file.

**`!include` with `vars:` parameterises a file, so it can be included more than
once.** `panel.yaml` is a complete panel — LVGL instance, widget, choreography
settings, `render_interval`, `cycle_modes`, the lot — and `common.yaml` pulls
it in three times, changing only four things each time:

```yaml
panel_a: !include
  file: panel.yaml
  vars:
    lvgl_id: lvgl_a
    display_id: my_display_a
    widget_id: dc_a
    clock_index: ${clock_index_a}
```

This is the distinction that makes the layering work: **`substitutions:` are
global to the build, `vars:` are local to one include.** There is exactly one
`clock_index` in a substitution namespace, so three panels could never each
have their own — but three includes can each carry their own `vars:`. The
result is that ~60 lines of widget configuration exist once, not three times,
and adding a fourth panel would be four lines in `common.yaml` rather than
another copy of the widget.

#### What overrides what

| Substitution | Default | Overridden by |
| --- | --- | --- |
| `clk_pin`, `cs_pin_a`… | `common_base_esp32_s3_xiao.yaml` | the command line, if your wiring differs |
| `clock_index_a/b/c` | `common.yaml` (0 / 2 / 4) | every `board_*.yaml` — this is all a listener file contains |
| `clock_mode` | `board_d.yaml` (`time`) | `-s clock_mode demo`, master only |

Because substitutions resolve at codegen, `-s` works on any of them without
editing a file:

```bash
esphome -s clock_index_a 19 -s clock_index_b 21 -s clock_index_c 23 \
        run board_d.yaml
```

So a listener board file really is just its column:

```yaml
packages:
  slave: !include common_slave.yaml

substitutions:
  clock_index_a: "1"
  clock_index_b: "3"
  clock_index_c: "5"
```

The hostname is derived from the first index inside `common_slave.yaml`
(`cc24-clock-${clock_index_a}`), so even that does not have to be repeated.

> **One YAML trap worth knowing.** A substitution inside a *flow* sequence
> fails to parse — `displays: [${display_id}]` makes the parser read `${` as
> the start of a flow mapping. Write it in block form instead:
>
> ```yaml
> displays:
>   - ${display_id}
> ```

## First prototype

Twelve clocks on four boards, before any of it went into a case — the stage
where everything is still reachable. Photos and notes in
[**`PROTOTYPE.md`**](./PROTOTYPE.md).
