# ClockClock 24 — mode sandbox

A browser port of the `clockclock24` engine from
[`components/lvgl_clock/lvgl_clock.cpp`](../../components/lvgl_clock/lvgl_clock.cpp),
for designing choreographies without a flash cycle. Eight boards, three panels
each, a two-minute build and a wall you have to walk over to — the loop is slow
enough that a new mode takes an evening. Here it takes a reload.

**Open [`index.html`](./index.html) in a browser.** No server, no build step, no
dependencies.

```
tools/clockclock24-sim/
  index.html   the wall, and the controls
  engine.js    constants, glyph tables, one function per choreography
  wall.js      the mode state machine: entry blend, settle back to time
  pattern.js   the pattern editor's model: per-clock motion, relative speeds
  check.js     headless "nothing jumped" regression check
  preview.py   ASCII render of a pose, for sketching glyphs in a terminal
```

## Why a port and not a rewrite

`engine.js` is a **deliberate line-for-line translation** of the C++, not an
approximation. Same constants, same tables, same per-frame maths, same variable
names where the language allows. `this->cur_[i]` becomes `cur[i]`;
`wall_pos_(c, col, row)` becomes `wallPos(c)`. That is the whole point: a mode
that looks right here is transcribed back into `lvgl_clock.cpp` by changing
syntax, not by re-deriving behaviour.

Keep it that way. The moment the two drift apart the sandbox stops being
evidence about the wall.

## Writing a new mode

One function in `engine.js`, taking `(cur, t, ctx)` and writing 48 angles:

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

Register it in `MODES` **and** in the `window.CC` export block, both at the
bottom of the file, and it appears in the UI. Give its label a trailing `*`
while it lives here only — **the marker means "sandbox only, not in the
firmware"**, and it comes off when the mode is ported. Then:

- **Angles are degrees, 0 = 12 o'clock, clockwise.** `wrap360()` normalises,
  `shortestDelta(a, b)` gives the signed way round in `(-180, 180]`.
- **Hand index is `clock * 2 + hand`**, and `clock = digit * 6 + cell` with
  `cell = row * 2 + col`. `wallPos(c)` converts a clock number to its
  `{ col, row }` on the 8 × 3 wall — use it for anything that should travel
  across the wall rather than per digit.
- **Never write a discontinuity.** Your function is sampled every frame and the
  output is the hand position, so a `%` wrap that steps 360 → 0 is a hand
  teleporting. Wrap the *phase*, not the angle, and give each hand its own
  `fmod` rather than scaling a shared one — that exact mistake once made `wind`
  leap 145° in a single frame.
- **Start uniform.** The entry blend holds `t` at 0 until it finishes, so
  whatever your function returns at `t = 0` is the pose the whole wall fades
  into. If that pose differs per column, the wall never looks like it starts
  together.
- **Ramp, do not snap.** The existing modes integrate a rate over a ramp
  (`turned = rate * x * x / (2 * RAMP)` while spinning up) so position stays
  continuous while speed is still changing.

## Edit mode — posing by hand

Designing a picture (a cat, a letter, an arrow) by writing angle tables and
guessing is slow. **Press `Edit`** and pose the wall directly instead:

- **The first click selects, and only selects.** Clicking a clock to look at
  its settings used to nudge a hand by however far the pointer sat from it,
  which quietly edited the pose every time you inspected something. Drag from
  an **already-selected** clock to move its hands.
- **Drag anywhere inside a clock** to swing a hand. It grabs whichever hand is
  already nearest the pointer, so you never have to hit a 2 px line.
- **Snaps to 15°** by default — 5°, 45° and 1° are in the dropdown. 15° is
  usually what you want: it is the granularity the glyph tables are written at,
  and it keeps strokes meeting cleanly at cell edges.
- **`both hands`** drags the pair together, preserving the angle between them —
  for rotating a whole stroke without rebuilding it.
- **`Copy TL → all`** copies the top-left clock's two angles to all 24. The
  fastest way to start from a uniform wall, which is what every choreography
  wants at `t = 0`.
- **`Park all`** clears to `PARK` (225°). Note that parked is not *off* — the
  hands are physical, so a parked cell still shows one short diagonal. That is
  how the real thing blanks a cell, and it is why glyphs have to read against a
  faint diagonal background rather than black.
- **`clock numbers`** labels each cell with its index, and the selected clock
  is ringed.

Entering edit mode pauses the animation, since otherwise the choreography
overwrites what you just posed.

### Getting a pose back out

`Copy JS` and `Copy CPP` put the current 48 angles on the clipboard and in the
box below the buttons.

- **JS** is `[row][col]` on the 8 × 3 wall — the way a full-wall picture reads,
  and what a sandbox mode wants.
- **C++** is clock-index order (`digit * 6 + cell`), formatted to drop straight
  in beside `FONT` and `LOVE` in `lvgl_clock.cpp`.

`PARK` is emitted by name in both, not as `225`.

## `rotating_maze`

A chevron per clock, alternating by **column** — even columns point down
(135 / 225), odd columns point up (45 / 315) — turning at one rate, with the
**direction alternating by row**: rows 0 and 2 clockwise, row 1
counter-clockwise.

```js
const sense = (row & 1) ? -1 : 1;                    // -1 = counter-clockwise
const base  = (col & 1) ? [45, 315] : [135, 225];    // up chevron : down chevron
cellAt(cur, row, col, wrap360(base[0] + sense * turned),
                      wrap360(base[1] + sense * turned));
```

**Both hands of a clock always turn the same way**, so the pair stays a rigid
90° chevron and simply rotates — it never opens or shuts in place. At `t = 0`
the wall is a clean interlocking pattern; the counter-rotating rows then shear
past each other and it opens and closes through it.

### The turn is not constant

It **crawls through the aligned figures and accelerates between them**, which
is what makes it read as a maze forming and dissolving rather than a wall of
spinning hands. Done as a **time warp** rather than a velocity, because
integrating a speed that depends on angle is an ODE while bending the phase is
closed-form:

```js
rate(x) = (1 - d·G(x)) / (1 - d·mean(G))        d set from MAZE_FLOOR
G(x)    = ((1 + cos x) / 2) ^ MAZE_SHARP        x = N·(u - phi)
```

The profile is a **narrow bump, not a cosine**. A plain cosine
(`MAZE_SHARP = 1`) sits near its minimum for most of the cycle — 28% of every
segment below half speed — which is why it read as *stopping* on each figure
rather than easing past it. Raising the exponent narrows the dip without
deepening it.

`G` has a closed-form integral at any integer exponent, which is what keeps
this exact rather than a lookup table:

```js
G(x)  = cos^2k(x/2) = 4^-k [ C(2k,k) + 2·Σⱼ C(2k,k-j)·cos(jx) ]
IG(x) = 4^-k [ C(2k,k)·x + 2·Σⱼ C(2k,k-j)·sin(jx)/j ]
```

so `theta(u) = (u - (d/N)·IG(N·(u - phi))) / (1 - d·mean(G))`, and the minima
land exactly on `theta = phi + k·360/N`.

**The knob is the floor, not the depth.** `MAZE_FLOOR` is the slowest rate as a
fraction of the average — the thing you actually judge by eye — and the dip
depth is derived from it:

```js
floor = (1 - d) / (1 - d·mean)   ⇒   d = (1 - floor) / (1 - floor·mean)
```

Any floor in `(0, 1]` gives `d ≤ 1`, so the rate can never go negative and the
hands can never run backwards. That failure mode is now unreachable by
construction rather than by remembering a bound.

| Constant | Now | |
| --- | --- | --- |
| `MAZE_TURN_S` | 20 s | One revolution **on average** — a dwell every 5 s |
| `MAZE_STOPS` | 4 | Slow-downs per revolution — one every 90° |
| `MAZE_FLOOR` | 0.40 | Slowest rate, as a fraction of average. Lower = more of a pause |
| `MAZE_SHARP` | 8 | How narrow the ease-off is — 1 is a plain cosine, higher is briefer |
| `MAZE_PHASE_DEG` | 0 | Which angle the slow-downs land on |

Measured over a revolution:

| | share of average | at 20 s / revolution |
| --- | --- | --- |
| Average | 100% | 18 °/s |
| Slowest | 40% | **7.2 °/s** |
| Fastest | 115% | **20.6 °/s** |
| Below half speed | 8.5% of a segment | 0.21 s of each 2.5 s figure |

So it eases briefly as each figure lines up and holds a near-steady pace the
rest of the way, instead of dragging through most of the segment. The sprint
got gentler as a side effect — 115% rather than 180% — because a narrower,
shallower dip leaves less time to make up elsewhere.

### Which figures it dwells on

The hands land on a 45° multiple every **45°** of turn, which gives eight
**aligned** figures per revolution, alternating between two families:

| θ | Row 0 hands | Figure |
| --- | --- | --- |
| 0, 90, 180, 270 | `135/225`, `45/315` | Chevrons — `< >` pairs, a **diagonal lattice** |
| 45, 135, 225, 315 | `180/270`, `90/0` | Right angles, an interlocking **rectangular grid** |

**Only the diagonal family gets a dwell** — `MAZE_STOPS = 4` with
`MAZE_PHASE_DEG = 0`. That is the family where every hand sits on a diagonal,
and it is also the base pose the mode starts from. Dwelling on both families
(`STOPS = 8`) put a pause every 45° and made neither stand out.

| `MAZE_STOPS` | `MAZE_PHASE_DEG` | Dwells on |
| --- | --- | --- |
| **4** | **0** | Diagonal chevrons only — **current** |
| 4 | 45 | Axis-aligned right-angle grids only |
| 8 | — | Every aligned figure |
| 16 | — | Aligned *and* the half-formed states between them |

Two traps came out of moving this constant off zero, both fixed and both
invisible while it was 0:

> **It is an output angle, so it has to be scaled before use.** The offset
> applies to the *linear* phase, which the normalisation then divides by
> `(1 − dwell·mean)` — asking for 45° silently produced dwells at 51.6°.
>
> **A non-zero phase moves the start.** `mazeRaw(0)` is then not 0, so the mode
> would come up part-way round instead of on its base pose. `MAZE_U0` bisects
> for the phase where the turn is zero and starts there — which fixes the start
> *without* moving the dwells, as subtracting `mazeRaw(0)` would have done.

The rhythm per dwell is `MAZE_TURN_S / MAZE_STOPS` = **5 s**.

At a 20 s revolution the whole eight-figure cycle fits inside a 35 s
`cycle_modes:` window with room for the fade in and the settle back to the
time — so the wall shows the complete sequence rather than a slice of it.

Wrap the **linear** phase, not the output: `theta(u + 2π) = theta(u) + 2π`, so
the seam is a whole turn and no hand steps.

**Frozen.** This one is settled; work new ideas in `test` instead.

Two things in there are worth copying into any new mode:

- **Hold the two hands a fixed angle apart and rotate the pair as a rigid
  body** to make a clock read as one shape — a chevron, a stroke, an arrow —
  instead of two loose hands. Send one hand each way instead and the shape
  opens and shuts in place rather than turning — a different look entirely, and
  not this one.
- **Wrap the turn, not the final angle.** `turned` is reduced mod 360 and then
  added to a base; the angle itself is never reset. Reducing the *output* is
  what produced the 145°-per-frame leap that `wind` once had.

**Sandbox only** — not in the firmware. Port it with `Copy CPP` when it earns
its place.

## `zipper`

A **travelling chevron**. The wall rests as one field of
top-left-to-bottom-right diagonals, and a front of mirrored chevrons crosses
it from left to right. **Every row does the same thing**, so the figure repeats
down each column and the front is one vertical band:

```
t=0.0   \ \ \ \ \ \ \ \        \ \ \ \ \ \ \ \        \ \ \ \ \ \ \ \
t=1.5   > < > \ \ \ \ \        > < > \ \ \ \ \        > < > \ \ \ \ \
t=3.0   \ \ \ < > < \ \        \ \ \ < > < \ \        \ \ \ < > < \ \
t=5.5   \ \ \ \ \ \ \ \        \ \ \ \ \ \ \ \        \ \ \ \ \ \ \ \
          row 0                    row 1                    row 2
```

The rest pose puts the hands **180° apart** (315 and 135), so a column at rest
is one straight stroke. A pass swings each hand 90°, turning `\` into `/`, and
the next pass swings it back.

**Four things make the figure, and it does not appear without any one of
them.** Each was a failed attempt first:

| | Why |
| --- | --- |
| The swing is **90°, not 180°** | At 180° the chevron's opening rotates all the way round — `>` then `v` then `<` then `^` — so no single shape is on screen for more than an instant |
| Hand 1 starts **after hand 0 finishes** (`HAND_LAG > FLIP`) | Overlap them and they are never more than ~40° apart: a slightly bent line, not a chevron. Separated, the stroke opens to a full right angle and **holds** for `HAND_LAG − FLIP` |
| The swing goes **out and back**, not round | Accumulating leaves each pass starting from a base 90° further on, so the second pass opens downwards (`v`/`^`) instead of sideways |
| **Which hand leads alternates by column** | Whichever hand goes first drags the stroke open towards its own side, so the front is `> < > <` rather than eight identical chevrons |

Measured: the chevron opens to a **full 90°** and holds within 5° of that for
**0.65 s** per pass.

**The rows are not differentiated, and two attempts to differentiate them were
wrong:**

- **Reversing the middle row's travel direction** puts the rows at different
  columns, so the wall stops reading as a single front crossing it.
- **Flipping the middle row's swing sign** turns its chevrons through a right
  angle (`v`/`^` against the neighbours' `>`/`<`), which breaks each column
  into three unrelated marks instead of one repeated figure.

The alternation that matters is **by column, not by row**.

| Constant | Now | |
| --- | --- | --- |
| `ZIP_FLIP_S` | 0.9 s | One hand's 90° swing |
| `ZIP_HAND_LAG_S` | 1.2 s | Hand 1 behind hand 0. **Must exceed `FLIP`** — the difference is the hold |
| `ZIP_COL_LAG_S` | 0.5 s | How fast the front crosses the wall |
| `ZIP_GAP_COLS` | 9 | Gap between one front and the next, in columns |
| `ZIP_DRIFT_DEG` | 30 | How far a resting column leans before the next pass |

**The resting field is never quite frozen.** Between passes a column leans
slowly, the drift applied to *both* hands so the stroke stays straight and
simply tilts. Direction comes from the pose it is currently resting on — one
way on `\`, the other on `/` — and since a pass flips it between the two, the
drift reverses every pass. That is what keeps it **bounded**: it sways between
0 and `ZIP_DRIFT_DEG` and back, rather than a constant creep that would have
the field far off its diagonal within a minute.

`ZIP_DRIFT_DEG` also sets the speed, since the lean ramps across the whole
rest: the rate is `ZIP_DRIFT_DEG / ZIP_HOLD_S` ≈ 5.3 °/s.

`ZIP_HOLD_S` is **derived**, not set: `HAND_LAG + GAP_COLS × COL_LAG`. A
column is in motion for `HAND_LAG + FLIP` = 2.1 s, which at `COL_LAG` is a front
about **four columns wide**; resting a further `GAP_COLS` worth on top sets how
closely the fronts follow each other.

At 9 the gap is wide enough that a front finishes crossing before the next sets
off, so there is only ever **one front on the wall** — it arrives, passes
through, and the wall is a clean field of diagonals again before the next
starts. Measured: 4 of 8 columns moving at once, a column in motion 23% of the
time. Dropping to 2 keeps a front on the wall permanently with the next already
coming in behind it (6 of 8 moving, 59% duty), which reads as much busier.

Expressing it as a gap in **columns** rather than a hold in seconds is the
point: it is what you actually see, and it stays correct when `COL_LAG` or
`FLIP` change.

Two more things this one needed:

- **`zipSwing()` returns 0 for negative time.** The columns ahead of the front
  have a lag that puts them at negative `x` when the mode starts, so without
  the clamp they would already be part-way through a swing at `t = 0` and the
  wall would not begin as one clean field for the entry blend to fade into.
- **`ZIP_FLIP_S` cannot go much below 0.9 s.** An eased swing peaks at 1.875×
  its average rate, `mode_speed` scales that, and the settle back to the time
  adds its own sweep on top. Measured at 0.9 s: 6.2 °/frame at ×1, 19.4 at ×3 —
  just inside the 20 °/frame ceiling in `check.js`. Tightening
  `ZIP_GAP_COLS` eats into that margin, so the two are linked.

The name is the mechanism: mirrored chevrons opening and closing as a front
runs across the wall is a zip being undone and done up again.

**Sandbox only** — not in the firmware. Port it with `Copy CPP` when it earns
its place.

## `mirror_wave`

Every clock rests as **one vertical stroke** — both hands on the 12–6 line —
then scissors open, the two hands parting like a pair of dividers. The wall is
**mirrored about its centre line**, so the left half opens right and the right
half opens left. The **middle row scissors the other way** from the two around
it, so a column reads as three alternating chevrons rather than three doing the
same thing, and the rows shear against each other as they open.

It starts in the middle and spreads outwards:

```
t=1.2    | | | | | | | |      | | | < > | | |      | | | | | | | |
t=2.4    | | | > < | | |      | | < < > > | |      | | | > < | | |
t=3.6    | | > > < < | |      | < < < > > > |      | | > > < < | |
t=5.4    > > > > < < < <      < < < < > > > >      > > > > < < < <
           row 0                 row 1                 row 2
```

1. The two centre columns, **middle row** first.
2. Their top and bottom rows, `MIRROR_ROW_LAG_DEG` behind.
3. Each ring outwards — (2, 5), then (1, 6), then (0, 7) — one
   `MIRROR_COL_LAG_DEG` behind the ring inside it.

**Within a row** every clock turns at the same rate forever, so the offsets
picked up on the way out are fixed: a standing fan, widest in the middle, never
bunching and never overtaking. Measured separation across the middle row once
it is running, at `t = 4`:

```
row 0    41  59  77  95  95  77  59  41
row 1    68  92 116 140 140 116  92  68
row 2    41  59  77  95  95  77  59  41
```

12° between rings is a **shallow** fan — the wall opens close to together, with
the middle only a little ahead. Raise `MIRROR_COL_LAG_DEG` and the spread
becomes a visible ripple travelling out from the centre; at 25° the outer
columns were still shut while the middle was half open.

**Across rows it is not fixed.** The top and bottom rows run at
`MIRROR_OUTER_RATE` of the middle one — 75%, so 15 °/s against 20 °/s — and
therefore fall steadily further behind rather than holding a constant offset.
The three rows beat against one another, coming back into step every
`MIRROR_TURN_S / (1 − MIRROR_OUTER_RATE)` = **36 s**. That is the one thing
here with a period longer than a single open-close, so it is what stops the
mode looking like a loop.

180° of travel returns both hands to the vertical — hand 0 lands where hand 1
was — so the figure closes back up and repeats with no seam to hide.

| Constant | Now | |
| --- | --- | --- |
| `MIRROR_TURN_S` | 9 s | 180° of hand travel: one open-close |
| `MIRROR_COL_LAG_DEG` | 12° | Between one ring and the next one out |
| `MIRROR_ROW_LAG_DEG` | 7° | Extra for the top and bottom rows |
| `MIRROR_RAMP_S` | 1 s | Spin-up, per clock |
| `MIRROR_OUTER_RATE` | 0.75 | Rows 0 and 2, as a fraction of the middle row's rate |

The lags are written in **degrees** and divided by the rate, not stored as
delays — so the look is tuned by the angle you want between neighbours, and
stays correct when `MIRROR_TURN_S` changes.

## `pattern` — the pattern editor

A mode with no code of its own: **24 per-clock motion specs you build in the
UI**. Pick the `pattern` mode, press **Edit**, and a *Pattern* card appears —
it is hidden otherwise, since every control writes into the selected clock and
there is no selection when you are not editing.

Each clock carries:

| | |
| --- | --- |
| **Home pose** | What you configured. Drag the hands on the wall — in `pattern` the drag writes into the spec, not just the frame, so it sticks |
| **Direction**, per hand | `←` counter-clockwise · `—` still · `→` clockwise |
| **Speed**, per hand | `fixed` 0…1, or `same as…` a neighbour ± an offset |

Speed 1.0 is `PATTERN_MAX_RATE` = 90 °/s, one turn in 4 s.

### Editing while it moves

Edit mode normally freezes the wall so a posed hand stays where you put it. The
pattern editor is the exception — its controls are all about motion, and you
cannot judge a speed you cannot see — so **`run motion while editing`** keeps
the animation going. It is on by default and applies only in `pattern`.

That needs one correction to work. With the motion running, storing a dragged
angle straight into the pose would snap the hand to wherever the motion had
carried it: you would be aiming at a moving target. So the pose is stored as
**where the hand is now, minus how far the motion has carried it** —

```js
setPoseAt(i, hand, angle, ts)   //  a = angle − dir · speed · RATE · ts
```

— and the hand lands exactly where you dropped it, then carries on. `Pose from
wall` applies the same correction, so capturing a moving wall does not bake the
offset in twice.

### Why editing a speed does not jump

The same maths bites harder on the motion controls. An angle is measured from
`t = 0`, so **changing a speed retroactively rewrites the whole history**: a
clock that has been running 10 s at 0.5 leaps 90° the instant you nudge it to
0.8. And because speeds can be relative, one edit moves clocks you never
touched.

So every motion edit goes through `rebaseAll()`: remember where all 48 hands
are, apply the change, then re-anchor all 24 poses so they are still there. The
hands carry on from where they were, at the new speed — measured, 0° of jump
against 90° without it.

It anchors to the **pattern's own output**, not to `wall.cur`. `wall.cur` is the
pattern *plus* whatever the entry blend or the settle is still adding on top, so
anchoring to that bakes the blend into the pose and the wall snaps by however
much the blend was contributing the moment it finishes — **135°**, measured, if
you touch a control during the fade-in. Toggling `run motion while editing` rebases too, so it
resumes from where the hands are rather than from where they would have been
had it never stopped.

It is an analogue clock even while you are editing it.

### Relative speed is the point

`same as… left − 0.12` means *take my left neighbour's speed for this hand and
subtract 0.12*. Set one clock going and the rest of the wall derives itself:

```
row 0 resolved speeds:  1.00  0.88  0.76  0.64  0.52  0.40  0.28  0.16
```

That is one fixed clock at 1.0 and seven relative ones, and it is how you get a
gradient without typing eight numbers. Neighbours are `left` / `right` / `up` /
`down`; a reference that runs off the edge of the wall resolves to 0.

**The offset is per hop, and it compounds.** Each clock adds it to its
neighbour's *resolved* speed, so across an 8-wide wall the total is seven times
what you set — which is why a value that looks small runs the far end down to a
standstill:

```
offset -0.05 ->  1.00 0.95 0.90 0.85 0.80 0.75 0.70 0.65
offset -0.10 ->  1.00 0.90 0.80 0.70 0.60 0.50 0.40 0.30
offset -0.20 ->  1.00 0.80 0.60 0.40 0.20 0.00 0.00 0.00
offset -0.30 ->  1.00 0.70 0.40 0.10 0.00 0.00 0.00 0.00
```

The usable range for a gradient that spans the wall is therefore about
**±1/7 ≈ ±0.14**, so the slider is capped at ±0.2 in steps of 0.01 rather than
spanning ±1 — at the old range the entire useful zone was a few pixels of
travel. Both speed readouts are in **°/s**, since 0…1 means nothing on its own.

**Chains can loop** — A takes B's speed and B takes A's. That resolves to 0 and
stops rather than recursing forever, so a mistake costs you a still clock, not
a hung tab.

### Copying

**Copy** takes the selected clock's pose *and* motion; then **Paste** to one
clock, **its row**, **its column**, or **all 24**. Building a wall usually
means posing one clock, giving it the motion you want, and pasting it out —
then going back to the few that differ.

**Copy JS** exports all 24 as an annotated array, one clock per line with its
column and row, ready to paste into `engine.js` as a fixed mode.

### Home pose vs runtime anchor

Each clock stores **two** poses, and the difference matters:

| | Changed by | |
| --- | --- | --- |
| **home** | dragging a hand · `Pose from wall` | What you configured. Nothing else touches it |
| **anchor** | every speed / direction edit, via `rebaseAll` | Where the motion is measured from, re-cut constantly so the hands never jump |

If there were only one pose, the thing you configured would be quietly
overwritten the first time you nudged a slider, and there would be nothing left
to go back to. Verified: configure 90°, run 8 s, edit the speed — the hand stays
put, the anchor moves to 162°, and **home is still 90°**.

**Back to pose** re-cuts every anchor from home, so the hands land on the poses
you chose. **Pose from wall** is its opposite: take wherever the hands have got
to and make *that* home.

Both are **disabled while the motion is running** — they rewrite poses, which
only makes sense against a still wall; against a moving one you would be aiming
at a moving target and could not see the result. Pause, or untick `run motion
while editing`, and they light up. With the motion off the loop is not ticking,
so both push the pattern into the frame by hand rather than waiting for a
redraw that will not come.

## `test` — the scratch bench

Sandbox-only and deliberately empty: every clock at 12 and 6, uniform and still.
Nothing depends on it, so overwrite the body freely. When something in there is
worth keeping, copy it out under its own name — which is how both
`rotating_maze` and `zipper` came about — and leave `test` back at the neutral
pose.

## What the sandbox actually simulates

`wall.js` is the part that makes it an analogue clock rather than a display:

- **Mode entry** (`blendIntoMode`) — each hand's offset from the choreography's
  first frame is measured **once**, then faded out with `ease()`, staggered by
  wall column. Measuring once is what stops it flipping direction mid-fade.
- **The settle** (`settleBlend`) — leaving a mode, the choreography keeps
  running underneath while the remaining delta is faded out with `easeOut()`,
  which starts at full speed because the hands are already moving. The delta is
  carried forward and reduced by the animation's own movement, so the target
  stays pinned.
- **Digit sweeps** (`plainSweep`) — `movement:` applies here and only here.
  Leaving a choreography always takes the shortest way round regardless.

Not simulated, because none of it changes how a choreography looks: UART sync,
the `cycle_modes:` scheduler, SPI/frame-rate limits, PSRAM.

## The no-jump check

> It is an analogue clock. It simply cannot jump.

Every regression in this engine has been the same regression, so it has a test.
`check.js` drives every mode through its full lifecycle — enter from the time,
run, settle back — at several transition lengths and speeds, and reports the
largest single-frame movement of any of the 48 hands.

```bash
/System/Library/Frameworks/JavaScriptCore.framework/Versions/A/Helpers/jsc check.js
```

(macOS ships JavaScriptCore, so this needs nothing installed. `node check.js`
works too if you swap the two `load()` calls for `require()`.)

A normal sweep is a few degrees per frame at 30 fps; a jump is 90–180. The last
block runs with `transition 0`, which **must** jump — that is the documented
meaning of a zero transition, and it doubles as proof the detector works.

```
transition 5 s, mode_speed 1.0
  ok  wave         enter+run 1.69    settle 3.94   deg/frame
  ok  wind         enter+run 2.81    settle 6.05   deg/frame
...
PASS — nothing jumped
```

## Not the CodePen

This was asked for as a local copy of
`codepen.io/Lorti/pen/XpQewQ`, which is Cloudflare-blocked — the pen page and
all three `.html` / `.css` / `.js` asset URLs return 403, so none of its source
is in here. For prototyping modes that turned out to be the better outcome
anyway: what you want to test against is *this* wall's font table, geometry and
blend behaviour, not another implementation's.
