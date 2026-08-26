# Digital Clock Clock 24

Run the wall from the sidebar: pick a mode, set the cycle, draw new patterns
and send them straight to the master.

Everything here is a Home Assistant entity on the master, which is an ESPHome
node already on your network. The add-on finds it, shows those entities as
something nicer than a list of rows, and writes back to them. It never talks to
the hardware directly and does not need to.

## Two things it is for

**A ClockClock 24 with no hardware at all.** The clock card renders the whole
wall in a dashboard, and **Full screen** at the top of the panel opens a
full-bleed version for a wall tablet — no dashboard, no card resource, nothing
to configure. It runs the same engine as the firmware.

**Driving the real wall, if you built one.** Mode, pattern slot, cycle
interval, the cycle list, and eight pattern slots you can draw into.

## Options

| Option | Default | What it is |
|---|---|---|
| `install_card` | `true` | Copy the Lovelace bundle into `/config/www` on start, so the cards are at `/local/`. Off, and `/config` is never written to. |

That is the whole of it. The board is discovered, not configured — see below.

## Finding your wall

`board_d.yaml` declares a project marker:

```yaml
esphome:
  name: cc24-board-d
  project:
    name: "tuct.digitalclockclock24"
    version: "1.1"
```

Home Assistant splits `project.name` on the dot into **manufacturer** and
**model**, so the master registers as manufacturer `tuct`, model
`digitalclockclock24`. The **Board** dropdown matches on the **model** — that
is the half that says what the thing is; the manufacturer says who wrote it and
would match every other project of theirs.

It is a fact the device carries, not a guess at what somebody called an entity.
A house full of `text.` entities has nothing else that identifies a wall.

**If your master was flashed before this marker existed**, reflash it. Until
then the add-on falls back to shape: any device offering a Mode select whose
options include `time` and `wave`, alongside pattern text fields, is still
listed — just without the **master** badge.

More than one wall is fine; they all appear in the dropdown.

## The wall panel

| | |
|---|---|
| **Mode** | What the wall is doing right now, and a way to change it. This is an **override**: with a cycle interval set, the next window opens on schedule and takes the wall back. Set the interval to `off` to make a choice stick. |
| **Pattern** | Which slot `pattern` mode draws. Each chip is labelled with that slot's own pattern name, because "3" tells you nothing. Empty slots fall back to showing the time. |
| **Cycle every** | How often a window opens. The 35 s window itself is fixed — only the cadence is yours. `off` stops the rotation entirely. |
| **Cycle list** | The rotation, in order. **Repeats count**: listing `temp` every other entry gives it half the slots. The chip matching the current mode is outlined. |
| **Reload patterns from firmware** | Throws away everything written from Home Assistant and goes back to the `patterns/` folder as compiled in. The way out of a bad edit. |

The Mode select republishes every second, so the wall moving itself on shows up
here without you touching anything. Names the master does not recognise are
dropped from a cycle list with a warning and it republishes what it kept — so a
typo shows up rather than silently changing the wall.

## The tablet view

**Full screen**, top right, opens a page that is the wall and nothing else:
black, edge to edge, controls fading out after three seconds. Point a tablet at
it and leave it there.

By default it **mirrors** the board selected in the panel — the wall changes
mode, the tablet follows about three seconds later. It only rebuilds when the
mode has actually changed, because restarting the animation every poll would
stutter.

Query string, all optional:

| | |
|---|---|
| `?mirror=1` | Follow the real wall. Off, it cycles on its own |
| `?board=<id>` | Which board to follow. Default: the first master found |
| `?mode=wave` | A fixed mode instead of the rotation |
| `?cycle=a,b,c` | The rotation to run |
| `?interval=120` | Seconds between windows |
| `?gap=0` | Digit gap, in clock widths |

With no wall on the network it says so and keeps running on its own — a tablet
that goes blank because a device is offline is a worse tablet.

> Inside Home Assistant this page is an iframe, and the browser's true
> full-screen mode is the frame's to grant, not ours. The page is full-bleed
> either way; open it in its own tab if you want the browser chrome gone too.

## The pattern editor

The editor is the real firmware engine running in the browser: the same
choreographies, the same easing, the same 24-clock geometry.

1. Pick the slot to **write to**.
2. **Click** a clock to select it, **shift-click** for several; every edit then
   applies to all of them. **Drag** its hands to pose it, snapped to 15°.
3. Set a **direction and speed per hand**. The speed slider is squared, so the
   slow end — where a pattern actually reads — gets half the travel.
4. It **runs while you edit**. **Back to pose** puts every hand back on what you
   configured, not on wherever the motion drifted to.
5. **Copy to all 24**, or to the primary clock's row or column.
6. **Send to wall.**

A packed 24-clock pattern is about 164 characters against the master's 255, so
the whole wall fits in one text field with room to spare.

> **Send overwrites that slot**, which the master saves to flash and pushes to
> all seven listeners.

### What it does not do

It does not generate ESPHome YAML, it does not compile and it does not flash.
Those belong to the ESPHome add-on, which is already installed next to this. A
pattern needs none of them: it is data, and the master takes it over the
network at runtime.

The board configs for a real wall are in
[`digital_clock_clock_24_24_round_screens/`](../digital_clock_clock_24_24_round_screens/README.md).

### What it is allowed to do

An add-on with access to Core can call any service. This one calls exactly
three — `text.set_value`, `select.select_option` and `button.press` — and only
on entities in the matching domain. Anything else is refused before it leaves
the add-on.

## The cards

`install_card: true` puts the bundle at `/local/clockclock24-card.js`. Register
it once — **Settings → Dashboards → ⋮ → Resources → Add**, type **JavaScript
module** — and both cards are available.

### The clock card

The whole wall in a dashboard, running the same engine as the firmware. Useful
on its own — a ClockClock 24 on a wall tablet, no hardware — and useful
alongside the real thing.

```yaml
type: custom:clockclock24-card
```

| Option | Default | |
| --- | --- | --- |
| `mode` | `cycle` | A mode name, `cycle` to rotate, or `pattern` |
| `cycle` | six choreographies | The rotation, in order. Repeats count |
| `cycle_interval` | `60` | Seconds between windows opening |
| `window` | `35` | Seconds of choreography per window |
| `return_to_time` | `true` | Show the clock between windows |
| `transition` | `5000` | ms for a digit sweep, and for the fade into a mode |
| `mode_speed` | `1.0` | Animation speed multiplier |
| `hand_color` | `#ffffff` | |
| `background` | `#000000` | |
| `face_color` | `#1f1f23` | Only drawn when `show_face` |
| `show_face` | `false` | A disc behind each pair of hands |
| `digit_gap` | `0.35` | Space between digits, in clock widths. `0` for one even grid |
| `fullscreen` | `false` | Fill the viewport height — for a wall tablet |
| `pattern` | — | A pattern string from the editor |
| `time_entity` | — | Optional; the browser's own clock is used otherwise |

A wall tablet:

```yaml
type: custom:clockclock24-card
fullscreen: true
digit_gap: 0
cycle: [wave, wind, rotating_maze, zipper, mirror_wave]
cycle_interval: 120
```

Your own pattern — **the same string the hardware takes**, whether it ends up
on a dashboard or on 24 round screens:

```yaml
type: custom:clockclock24-card
mode: pattern
pattern: "fan:AHgIPB4AeAg2HgB4CDweAHgINh4..."
```

### The editor card

The add-on's editor, on a dashboard instead of in the sidebar. Same file — the
add-on's page instantiates this card rather than reimplementing it.

```yaml
type: custom:clockclock24-editor-card
entity: text.cc24_board_d_pattern_1
```

| Option | Default | |
| --- | --- | --- |
| `entity` | — | A `text.` entity holding a pattern. Enables Load and Send |
| `name` | `pattern` | The name written into the string, and shown in the master's logs |
| `snap` | `15` | Degrees a dragged hand snaps to |

Both cards share the engine *and* the pattern model, which is why the editor's
preview is not an approximation: it is the wall's own code with a canvas
instead of panels. It also means a clock card set to `mode: pattern` on the
same dashboard follows the editor live — handy, but worth knowing before you
wonder why it changed.

## Not yet

- **Naming and reordering the master's slots from here.** You get the eight
  entities as Home Assistant names them.
- **A visual config editor for the clock card.** Its options are YAML for now.
