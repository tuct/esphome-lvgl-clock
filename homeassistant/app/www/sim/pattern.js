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
    // The whole configuration - home pose and motion, per clock - as JSON that
    // fromJSON() can read back.
    toJSON() {
      const r = v => Math.round(v * 100) / 100;
      return JSON.stringify({
        format: "clockclock24-pattern",
        version: 1,
        maxRateDegPerS: PATTERN_MAX_RATE,
        clocks: spec.map(s => ({
          h0: Math.round(s.h0), h1: Math.round(s.h1),
          dirA: s.dirA, dirB: s.dirB,
          spdA: s.spdA.mode === "fixed"
            ? { mode: "fixed", v: r(s.spdA.v) }
            : { mode: "rel", from: s.spdA.from, d: r(s.spdA.d) },
          spdB: s.spdB.mode === "fixed"
            ? { mode: "fixed", v: r(s.spdB.v) }
            : { mode: "rel", from: s.spdB.from, d: r(s.spdB.d) },
        })),
      }, null, 1);
    },

    // The form an ESPHome text entity takes: "<name>:<base64>".
    //
    // A pattern as JSON is ~5 kB and a Home Assistant text entity holds 255
    // characters, so this is packed - five bytes per clock, mirroring
    // PatternStore::to_text() in the firmware:
    //
    //   0  h0, in 1.5 deg steps (0..239)     3  v0, 0..100
    //   1  h1                                 4  v1
    //   2  (dir0+1) << 2 | (dir1+1)
    //
    // 24 x 5 = 120 bytes = 160 base64 chars. The 1.5 deg quantisation is ten
    // times finer than the editor's 15 deg snap, so nothing authored is lost.
    //
    // Relative speeds are RESOLVED first: the firmware only ever sees numbers.
    toESPHome(name) {
      const B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
      const sp = this.resolved();
      const buf = new Uint8Array(NUM_CLOCKS * 5);
      for (let i = 0; i < NUM_CLOCKS; i++) {
        const s = spec[i], b = i * 5;
        buf[b + 0] = Math.round(wrap360(s.h0) / 1.5) % 240;
        buf[b + 1] = Math.round(wrap360(s.h1) / 1.5) % 240;
        buf[b + 2] = ((s.dirA + 1) << 2) | (s.dirB + 1);
        buf[b + 3] = Math.round(sp[i][0] * 100);
        buf[b + 4] = Math.round(sp[i][1] * 100);
      }
      let out = "";
      for (let i = 0; i < buf.length; i += 3) {
        const v = (buf[i] << 16) | ((buf[i + 1] || 0) << 8) | (buf[i + 2] || 0);
        out += B64[(v >> 18) & 63] + B64[(v >> 12) & 63] +
               (i + 1 < buf.length ? B64[(v >> 6) & 63] : "=") +
               (i + 2 < buf.length ? B64[v & 63] : "=");
      }
      return (name || "pattern").slice(0, 15) + ":" + out;
    },

    // The inverse of toESPHome(): unpack "<name>:<base64>" into 24 clock specs.
    //
    // Returns { name, clocks } or null. Used by the Home Assistant card, which
    // takes the SAME string that goes into a Pattern text entity - one format
    // for the hardware and the browser, so a pattern you like can be dropped
    // into either.
    parseESPHome(text) {
      const B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
      if (typeof text !== "string" || !text) return null;
      const colon = text.indexOf(":");
      const name = colon >= 0 ? text.slice(0, colon) : "pattern";
      const data = colon >= 0 ? text.slice(colon + 1) : text;

      const bytes = [];
      let acc = 0, bits = 0;
      for (const ch of data) {
        if (ch === "=" || ch === "\n" || ch === "\r" || ch === " ") continue;
        const v = B64.indexOf(ch);
        if (v < 0) return null;
        acc = (acc << 6) | v; bits += 6;
        if (bits >= 8) { bits -= 8; bytes.push((acc >> bits) & 0xff); }
      }
      if (bytes.length !== NUM_CLOCKS * 5) return null;

      const clocks = [];
      for (let i = 0; i < NUM_CLOCKS; i++) {
        const b = i * 5;
        clocks.push({
          h0: (bytes[b] * 3) / 2 % 360,
          h1: (bytes[b + 1] * 3) / 2 % 360,
          dir0: ((bytes[b + 2] >> 2) & 3) - 1,
          dir1: (bytes[b + 2] & 3) - 1,
          v0: Math.min(100, bytes[b + 3]) / 100,
          v1: Math.min(100, bytes[b + 4]) / 100,
        });
      }
      return { name, clocks };
    },

    // Read a configuration back. Returns null on success, or a message.
    //
    // Everything is validated and coerced rather than trusted: this is text a
    // human has been editing, and a bad field should cost you one wrong clock,
    // not a wall of NaN that silently draws nothing.
    fromJSON(text, ts) {
      let o;
      try { o = JSON.parse(text); } catch (e) { return "not valid JSON — " + e.message; }
      const list = Array.isArray(o) ? o : (o && o.clocks);
      if (!Array.isArray(list)) return "no `clocks` array in there";
      if (list.length !== NUM_CLOCKS) return `expected ${NUM_CLOCKS} clocks, found ${list.length}`;

      const DIRS = ["left", "right", "up", "down"];
      const num = (v, dflt) => (Number.isFinite(+v) ? +v : dflt);
      const readSpeed = raw => {
        if (!raw || typeof raw !== "object") return fixed(0.3);
        if (raw.mode === "rel")
          return { mode: "rel",
                   from: DIRS.indexOf(raw.from) >= 0 ? raw.from : "left",
                   d: Math.max(-1, Math.min(1, num(raw.d, 0))) };
        return fixed(clamp01(num(raw.v, 0.3)));
      };

      const next = blank();
      for (let i = 0; i < NUM_CLOCKS; i++) {
        const c = list[i] || {};
        next[i].h0 = wrap360(num(c.h0, 0));
        next[i].h1 = wrap360(num(c.h1, 180));
        next[i].dirA = Math.sign(num(c.dirA, 0));
        next[i].dirB = Math.sign(num(c.dirB, 0));
        next[i].spdA = readSpeed(c.spdA);
        next[i].spdB = readSpeed(c.spdB);
      }
      spec = next;
      this.toHome(ts || 0);      // start from the loaded poses, not from t = 0
      return null;
    },

  };
  window.CC.tickPattern = tickPattern;
  // No sandbox-only marker on this one: an editor is not a thing that could
  // ever be ported to the firmware, so the note would be telling you nothing.
  window.CC.MODES.pattern = { fn: tickPattern, label: "Motion Pattern Editor Mode" };
})();
