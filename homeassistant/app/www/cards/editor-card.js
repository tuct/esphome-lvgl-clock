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
  // Speed sliders are SQUARED: a linear 0..1 spends nine tenths of its travel
  // above 9 deg/s, and the slow end is where a pattern reads.
  const spdFromSlider = p => p * p;
  const sliderFromSpd = v => Math.sqrt(Math.max(0, v));

  class ClockClock24EditorCard extends HTMLElement {
    setConfig(cfg) {
      this._cfg = Object.assign({ entity: null, snap: 15, name: "pattern" }, cfg || {});
      this._sel = new Set([0]);
      this._primary = 0;
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
          .val { min-width:5.5em; text-align:right; font-variant-numeric:tabular-nums; font-size:.85em; }
          .hint { margin:8px 0 0; font-size:.8em; opacity:.7; }
          textarea { width:100%; margin-top:8px; min-height:60px; font:11px/1.4 ui-monospace,monospace;
                     background:#0e1013; color:#8fd0a0; border:1px solid var(--divider-color,#444);
                     border-radius:6px; padding:6px; }
        </style>
        <ha-card>
          <canvas></canvas>
          <div class="pad">
            <p class="hint" id="who"></p>

            <div class="row"><span class="lbl">hand A</span>
              <button class="dir" data-h="0" data-v="-1" title="counter-clockwise">←</button>
              <button class="dir" data-h="0" data-v="0">—</button>
              <button class="dir" data-h="0" data-v="1" title="clockwise">→</button>
              <input type="range" id="spd0" min="0" max="1" step="0.005">
              <span class="val" id="spd0v"></span>
            </div>
            <div class="row"><span class="lbl">hand B</span>
              <button class="dir" data-h="1" data-v="-1" title="counter-clockwise">←</button>
              <button class="dir" data-h="1" data-v="0">—</button>
              <button class="dir" data-h="1" data-v="1" title="clockwise">→</button>
              <input type="range" id="spd1" min="0" max="1" step="0.005">
              <span class="val" id="spd1v"></span>
            </div>

            <div class="row">
              <button id="play" aria-pressed="true">Pause</button>
              <button id="home" title="Every hand back on the pose you configured">Back to pose</button>
              <button id="all">Copy to all 24</button>
              <button id="row">to row</button>
              <button id="col">to column</button>
            </div>

            <div class="row" id="entityRow">
              <button id="load">Load from wall</button>
              <button id="send">Send to wall</button>
              <button id="copy">Copy string</button>
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
      [0, 1].forEach(h => {
        $("spd" + h).oninput = e => this._rebase(() => this._each(i => {
          P.get(i)[h === 0 ? "spdA" : "spdB"] = { mode: "fixed", v: spdFromSlider(+e.target.value) };
        }));
      });

      $("play").onclick = () => {
        this._running = !this._running;
        $("play").textContent = this._running ? "Pause" : "Play";
        $("play").setAttribute("aria-pressed", String(this._running));
        this._rebase(() => {});   // resume from where the hands are
      };
      $("home").onclick = () => { P.toHome(this._t); this._sync(); };
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

    _spread(pred) {
      const src = P.copy(this._primary);
      for (let i = 0; i < C.NUM_CLOCKS; i++) if (pred(i)) P.paste(i, src);
      this._sync();
      this._note("Copied — pose and motion.");
    }

    // ---- entity ------------------------------------------------------------
    _syncEntity() {
      const on = !!(this._cfg.entity && this._hass && this._hass.states[this._cfg.entity]);
      ["load", "send"].forEach(id => { this.$(id).disabled = !on; });
      if (!this._cfg.entity) this._note("Set `entity:` to a Pattern text entity to load and send.");
    }
    _load() {
      const st = this._hass && this._hass.states[this._cfg.entity];
      if (!st) { this._note("Entity not found."); return; }
      const got = P.parseESPHome(st.state);
      if (!got) { this._note("That entity does not hold a pattern."); return; }
      got.clocks.forEach((c, i) => {
        const s = P.get(i);
        s.h0 = c.h0; s.h1 = c.h1; s.dirA = c.dir0; s.dirB = c.dir1;
        s.spdA = { mode: "fixed", v: c.v0 }; s.spdB = { mode: "fixed", v: c.v1 };
      });
      P.toHome(this._t);
      this._sync();
      this._note(`Loaded '${got.name}' from the wall.`);
    }
    _send() {
      const value = P.toESPHome(this._cfg.name);
      this._hass.callService("text", "set_value", { entity_id: this._cfg.entity, value });
      this._note(`Sent ${value.length} characters — the wall has it in about a second.`);
    }

    // ---- selection and posing ---------------------------------------------
    _layout() {
      const cv = this._cv, w = cv.width, h = cv.height;
      const gap = 0.35;
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
        const v = r[h];
        this.$("spd" + h).value = sliderFromSpd(v);
        this.$("spd" + h + "v").textContent = (v * MAX_RATE).toFixed(1) + "°/s";
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
