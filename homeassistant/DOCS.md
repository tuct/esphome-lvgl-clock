# Digital Clock Clock 24

The pattern editor, in the sidebar, wired to the wall.

## Options

| Option | Default | What it is |
|---|---|---|
| `install_card` | `true` | Copy the Lovelace bundle into `/config/www` on start, so the cards are at `/local/`. Off, and `/config` is never written to. |
| `entity_filter` | `pattern` | Substring an entity must match to appear in the slot list. A pattern slot is a `text.` entity, and a Home Assistant with other integrations has plenty of those. Empty lists them all. |

## Sending a pattern

1. Open the add-on. Pick a **Pattern slot** — the master's eight `text.`
   entities, empty ones included, since an empty slot is exactly where a new
   pattern goes.
2. **Click** a clock to select it, **shift-click** for several; every edit then
   applies to all of them. **Drag** its hands to pose it, snapped to 15°.
3. Set a **direction and speed per hand**. The speed slider is squared, so the
   slow end — where a pattern actually reads — gets half the travel. The
   readout is in °/s.
4. It **runs while you edit**. **Back to pose** puts every hand back on what
   you configured, not on wherever the motion drifted to.
5. **Copy to all 24**, or to the primary clock's row or column.
6. **Send to wall.**

A packed 24-clock pattern is about 164 characters, against the master's 255, so
the whole wall fits in one text field with room to spare.

> **Send overwrites that slot**, which the master saves to flash and pushes to
> all seven listeners. The master's **Reload patterns from firmware** button is
> the way back to what is in the `patterns/` folder.

### What it does not do

It does not generate ESPHome YAML, it does not compile and it does not flash.
Those belong to the ESPHome add-on, which is already installed next to this and
already does them well. A pattern needs none of them: it is data, and the
master takes it over the network at runtime.

The board configs for a real wall are in
[`digital_clock_clock_24_24_round_screens/`](../digital_clock_clock_24_24_round_screens/README.md).
Copy them into your ESPHome folder once, flash the listeners over USB and the
master over the network, and you should not need USB again.

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
| `cycle` | six choreographies | The rotation, in order. **Repeats count** — listing one twice gives it twice the slots |
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

Without an `entity:`, everything works except Load and Send — useful for
drawing something to paste elsewhere.

Both cards share the engine *and* the pattern model, which is why the editor's
preview is not an approximation: it is the wall's own code with a canvas
instead of panels. It also means a clock card set to `mode: pattern` on the
same dashboard follows the editor live — handy, but worth knowing before you
wonder why it changed.

## Not yet

- **A visual config editor for the clock card.** Its options are YAML for now.
- **Naming and reordering the master's slots from here.** You get the eight
  entities as Home Assistant names them.
