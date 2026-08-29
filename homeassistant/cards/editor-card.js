// ClockClock 24 — pattern editor card.
//
// The sandbox's Motion Pattern Editor, as a Lovelace card, wired to the wall:
// draw a pattern, watch it run, and press Send to write it into the ESPHome
// `Pattern N` text entity. The master pushes it down the sync bus and 24 real
// clocks are running it a second later.
//
// It shares the engine and the pattern model with the clock card - both are in
// the same bundle - so the preview here is not an approximation of the wall, it
// is the wall's own code with a canvas instead of panels.

(function () {
  const C = window.CC;
  if (!C) { console.error("[clockclock24-editor-card] engine missing — rebuild with build.sh"); return; }
  const P = C.pattern;

  const MAX_RATE = P.MAX_RATE;
  // Speed sliders are SQUARED: a linear travel spends nine tenths of itself
  // above 9 deg/s, and the slow end is where a pattern reads. The slider is in
  // PERCENT with a 0.5 step, so the readout is a number you can dial back to
  // rather than a position you have to find again by eye.
  const ICON_PAUSE = '<svg viewBox="0 0 12 12" width="12" height="12" aria-hidden="true">' +
    '<rect x="2" y="1.5" width="3" height="9" fill="currentColor"/>' +
    '<rect x="7" y="1.5" width="3" height="9" fill="currentColor"/></svg>';
  const ICON_PLAY = '<svg viewBox="0 0 12 12" width="12" height="12" aria-hidden="true">' +
    '<path d="M3 1.5 L10.5 6 L3 10.5 Z" fill="currentColor"/></svg>';

  const spdFromSlider = pct => { const p = pct / 100; return p * p; };
  const sliderFromSpd = v => Math.sqrt(Math.max(0, v)) * 100;

  class ClockClock24EditorCard extends HTMLElement {
    setConfig(cfg) {
      this._cfg = Object.assign({ entity: null, snap: 15, name: "pattern" }, cfg || {});
      this._sel = new Set([0]);
      this._primary = 0;
      this._scope = "all";       // what Copy carries: all | position | motion
      this._clip = null;
      this._build();
    }
    set hass(h) { this._hass = h; if (this._entityRow) this._syncEntity(); }
    getCardSize() { return 8; }
    static getStubConfig() { return { type: "custom:clockclock24-editor-card" }; }

    connectedCallback() { this._start(); }
    disconnectedCallback() { this._stop(); }

    _build() {
      if (!this.shadowRoot) this.attachShadow({ mode: "open" });
      this.shadowRoot.innerHTML = `
        <style>
          :host { display:block; }
          canvas { display:block; width:100%; height:auto; aspect-ratio:8/3; background:#000; cursor:crosshair; }
          .pad { padding: 12px 16px 16px; }
          .row { display:flex; gap:6px; align-items:center; flex-wrap:wrap; margin-top:8px; }
          .row > .lbl { min-width:4.5em; opacity:.7; font-size:.85em; }
          button { font:inherit; padding:4px 9px; border-radius:6px; cursor:pointer;
                   border:1px solid var(--divider-color,#444);
                   background:var(--secondary-background-color,#222);
                   color:var(--primary-text-color,#eee); }
          button[aria-pressed="true"] { background:var(--primary-color,#03a9f4); color:#000; font-weight:600; }
          button:disabled { opacity:.4; cursor:not-allowed; }
          input[type=range] { flex:1 1 90px; min-width:80px; }
          select { font:inherit; padding:3px 6px; border-radius:6px;
                   border:1px solid var(--divider-color,#444);
                   background:var(--secondary-background-color,#222);
                   color:var(--primary-text-color,#eee); }
          .row.rel { margin-top:2px; opacity:.9; }
          .pm { opacity:.6; }
          .val { min-width:9.5em; text-align:right; font-variant-numeric:tabular-nums; font-size:.85em; }
          .hint { margin:8px 0 0; font-size:.8em; opacity:.7; }
          /* Holding something is a state you should be able to see without
             reading - the dot is the same one the wall panel marks patterns
             with. */
          .hint.held { opacity:1; color:var(--primary-color,#03a9f4); }
          .hint.held::before {
            content:""; display:inline-block; width:5px; height:5px;
            border-radius:50%; background:currentColor; margin-right:6px;
            vertical-align:middle;
          }
          .hint.inline { margin:0; }
          .sec { margin:16px 0 2px; font-size:.72em; letter-spacing:.09em;
                 text-transform:uppercase; opacity:.55; font-weight:600;
                 border-top:1px solid var(--divider-color,#333); padding-top:10px;
                 display:flex; gap:8px; align-items:baseline; flex-wrap:wrap; }
          .sec:first-of-type { border-top:0; margin-top:6px; padding-top:0; }
          .secnote { text-transform:none; letter-spacing:0; font-weight:400; opacity:.8; }
          button.icon { line-height:0; padding:6px 11px; }
          button.icon svg { display:block; }
          /* Hovering a clock says what it is set to - the canvas cannot carry
             text, and reading a pattern off 24 pairs of hands is guesswork. */
          .tip { position:absolute; pointer-events:none; z-index:5; display:none;
                 background:#0e1013ee; color:#e7e9ee; border:1px solid #2a2f37;
                 border-radius:6px; padding:6px 9px; font:11px/1.45 ui-monospace,monospace;
                 white-space:pre; transform:translate(-50%,-115%); }
          .cvwrap { position:relative; }
          textarea { width:100%; margin-top:8px; min-height:60px; font:11px/1.4 ui-monospace,monospace;
                     background:#0e1013; color:#8fd0a0; border:1px solid var(--divider-color,#444);
                     border-radius:6px; padding:6px; }
        </style>
        <ha-card>
          <div class="cvwrap"><canvas></canvas><div class="tip" id="tip"></div></div>
          <div class="pad">
            <p class="hint" id="who"></p>

            <div class="sec">Selection</div>
            <div class="row">
              <span class="lbl">select</span>
              <button id="selall">All 24</button>
              <button id="selrow">Row</button>
              <button id="selcol">Column</button>
              <button id="selnone">Just one</button>
              <span class="hint inline">or shift-click the wall</span>
            </div>

            <div class="row"><span class="lbl">hand A</span>
              <button class="dir" data-h="0" data-v="-1" title="counter-clockwise">&#8634;</button>
              <button class="dir" data-h="0" data-v="0" title="still">&#9679;</button>
              <button class="dir" data-h="0" data-v="1" title="clockwise">&#8635;</button>
              <select id="mode0" title="Fixed rate, or take a neighbour's and add to it">
                <option value="fixed">fixed</option>
                <option value="rel">same as…</option>
              </select>
              <input type="range" id="spd0" min="0" max="100" step="0.5">
              <span class="val" id="spd0v"></span>
            </div>
            <div class="row rel" id="rel0" style="display:none">
              <span class="lbl"></span>
              <select id="from0">
                <option value="left">the clock to its left</option>
                <option value="right">to its right</option>
                <option value="up">above it</option>
                <option value="down">below it</option>
              </select>
              <span class="pm">±</span>
              <input type="range" id="d0" min="-50" max="50" step="0.5">
              <span class="val" id="d0v"></span>
            </div>
            <div class="row"><span class="lbl">hand B</span>
              <button class="dir" data-h="1" data-v="-1" title="counter-clockwise">&#8634;</button>
              <button class="dir" data-h="1" data-v="0" title="still">&#9679;</button>
              <button class="dir" data-h="1" data-v="1" title="clockwise">&#8635;</button>
              <select id="mode1" title="Fixed rate, or take a neighbour's and add to it">
                <option value="fixed">fixed</option>
                <option value="rel">same as…</option>
              </select>
              <input type="range" id="spd1" min="0" max="100" step="0.5">
              <span class="val" id="spd1v"></span>
            </div>
            <div class="row rel" id="rel1" style="display:none">
              <span class="lbl"></span>
              <select id="from1">
                <option value="left">the clock to its left</option>
                <option value="right">to its right</option>
                <option value="up">above it</option>
                <option value="down">below it</option>
              </select>
              <span class="pm">±</span>
              <input type="range" id="d1" min="-50" max="50" step="0.5">
              <span class="val" id="d1v"></span>
            </div>

            <div class="row">
              <span class="lbl">pose</span>
              <button id="home" title="Put all 24 clocks back on the pose you configured">Back to pose</button>
              <button id="seed" title="Freeze where the selected hands are NOW as their pose">Wall to pose</button>
            </div>

          <div class="sec">Copy<span class="secnote">from the primary clock to every selected one</span></div>
            <div class="row">
              <span class="lbl">what</span>
              <button class="scope" data-s="all" aria-pressed="true">Everything</button>
              <button class="scope" data-s="position">Position only</button>
              <button class="scope" data-s="motion">Motion only</button>
            </div>
            <div class="row">
              <span class="lbl">to</span>
              <button id="copyc" title="Remember the primary clock">Copy</button>
              <button id="pastec" title="Paste into every selected clock">Paste into selection</button>
              <button id="all">All 24</button>
              <button id="row">Its row</button>
              <button id="col">Its column</button>
            </div>
            <p class="hint" id="clipnote"></p>

          <div class="sec">The pattern<span class="secnote">whole wall &mdash; not just the selection</span></div>
            <div class="row">
              <button id="play" class="icon" aria-pressed="true" title="Pause the motion"></button>
              <span class="hint inline" id="playnote">running</span>
            </div>

            <div class="row" id="entityRow">
              <button id="load" title="Read the pattern out of the entity">Load from wall</button>
              <button id="send" title="Write it back - the whole wall has it a second later">Send to wall</button>
              <button id="copy" title="The pattern as one line of text">Copy string</button>
            </div>
            <p class="hint" id="note"></p>
            <textarea id="out" spellcheck="false" style="display:none"></textarea>
          </div>
        </ha-card>`;

      this._cv = this.shadowRoot.querySelector("canvas");
      this._ctx = this._cv.getContext("2d");
      this._entityRow = this.shadowRoot.getElementById("entityRow");
      this._running = true;
      this._t = 0;

      const $ = id => this.shadowRoot.getElementById(id);
      this.$ = $;

      // ---- motion controls. Every edit goes through _rebase, or the hands
      // jump: an angle is pose + dir*speed*rate*t measured from t=0, so
      // changing a speed rewrites the whole history.
      this.shadowRoot.querySelectorAll("button.dir").forEach(b => {
        b.onclick = () => this._rebase(() => this._each(i => {
          P.get(i)[b.dataset.h === "0" ? "dirA" : "dirB"] = +b.dataset.v;
        }));
      });
      // Speed is either a rate or a RELATIONSHIP: "whatever my neighbour is
      // doing, plus a bit". That is what makes a gradient across the wall one
      // number instead of eight, and it is the thing the sandbox had and this
      // card did not - so a pattern drawn there could not be edited here
      // without silently flattening it to fixed rates.
      const key = h => (h === 0 ? "spdA" : "spdB");
      const writeSpeed = (h) => {
        const rel = this.$("mode" + h).value === "rel";
        const spec = rel
          ? { mode: "rel", from: this.$("from" + h).value,
              d: (+this.$("d" + h).value) / 100 }
          : { mode: "fixed", v: spdFromSlider(+this.$("spd" + h).value) };
        this._rebase(() => this._each(i => { P.get(i)[key(h)] = JSON.parse(JSON.stringify(spec)); }));
      };
      [0, 1].forEach(h => {
        $("spd" + h).oninput = () => writeSpeed(h);
        $("d" + h).oninput = () => writeSpeed(h);
        $("mode" + h).onchange = () => writeSpeed(h);
        $("from" + h).onchange = () => writeSpeed(h);
      });

      $("play").innerHTML = ICON_PAUSE;
      $("play").onclick = () => {
        this._running = !this._running;
        // U+23F8 pause, U+25B6 play. An icon, because the button's job is
        // obvious and the word was the widest thing in the row.
        $("play").innerHTML = this._running ? ICON_PAUSE : ICON_PLAY;
        $("play").title = this._running ? "Pause the motion" : "Run the motion";
        $("play").setAttribute("aria-pressed", String(this._running));
        $("playnote").textContent = this._running ? "running" : "paused";
        this._rebase(() => {});   // resume from where the hands are
      };
      // These two are inverses, and they are NOT symmetric in what they risk.
      //
      // `Back to pose` throws nothing away - it re-cuts every anchor from the
      // pose you configured, so the hands land on it and carry on from there.
      // Nothing is lost, so there is no reason to make you select first: it
      // takes ALL 24, and is the resync you reach for mid-run when the wall has
      // wandered off somewhere you did not mean.
      //
      // `Wall to pose` OVERWRITES the pose with whatever is on screen. That one
      // stays on the selection, because doing it to all 24 on a mis-click is
      // how you lose an afternoon's work.
      $("home").onclick = () => {
        for (let i = 0; i < C.NUM_CLOCKS; i++) {
          P.anchorAt(i, 0, P.get(i).h0, this._t);
          P.anchorAt(i, 1, P.get(i).h1, this._t);
        }
        this._sync();
        this._note(`Back to pose — all ${C.NUM_CLOCKS} clocks.`);
      };
      $("seed").onclick = () => {
        this._each(i => { P.setHomeAt(i, 0, this._angle(i, 0), this._t);
                          P.setHomeAt(i, 1, this._angle(i, 1), this._t); });
        this._sync();
        this._note(`Pose taken from where the hands are — ${this._sel.size} clock${
          this._sel.size > 1 ? "s" : ""}.`);
      };

      // ---- selection helpers. Shift-click works, but nothing on the canvas
      // says so, and "select the whole row" is the commonest thing you want.
      $("selall").onclick = () => this._select(() => true, "all 24");
      $("selrow").onclick = () => { const r = C.wallPos(this._primary).row;
        this._select(i => C.wallPos(i).row === r, "row " + r); };
      $("selcol").onclick = () => { const c = C.wallPos(this._primary).col;
        this._select(i => C.wallPos(i).col === c, "column " + c); };
      $("selnone").onclick = () => this._select(i => i === this._primary, "one clock");

      // ---- copy scope
      this.shadowRoot.querySelectorAll("button.scope").forEach(b => {
        b.onclick = () => {
          this._scope = b.dataset.s;
          this.shadowRoot.querySelectorAll("button.scope").forEach(o =>
            o.setAttribute("aria-pressed", String(o === b)));
          this._clipNote();
          this._note(`Copy will carry ${b.textContent.toLowerCase()}.`);
        };
      });
      $("copyc").onclick = () => {
        this._clip = P.copy(this._primary);
        this._clipFrom = this._primary;
        this._clipNote();
        this._note(`Copied clock ${this._primary}. Select targets, then Paste.`);
      };
      $("pastec").onclick = () => {
        if (!this._clip) { this._note("Nothing copied yet."); return; }
        this._spread(i => this._sel.has(i), this._clip);
      };
      $("all").onclick = () => this._spread(() => true);
      $("row").onclick = () => { const r = C.wallPos(this._primary).row;
                                 this._spread(i => C.wallPos(i).row === r); };
      $("col").onclick = () => { const c = C.wallPos(this._primary).col;
                                 this._spread(i => C.wallPos(i).col === c); };
      $("copy").onclick = () => {
        const t = P.toESPHome(this._cfg.name);
        $("out").style.display = "block"; $("out").value = t; $("out").select();
        try { navigator.clipboard.writeText(t); } catch (_) {}
        this._note(`${t.length} characters — paste into a Pattern text entity.`);
      };
      $("load").onclick = () => this._load();
      $("send").onclick = () => this._send();

      // ---- posing
      this._cv.addEventListener("pointerdown", e => this._down(e));
      this._cv.addEventListener("pointermove", e => { if (this._drag) this._move(e); });
      this._cv.addEventListener("pointerup", () => { this._drag = null; });
      this._cv.addEventListener("pointercancel", () => { this._drag = null; });
      // Hover says what a clock is SET to. 24 pairs of hands all turning at
      // slightly different rates is not something you can read off the canvas,
      // and the controls only ever show the primary.
      this._cv.addEventListener("pointermove", e => this._hover(e));
      this._cv.addEventListener("pointerleave", () => { this.$("tip").style.display = "none"; });

      this._clipNote();
      this._sync();
    }

    _each(fn) { this._sel.forEach(fn); }
    _note(t) { this.$("note").textContent = t; }

    // Remember where every hand is, apply the change, put them back. Without
    // this a speed edit teleports the wall.
    _rebase(mutate) {
      const seen = [];
      for (let i = 0; i < C.NUM_CLOCKS; i++) {
        seen.push(P.offsetAt(i, 0, this._t), P.offsetAt(i, 1, this._t));
      }
      const spec = [];
      for (let i = 0; i < C.NUM_CLOCKS; i++) {
        spec.push(C.wrap360(P.get(i).a0 + seen[i * 2]), C.wrap360(P.get(i).a1 + seen[i * 2 + 1]));
      }
      mutate();
      for (let i = 0; i < C.NUM_CLOCKS; i++) {
        P.anchorAt(i, 0, spec[i * 2], this._t);
        P.anchorAt(i, 1, spec[i * 2 + 1], this._t);
      }
      this._sync();
    }

    // What the clipboard is holding, said out loud. The scope buttons change
    // what a Paste carries, so it is folded into the same line rather than
    // being a second thing to cross-reference - and "nothing yet" is worth
    // saying too, next to a Paste button that would otherwise look pressable
    // with nothing behind it.
    _clipNote() {
      const el = this.$("clipnote");
      const what = this._scope === "all" ? "pose and motion"
                 : this._scope === "position" ? "position" : "motion";
      const held = this._clip != null;
      el.textContent = held
        ? `Holding clock ${this._clipFrom} — Paste carries ${what}.`
        : "Nothing copied yet — Copy remembers the primary clock.";
      el.classList.toggle("held", held);
      this.$("pastec").disabled = !held;
    }

    // Copy from `src` (or the primary) into everything `pred` matches.
    //
    // The SCOPE is the point: "make this whole row turn like this one but keep
    // where each hand is" is a normal thing to want, and pasting everything is
    // the one operation that cannot express it.
    _spread(pred, src) {
      const from = src || P.copy(this._primary);
      let n = 0;
      this._rebase(() => {
        for (let i = 0; i < C.NUM_CLOCKS; i++) {
          if (!pred(i) || i === this._clipFrom && src) continue;
          const cur = P.get(i);
          if (this._scope === "position") {
            cur.h0 = from.h0; cur.h1 = from.h1;
          } else if (this._scope === "motion") {
            cur.dirA = from.dirA; cur.dirB = from.dirB;
            cur.spdA = JSON.parse(JSON.stringify(from.spdA));
            cur.spdB = JSON.parse(JSON.stringify(from.spdB));
          } else {
            P.paste(i, from);
          }
          n++;
        }
      });
      // Position is a POSE change, so the hands have to go there - rebase
      // would otherwise hold them where they were and only move the anchor.
      if (this._scope !== "motion") {
        for (let i = 0; i < C.NUM_CLOCKS; i++) {
          if (!pred(i)) continue;
          P.anchorAt(i, 0, P.get(i).h0, this._t);
          P.anchorAt(i, 1, P.get(i).h1, this._t);
        }
      }
      this._sync();
      const what = this._scope === "all" ? "pose and motion"
                 : this._scope === "position" ? "position" : "motion";
      this._note(`Copied ${what} to ${n} clock${n === 1 ? "" : "s"}.`);
    }

    _select(pred, label) {
      this._sel = new Set();
      for (let i = 0; i < C.NUM_CLOCKS; i++) if (pred(i)) this._sel.add(i);
      if (!this._sel.has(this._primary)) this._primary = this._sel.values().next().value;
      this._sync();
      this._note(`Selected ${label} — edits apply to ${this._sel.size}.`);
    }

    // ---- entity ------------------------------------------------------------
    _syncEntity() {
      const on = !!(this._cfg.entity && this._hass && this._hass.states[this._cfg.entity]);
      ["load", "send"].forEach(id => { this.$(id).disabled = !on; });
      if (!this._cfg.entity) this._note("Set `entity:` to a Pattern text entity to load and send.");
    }
    // ---- what is on the canvas, as text ------------------------------------
    // Public, because a pattern now has somewhere to go that is not an entity:
    // the add-on keeps a library, and the panel around this card drives it. The
    // card stays usable on its own in a dashboard either way.
    get patternText() { return P.toESPHome(this._cfg.name); }

    set patternText(text) {
      if (!this.applyPattern(text)) throw new Error("not a pattern string");
    }

    applyPattern(text, where) {
      const got = P.parseESPHome(text || "");
      if (!got) { this._note("That does not hold a pattern."); return false; }
      got.clocks.forEach((c, i) => {
        const s = P.get(i);
        s.h0 = c.h0; s.h1 = c.h1; s.dirA = c.dir0; s.dirB = c.dir1;
        s.spdA = { mode: "fixed", v: c.v0 }; s.spdB = { mode: "fixed", v: c.v1 };
      });
      P.toHome(this._t);
      this._sync();
      this._note(`Loaded '${got.name}'${where ? " " + where : ""}.`);
      return true;
    }

    _load() {
      const st = this._hass && this._hass.states[this._cfg.entity];
      if (!st) { this._note("Entity not found."); return; }
      this.applyPattern(st.state, "from the wall");
    }
    _send() {
      const value = P.toESPHome(this._cfg.name);
      this._hass.callService("text", "set_value", { entity_id: this._cfg.entity, value });
      this._note(`Sent ${value.length} characters — the wall has it in about a second.`);
    }

    // ---- selection and posing ---------------------------------------------
    _layout() {
      const cv = this._cv, w = cv.width, h = cv.height;
      // NO digit gap. The wall is 24 separate 240x240 panels on one frame, so
      // its pitch is even by construction - the same distance right and down
      // between every pair of faces. A gap here drew a wall that does not
      // exist, and squeezed the cells besides: 8 + 0.35*3 columns of layout
      // inside a canvas whose aspect-ratio is 8/3.
      const gap = 0;
      const cell = Math.min(w / (C.WALL_COLS + gap * 3), h / C.WALL_ROWS);
      return { cell, gap: cell * gap, r: cell * 0.46,
               x0: (w - (cell * C.WALL_COLS + cell * gap * 3)) / 2 + cell / 2,
               y0: (h - cell * C.WALL_ROWS) / 2 + cell / 2 };
    }
    _at(e) {
      const b = this._cv.getBoundingClientRect();
      return { x: (e.clientX - b.left) * this._cv.width / b.width,
               y: (e.clientY - b.top) * this._cv.height / b.height };
    }
    _centre(i, L) {
      const p = C.wallPos(i);
      return { x: L.x0 + p.col * L.cell + Math.floor(p.col / 2) * L.gap, y: L.y0 + p.row * L.cell };
    }
    _pick(pt, L) {
      let best = -1, bd = L.cell * 0.6;
      for (let i = 0; i < C.NUM_CLOCKS; i++) {
        const c = this._centre(i, L);
        const d = Math.hypot(pt.x - c.x, pt.y - c.y);
        if (d < bd) { bd = d; best = i; }
      }
      return best;
    }
    _down(e) {
      const L = this._layout(), pt = this._at(e), i = this._pick(pt, L);
      if (i < 0) return;
      if (e.shiftKey) {                      // add to the selection, never drag
        if (this._sel.has(i) && this._sel.size > 1) this._sel.delete(i); else this._sel.add(i);
        this._primary = this._sel.has(i) ? i : this._sel.values().next().value;
        this._sync(); return;
      }
      const first = !(this._sel.size === 1 && this._sel.has(i));
      this._sel = new Set([i]); this._primary = i;
      this._sync();
      if (first) return;                     // first click selects only
      const c = this._centre(i, L);
      const a = C.wrap360(Math.atan2(pt.y - c.y, pt.x - c.x) * 180 / Math.PI + 90);
      const d0 = Math.abs(C.shortestDelta(this._angle(i, 0), a));
      const d1 = Math.abs(C.shortestDelta(this._angle(i, 1), a));
      this._drag = { hand: d0 <= d1 ? 0 : 1 };
      this._cv.setPointerCapture(e.pointerId);
      this._move(e);
    }
    _move(e) {
      const L = this._layout(), pt = this._at(e), c = this._centre(this._primary, L);
      const snap = this._cfg.snap || 15;
      let a = C.wrap360(Math.atan2(pt.y - c.y, pt.x - c.x) * 180 / Math.PI + 90);
      a = ((Math.round(a / snap) * snap) % 360 + 360) % 360;
      this._each(i => P.setHomeAt(i, this._drag.hand, a, this._t));
      this._sync();
    }
    _hover(e) {
      const tip = this.$("tip");
      if (this._drag) { tip.style.display = "none"; return; }
      const L = this._layout(), pt = this._at(e), i = this._pick(pt, L);
      if (i < 0) { tip.style.display = "none"; return; }
      const s = P.get(i), r = P.resolved()[i], { col, row } = C.wallPos(i);
      const arrow = d => (d < 0 ? "\u21ba ccw" : d > 0 ? "\u21bb cw" : "\u25cf still");
      const line = (label, ang, dir, v, sp) =>
        `${label}  ${String(Math.round(ang)).padStart(3)}\u00b0  ${arrow(dir).padEnd(9)}` +
        (dir ? `${(v * MAX_RATE).toFixed(1)}\u00b0/s` : "") +
        (sp.mode === "rel"
          ? `  (${sp.from}${sp.d >= 0 ? "+" : ""}${(sp.d * 100).toFixed(1)}%)` : "");
      tip.textContent =
        `clock ${i} \u00b7 col ${col} row ${row}${this._sel.has(i) ? "  (selected)" : ""}\n` +
        line("A", s.h0, s.dirA, r[0], s.spdA) + "\n" +
        line("B", s.h1, s.dirB, r[1], s.spdB);
      // Positioned over the clock, in CSS pixels - the canvas backing store is
      // scaled for retina, so its own coordinates are the wrong unit here.
      const box = this._cv.getBoundingClientRect();
      const c = this._centre(i, L), k = box.width / this._cv.width;
      tip.style.left = (c.x * k) + "px";
      tip.style.top = (c.y * k - L.r * k) + "px";
      tip.style.display = "block";
    }

    _angle(i, hand) {
      const s = P.get(i);
      return C.wrap360((hand ? s.a1 : s.a0) + P.offsetAt(i, hand, this._t));
    }

    // ---- controls reflect the primary --------------------------------------
    _sync() {
      const s = P.get(this._primary), r = P.resolved()[this._primary];
      const { col, row } = C.wallPos(this._primary);
      const extra = this._sel.size > 1 ? ` +${this._sel.size - 1} more — edits apply to all ${this._sel.size}` : "";
      this.$("who").textContent =
        `clock ${this._primary} · col ${col} row ${row}${extra}` +
        ` — ${(r[0] * MAX_RATE).toFixed(1)}°/s · ${(r[1] * MAX_RATE).toFixed(1)}°/s`;
      this.shadowRoot.querySelectorAll("button.dir").forEach(b => {
        const cur = b.dataset.h === "0" ? s.dirA : s.dirB;
        b.setAttribute("aria-pressed", String(+b.dataset.v === cur));
      });
      [0, 1].forEach(h => {
        const v = r[h];                          // the RESOLVED rate
        const sp = h === 0 ? s.spdA : s.spdB;
        const rel = sp.mode === "rel";
        this.$("mode" + h).value = rel ? "rel" : "fixed";
        this.$("rel" + h).style.display = rel ? "flex" : "none";
        // The slider shows the resolved rate either way, so a relative hand
        // still tells you what it is actually doing - it is just not what you
        // drag to change it.
        this.$("spd" + h).value = sliderFromSpd(v);
        this.$("spd" + h).disabled = rel;
        if (rel) {
          this.$("from" + h).value = sp.from;
          this.$("d" + h).value = Math.round(sp.d * 100 * 2) / 2;
          this.$("d" + h + "v").textContent =
            `${sp.d >= 0 ? "+" : ""}${(sp.d * 100).toFixed(1)}%`;
        }
        this.$("spd" + h + "v").textContent =
          `${sliderFromSpd(v).toFixed(1)}% · ${(v * MAX_RATE).toFixed(1)}°/s`;
      });
      this._syncEntity();
    }

    // ---- loop --------------------------------------------------------------
    _start() {
      if (this._raf) return;
      let last = performance.now();
      const tick = (t) => {
        this._raf = requestAnimationFrame(tick);
        const dt = Math.min((t - last) / 1000, 0.1);
        last = t;
        if (this._running) this._t += dt;
        this._render();
      };
      this._raf = requestAnimationFrame(tick);
    }
    _stop() { if (this._raf) cancelAnimationFrame(this._raf); this._raf = null; }

    _render() {
      const cv = this._cv, ctx = this._ctx;
      const rect = cv.getBoundingClientRect();
      const dpr = window.devicePixelRatio || 1;
      const w = Math.max(1, Math.round(rect.width * dpr)), h = Math.max(1, Math.round(rect.height * dpr));
      if (cv.width !== w || cv.height !== h) { cv.width = w; cv.height = h; }
      ctx.fillStyle = "#000"; ctx.fillRect(0, 0, w, h);

      const L = this._layout(), lw = Math.max(1, L.r / 8);
      for (let i = 0; i < C.NUM_CLOCKS; i++) {
        const c = this._centre(i, L);
        if (this._sel.has(i)) {
          ctx.strokeStyle = i === this._primary ? "#6ea8fe" : "#31537f";
          ctx.lineWidth = 2;
          ctx.beginPath(); ctx.arc(c.x, c.y, L.r + 5, 0, Math.PI * 2); ctx.stroke();
        }
        ctx.strokeStyle = "#fff"; ctx.fillStyle = "#fff";
        ctx.lineWidth = lw; ctx.lineCap = "butt";
        for (const hand of [0, 1]) {
          const rad = (this._angle(i, hand) - 90) * Math.PI / 180;
          ctx.beginPath(); ctx.moveTo(c.x, c.y);
          ctx.lineTo(c.x + Math.cos(rad) * L.r, c.y + Math.sin(rad) * L.r);
          ctx.stroke();
        }
        ctx.beginPath(); ctx.arc(c.x, c.y, lw / 2, 0, Math.PI * 2); ctx.fill();
      }
    }
  }

  customElements.define("clockclock24-editor-card", ClockClock24EditorCard);
  window.customCards = window.customCards || [];
  window.customCards.push({
    type: "clockclock24-editor-card",
    name: "ClockClock 24 — pattern editor",
    description: "Draw a motion pattern, watch it run, and send it to the wall.",
    preview: false,
  });
})();
