// The pattern editor's model: a per-clock motion spec, and the mode that draws
// it. Loaded as a classic script after engine.js.
//
// A pattern is 24 entries, one per clock:
//
//   { h0, h1,      the HOME pose - what you configured, in degrees
//     a0, a1,      the runtime ANCHOR the motion is measured from
//     dirA, dirB,  -1 left (counter-clockwise), 0 still, +1 right
//     spdA, spdB } speed, 0..1 of PATTERN_MAX_RATE
//
// HOME and ANCHOR are separate on purpose. The anchor moves constantly: every
// time a speed or direction is edited it has to be re-cut so the hands do not
// jump (see rebaseAll in the page). If that were the only stored pose, the
// thing you configured would be quietly overwritten the first time you touched
// a slider, and there would be nothing left to go back to.
//
// So: dragging a hand sets BOTH - you are configuring a new pose. Everything
// else moves the anchor only, and `toHome()` puts the hands back on the pose
// you actually chose.
//
// A speed is either FIXED - {mode:"fixed", v} - or RELATIVE to a neighbour's
// speed for the same hand: {mode:"rel", from:"left"|"right"|"up"|"down", d}.
// Relative speeds are what make a pattern a pattern rather than 24 unrelated
// clocks: set one clock going and let the wall derive itself from it.

(function () {
  const C = window.CC;
  const { NUM_CLOCKS, WALL_COLS, WALL_ROWS, wallPos, wrap360 } = C;

  // Speed 1.0 in degrees per second. One full turn in 4 s at full tilt - fast
  // enough to be unmistakable, slow enough to stay an analogue clock.
  const PATTERN_MAX_RATE = 90;

  const clamp01 = v => Math.max(0, Math.min(1, v));
  const fixed = v => ({ mode: "fixed", v });

  function blank() {
    const spec = [];
    for (let i = 0; i < NUM_CLOCKS; i++)
      spec.push({ h0: 0, h1: 180, a0: 0, a1: 180,
                  dirA: 0, dirB: 0, spdA: fixed(0.3), spdB: fixed(0.3) });
    return spec;
  }
  let spec = blank();

  // The clock one step away in a given direction, or null at the wall's edge.
  function neighbour(i, dir) {
    const { col, row } = wallPos(i);
    let c = col, r = row;
    if (dir === "left") c--; else if (dir === "right") c++;
    else if (dir === "up") r--; else if (dir === "down") r++;
    if (c < 0 || c >= WALL_COLS || r < 0 || r >= WALL_ROWS) return null;
    return (c >> 1) * C.CLOCKS_PER_DIGIT + r * 2 + (c & 1);
  }

  // Resolve a speed, following "same as my neighbour" chains.
  //
  // The chain can loop - A takes B's speed and B takes A's - so `seen` breaks
  // the cycle at 0 rather than recursing forever. A cycle is a user mistake,
  // not a crash, and the clock simply stops.
  function speedOf(i, hand, seen) {
    const sp = spec[i][hand === 0 ? "spdA" : "spdB"];
    if (sp.mode === "fixed") return clamp01(sp.v);
    const key = i * 2 + hand;
    if (seen.has(key)) return 0;
    seen.add(key);
    const j = neighbour(i, sp.from);
    if (j === null) return 0;                    // off the edge: nothing to copy
    return clamp01(speedOf(j, hand, seen) + sp.d);
  }

  function resolved() {
    const out = [];
    for (let i = 0; i < NUM_CLOCKS; i++)
      out.push([speedOf(i, 0, new Set()), speedOf(i, 1, new Set())]);
    return out;
  }

  // Every hand is base + direction * speed * rate * t: continuous by
  // construction, so a pattern can never jump however it is edited.
  function tickPattern(cur, t, { modeSpeed }) {
    const ts = t * modeSpeed;
    const sp = resolved();
    for (let i = 0; i < NUM_CLOCKS; i++) {
      const s = spec[i];
      cur[i * 2 + 0] = wrap360(s.a0 + s.dirA * sp[i][0] * PATTERN_MAX_RATE * ts);
      cur[i * 2 + 1] = wrap360(s.a1 + s.dirB * sp[i][1] * PATTERN_MAX_RATE * ts);
    }
  }

  const clone = s => JSON.parse(JSON.stringify(s));

  window.CC.pattern = {
    MAX_RATE: PATTERN_MAX_RATE,
    get: i => spec[i],
    all: () => spec,
    resolved,
    neighbour,
    reset: () => { spec = blank(); },
    // How far a hand has been carried from its pose by time `ts`.
    offsetAt(i, hand, ts) {
      const s = spec[i];
      const dir = hand === 0 ? s.dirA : s.dirB;
      return dir * speedOf(i, hand, new Set()) * PATTERN_MAX_RATE * ts;
    },
    // Cut the ANCHOR so the hand is AT `angle` right now, rather than at t = 0.
    //
    // With the motion running, storing an angle straight into the anchor would
    // make the hand snap to wherever the motion has carried it - you would be
    // aiming at a moving target. Subtracting the offset means the hand lands
    // where you put it and carries on from there.
    //
    // This does NOT touch the home pose: it is the continuity fix, not an edit.
    anchorAt(i, hand, angle, ts) {
      const a = wrap360(angle - this.offsetAt(i, hand, ts));
      if (hand === 0) spec[i].a0 = a; else spec[i].a1 = a;
    },
    // Configure a hand: this IS an edit, so it sets the home pose as well.
    setHomeAt(i, hand, angle, ts) {
      const a = wrap360(angle);
      if (hand === 0) spec[i].h0 = a; else spec[i].h1 = a;
      this.anchorAt(i, hand, a, ts);
    },
    // Put every hand back on the pose it was configured with, still running.
    toHome(ts) {
      for (let i = 0; i < NUM_CLOCKS; i++) {
        this.anchorAt(i, 0, spec[i].h0, ts || 0);
        this.anchorAt(i, 1, spec[i].h1, ts || 0);
      }
    },
    // Take the wall's current angles as the pattern's home pose, so anything
    // posed by hand or left behind by another mode can be a starting point.
    seedFrom(cur, ts) {
      for (let i = 0; i < NUM_CLOCKS; i++) {
        this.setHomeAt(i, 0, cur[i * 2 + 0], ts || 0);
        this.setHomeAt(i, 1, cur[i * 2 + 1], ts || 0);
      }
    },
    copy: i => clone(spec[i]),
    paste(i, src) { spec[i] = clone(src); },
    pasteAll(src) { for (let i = 0; i < NUM_CLOCKS; i++) spec[i] = clone(src); },
    pasteRow(row, src) {
      for (let i = 0; i < NUM_CLOCKS; i++) if (wallPos(i).row === row) spec[i] = clone(src);
    },
    pasteCol(col, src) {
      for (let i = 0; i < NUM_CLOCKS; i++) if (wallPos(i).col === col) spec[i] = clone(src);
    },
    // Everything needed to reproduce this pattern in code, one clock per line.
    export() {
      const sp = o => o.mode === "fixed"
        ? `{mode:"fixed", v:${(+o.v).toFixed(2)}}`
        : `{mode:"rel", from:"${o.from}", d:${(+o.d).toFixed(2)}}`;
      const lines = spec.map((s, i) => {
        const { col, row } = wallPos(i);
        return `  /* ${String(i).padStart(2)} col${col} row${row} */ ` +
               `{a0:${Math.round(s.h0)}, a1:${Math.round(s.h1)}, ` +
               `dirA:${s.dirA}, dirB:${s.dirB}, ` +
               `spdA:${sp(s.spdA)}, spdB:${sp(s.spdB)}},`;
      });
      return "// speed 1.0 = " + PATTERN_MAX_RATE + " deg/s\nconst PATTERN = [\n" +
             lines.join("\n") + "\n];";
    },
  };
  window.CC.tickPattern = tickPattern;
  window.CC.MODES.pattern = { fn: tickPattern, label: "pattern *" };
})();
