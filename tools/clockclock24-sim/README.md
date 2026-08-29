# Writing a choreography in code

A browser port of the `clockclock24` engine from
[`components/lvgl_clock/lvgl_clock.cpp`](../../components/lvgl_clock/lvgl_clock.cpp),
for designing choreographies without a flash cycle. Eight boards, three panels
each, a two-minute build and a wall you have to walk over to — the loop is slow
enough that a new mode takes an evening. Here it takes a reload.

> **This page is for adding a mode to the firmware.** If you want to make the
> wall do something new *without* touching C++, you want a **pattern** instead —
> data rather than code, drawn in the
> [pattern editor](../../homeassistant/DOCS.md#the-pattern-editor) and sent to
> the wall with nothing recompiled. See [Modes and patterns](../../modes.md).

**Open [`index.html`](./index.html) in a browser.** No build step and no
dependencies — though it now needs a server rather than `file://`, because it
loads the two cards from `homeassistant/cards/` by relative path. Anything will
do: `python3 -m http.server` from the repo root, or the published site.

It is also published with the project site, so it can be linked to and used
without cloning anything — this folder's URL *is* the running app, and this
README sits beside it at `README.html`.

The page itself is **not a third implementation**. It mounts the same two
custom elements the Home Assistant add-on does — `clockclock24-card` for the
wall and `clockclock24-editor-card` for the editor — loading the same five
source files the add-on's bundle concatenates. The front page of the site does
the same. There is one editor in this repo, and fixing it fixes all three.

```
tools/clockclock24-sim/
  index.html   the page: the wall, the mode chips, the editor
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

## Posing by hand

Designing a picture — a cat, a letter, an arrow — by writing angle tables and
guessing is slow. Pose the wall directly instead, in the **pattern editor** on
[the page itself](./index.html): click a clock to select it, shift-click for
several, drag inside one to swing a hand. The controls, the selection helpers
and the copy scopes are documented once, with the editor, in
[the add-on's reference](../../homeassistant/DOCS.md#the-pattern-editor) — it is
the same editor.

A pose you like becomes a **pattern**, which the firmware plays as data. It is
not a route to a new *mode*: a mode is code, and that is what the rest of this
page is about.

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

## `pattern` — patterns are data, not a choreography

`pattern` is the one mode with no code of its own: **24 per-clock poses and
speeds**, authored in a UI and carried to the wall as one line of text. Every
hand is `pose + direction × speed × rate × t`, which is continuous for any data
whatsoever — so a pattern cannot make a hand jump however badly it was drawn.
That is why this mode can safely take its input from a text field.

Speed 1.0 is `PATTERN_MAX_RATE` = 90 °/s, one turn in 4 s.

**The editor lives in the Home Assistant add-on**, wired to a slot on the
master: draw it, watch it in this same engine, press **Send**, and the wall is
running it a second later. See
[the pattern editor](../../homeassistant/DOCS.md#the-pattern-editor) — that page
also covers relative speeds, the squared sliders, and why editing a speed does
not make the hands jump.

> A standalone editor that needs no Home Assistant is planned. Until it lands,
> the sandbox here is for **writing choreographies in code** — the sections
> above — rather than for drawing patterns.

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
