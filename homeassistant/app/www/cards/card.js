// ClockClock 24 — a Lovelace card.
//
// Renders the wall in Home Assistant using the SAME engine as the firmware and
// the sandbox. engine.js, wall.js and pattern.js are prepended to this file by
// build.sh rather than copied into it, so there is one implementation of a
// choreography, not three.
//
// It also eats the sandbox's pattern strings: the exact text you paste into a
// `Pattern` entity to drive the real wall will drive this card too.

(function () {
  const C = window.CC;
  if (!C) { console.error("[clockclock24-card] engine missing — rebuild with build.sh"); return; }

  const CARD_VERSION = "0.1.0";
  console.info(`%c CLOCKCLOCK24-CARD %c ${CARD_VERSION} `,
               "color:#000;background:#6ea8fe;font-weight:700",
               "color:#6ea8fe;background:#222");

  const DEFAULTS = {
    mode: "cycle",              // a mode name, "cycle", or "pattern"
    cycle: ["wave", "wind", "rotating_maze", "zipper", "mirror_wave", "spiral"],
    cycle_interval: 60,         // seconds between windows
    window: 35,                 // seconds of choreography per window
    return_to_time: true,       // …then back to the clock until the next one
    transition: 5000,           // ms for a sweep, and for the fade into a mode
    mode_speed: 1.0,
    // How the two hands travel to a new digit, same names as the firmware.
    // wall.js has always honoured this; it was simply never in the config.
    movement: "opposite",       // opposite | clockwise | counter | long
    hand_color: "#ffffff",
    background: "#000000",
    face_color: "#1f1f23",
    show_face: false,
    // 0 = the real wall: 24 panels on one frame, evenly spaced. Raise it for a
    // dashboard card if you want the HH:MM grouping to read at a glance.
    digit_gap: 0,               // extra space between digits, in clock widths
    fullscreen: false,          // fill the viewport height instead of flowing
    pattern: null,              // "<name>:<base64>" from the sandbox
    // Named patterns, { name: "<name>:<base64>" }. A name here is usable
    // anywhere a mode name is - `mode: fan`, or `fan` inside `cycle:` - which
    // is what makes a pattern a first-class mode on a screen, exactly as it is
    // on the wall. `pattern:` above is the single-pattern shorthand and still
    // works; it is simply the one named `pattern`.
    patterns: null,
    time_entity: null,          // optional; default is the browser's clock
  };

  class ClockClock24Card extends HTMLElement {
    setConfig(config) {
      this._cfg = Object.assign({}, DEFAULTS, config || {});
      if (typeof this._cfg.cycle === "string") this._cfg.cycle = [this._cfg.cycle];

      // Patterns are decoded once, here, rather than every frame. Both spellings
      // land in the same table, so the rest of the card only knows about names.
      this._patterns = {};
      const add = (name, text) => {
        const p = C.pattern.parseESPHome(text);
        if (!p)
          throw new Error(`clockclock24-card: pattern \`${name}\` is not a pattern string`);
        this._patterns[name] = p;
      };
      if (this._cfg.pattern) add("pattern", this._cfg.pattern);
      if (this._cfg.patterns)
        for (const [name, text] of Object.entries(this._cfg.patterns)) add(name, text);
      this._patternLoaded = null;   // which name is currently in C.pattern

      // Catch a name that will never draw at CONFIG time rather than leaving a
      // still wall at 3am with nothing in the log to say why.
      const named = [this._cfg.mode, ...(this._cfg.cycle || [])];
      for (const n of named) {
        if (n === "cycle" || n === "time" || C.MODES[n]) continue;
        if (!this._patterns[n])
          throw new Error(
            `clockclock24-card: \`${n}\` is neither a mode nor one of the patterns ` +
            `given (${Object.keys(this._patterns).join(", ") || "none"})`);
      }

      this._build();
    }

    // Home Assistant hands the card its state object on every update. Nothing
    // here needs it unless a time_entity was named - the wall runs off the
    // browser clock, which is already correct and costs no round trips.
    set hass(hass) { this._hass = hass; }

    getCardSize() { return this._cfg && this._cfg.fullscreen ? 10 : 4; }

    static getStubConfig() { return { type: "custom:clockclock24-card", mode: "cycle" }; }

    connectedCallback() { this._start(); }
    disconnectedCallback() { this._stop(); }

    // ---- setup -------------------------------------------------------------
    _build() {
      if (!this.shadowRoot) this.attachShadow({ mode: "open" });
      const cfg = this._cfg;
      this.shadowRoot.innerHTML = `
        <style>
          :host { display: block; }
          /* display:block explicitly. Inside Lovelace ha-card is a defined
             element and already block, but in the add-on's tablet view it is
             UNDEFINED - and an unknown element defaults to display:inline, so
             the canvas's width:100% resolves against a shrink-to-fit inline
             box and the wall collapses to nothing. */
          ha-card { display: block; overflow: hidden; }
          .wrap { background: ${cfg.background}; }
          canvas {
            display: block; width: 100%; height: auto;
            aspect-ratio: ${8 + cfg.digit_gap * 3} / 3;
          }
          :host([data-fullscreen]) canvas {
            height: 100vh; width: 100%; object-fit: contain; aspect-ratio: auto;
          }
        </style>
        <ha-card><div class="wrap"><canvas></canvas></div></ha-card>`;
      if (cfg.fullscreen) this.setAttribute("data-fullscreen", "");
      this._canvas = this.shadowRoot.querySelector("canvas");
      this._ctx = this._canvas.getContext("2d");

      this._wall = new C.Wall();
      this._wall.transitionMs = cfg.transition;
      this._wall.modeSpeed = cfg.mode_speed;
      this._wall.movement = cfg.movement;
      this._pushTime(performance.now());

      // Fixed mode, or the rotation.
      this._cycleAt = -1;
      this._nextWindow = 0;
      this._inWindow = false;
      if (cfg.mode !== "cycle") this._wall.setMode(this._modeFor(cfg.mode), performance.now());
    }

    // Change what is showing on the LIVE card.
    //
    // setConfig() rebuilds everything, which throws away where the hands are -
    // and this card exists to be an analogue clock, so it cannot jump. Anything
    // following a real wall must come through here instead.
    setMode(name, pattern) {
      // Following a wall hands us the slot's text, which has no name of its own
      // here - it lands under `pattern`, the same key the shorthand config uses.
      if (pattern !== undefined && pattern !== this._cfg.pattern) {
        this._cfg.pattern = pattern;
        const p = pattern ? C.pattern.parseESPHome(pattern) : null;
        if (p) this._patterns.pattern = p;
        else delete this._patterns.pattern;
        this._patternLoaded = null;
      }
      if (name === this._cfg.mode && name !== "pattern") return;
      this._cfg.mode = name;
      this._cycleAt = -1;
      this._inWindow = false;
      if (name === "cycle") { this._nextWindow = 0; return; }
      this._wall.setMode(this._modeFor(name), performance.now());
    }

    // Colours, live. Same reason as setMode: rebuilding to recolour would
    // restart the animation, and the hands would jump.
    setColors(hand, background) {
      if (hand) this._cfg.hand_color = hand;
      if (background) {
        this._cfg.background = background;
        const wrap = this.shadowRoot && this.shadowRoot.querySelector(".wrap");
        if (wrap) wrap.style.background = background;
      }
    }

    // A name is either a choreography or one of our patterns. Patterns all draw
    // through the one `pattern` mode, so this also swaps the spec in when the
    // name changes - a cycle can list several and they take turns.
    _modeFor(name) {
      if (C.MODES[name] && name !== "pattern") return name;
      if (this._patterns[name]) {
        this._loadPattern(name);
        return "pattern";
      }
      return name;
    }

    _loadPattern(name) {
      const p = this._patterns[name];
      if (!p || this._patternLoaded === name) return;
      p.clocks.forEach((c, i) => {
        const s = C.pattern.get(i);
        s.h0 = c.h0; s.h1 = c.h1;
        s.dirA = c.dir0; s.dirB = c.dir1;
        s.spdA = { mode: "fixed", v: c.v0 };
        s.spdB = { mode: "fixed", v: c.v1 };
      });
      C.pattern.toHome(0);
      this._patternLoaded = name;
    }

    _pushTime(now) {
      let d = new Date();
      const ent = this._cfg.time_entity && this._hass && this._hass.states[this._cfg.time_entity];
      if (ent && !isNaN(Date.parse(ent.state))) d = new Date(ent.state);
      const hh = d.getHours(), mm = d.getMinutes();
      const digits = [Math.floor(hh / 10), hh % 10, Math.floor(mm / 10), mm % 10];
      if (this._digits && digits.every((v, i) => v === this._digits[i])) return;
      this._digits = digits;
      this._wall.setDigits(digits, now);
    }

    // ---- the loop ----------------------------------------------------------
    _start() {
      if (this._raf) return;
      this._last = performance.now();
      const tick = (t) => {
        this._raf = requestAnimationFrame(tick);
        // Clamped: a card on a background tab must not leap when it wakes.
        const dt = Math.min((t - this._last) / 1000, 0.1);
        this._last = t;
        this._advance(t);
        this._wall.frame(t, dt);
        this._render();
      };
      this._raf = requestAnimationFrame(tick);
    }
    _stop() { if (this._raf) cancelAnimationFrame(this._raf); this._raf = null; }

    // The same shape as the firmware's cycle_modes: a window opens every
    // `cycle_interval`, runs for `window` seconds, then the clock comes back.
    _advance(t) {
      const cfg = this._cfg;
      if (cfg.mode !== "cycle") { this._pushTime(t); return; }
      if (!cfg.cycle.length) return;

      if (!this._inWindow && t >= this._nextWindow) {
        this._cycleAt = (this._cycleAt + 1) % cfg.cycle.length;
        const m = this._modeFor(cfg.cycle[this._cycleAt]);
        if (C.MODES[m]) this._wall.setMode(m, t);
        this._inWindow = true;
        this._windowEnds = t + cfg.window * 1000;
        this._nextWindow = t + cfg.cycle_interval * 1000;
      } else if (this._inWindow && t >= this._windowEnds) {
        this._inWindow = false;
        if (cfg.return_to_time) {
          // Digits FIRST, then leave the mode: settleToTime() reads
          // wall.digits to work out where every hand is going, and setDigits()
          // is a no-op while a choreography is running (it only settles when
          // the mode is already `time`). Pushing digits without this left the
          // wall animating for ever; leaving the mode without it settled to
          // whatever minute was last shown.
          this._digits = null;
          this._pushTime(t);
          this._wall.setMode("time", t);
        }
      }
      if (!this._inWindow) this._pushTime(t);
    }

    // ---- drawing -----------------------------------------------------------
    _render() {
      const cfg = this._cfg, cv = this._canvas, ctx = this._ctx;
      // Match the backing store to the CSS box so the hands stay crisp on a
      // retina display and do not blur when the card is resized.
      const rect = cv.getBoundingClientRect();
      const dpr = window.devicePixelRatio || 1;
      const w = Math.max(1, Math.round(rect.width * dpr));
      const h = Math.max(1, Math.round(rect.height * dpr));
      if (cv.width !== w || cv.height !== h) { cv.width = w; cv.height = h; }

      ctx.fillStyle = cfg.background;
      ctx.fillRect(0, 0, w, h);

      const gapCols = cfg.digit_gap * 3;
      const cell = Math.min(w / (C.WALL_COLS + gapCols), h / C.WALL_ROWS);
      const gap = cell * cfg.digit_gap;
      const r = cell * 0.46;
      const totalW = cell * C.WALL_COLS + gap * 3;
      const x0 = (w - totalW) / 2 + cell / 2;
      const y0 = (h - cell * C.WALL_ROWS) / 2 + cell / 2;
      const lw = Math.max(1, r / 8);

      for (let c = 0; c < C.NUM_CLOCKS; c++) {
        const p = C.wallPos(c);
        const cx = x0 + p.col * cell + Math.floor(p.col / 2) * gap;
        const cy = y0 + p.row * cell;
        if (cfg.show_face) {
          ctx.fillStyle = cfg.face_color;
          ctx.beginPath(); ctx.arc(cx, cy, r, 0, Math.PI * 2); ctx.fill();
        }
        ctx.strokeStyle = cfg.hand_color;
        ctx.fillStyle = cfg.hand_color;
        ctx.lineWidth = lw;
        ctx.lineCap = "butt";
        for (const a of [this._wall.cur[c * 2], this._wall.cur[c * 2 + 1]]) {
          const rad = (a - 90) * Math.PI / 180;
          ctx.beginPath();
          ctx.moveTo(cx, cy);
          ctx.lineTo(cx + Math.cos(rad) * r, cy + Math.sin(rad) * r);
          ctx.stroke();
        }
        // Rounded pivot, square tips - the ClockClock look.
        ctx.beginPath(); ctx.arc(cx, cy, lw / 2, 0, Math.PI * 2); ctx.fill();
      }
    }
  }

  customElements.define("clockclock24-card", ClockClock24Card);
  window.customCards = window.customCards || [];
  window.customCards.push({
    type: "clockclock24-card",
    name: "ClockClock 24",
    description: "A digital clock made of 24 analogue ones, with the wall's own choreographies.",
    preview: true,
  });
})();
