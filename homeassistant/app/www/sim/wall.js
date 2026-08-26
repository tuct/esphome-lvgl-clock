// The mode state machine: entry blend, live choreography, settle back to time.
// Ported from LvglClock::blend_into_mode_(), retarget_() and settle_blend_().
//
// This is the part that makes the wall look like an analogue clock rather than
// a display: NOTHING ever jumps. A mode change fades each hand from where it
// was into the choreography, staggered by wall column; leaving one fades the
// remaining offset out while the choreography keeps running underneath.

(function () {
  const C = window.CC;
  const { NUM_HANDS, NUM_DIGITS, CLOCKS_PER_DIGIT, PARK, FONT,
          wrap360, shortestDelta, ease, easeOut, wallPos, MODES, resolveMode } = C;

  const BLEND_NONE = 0, BLEND_PENDING = 1, BLEND_ACTIVE = 2;

  class Wall {
    constructor() {
      this.cur = new Float64Array(NUM_HANDS).fill(PARK);
      this.mode = "time";
      this.modeSpeed = 1.0;
      this.transitionMs = 5000;
      this.staggerMs = C.K.COLUMN_STAGGER_MS;
      this.movement = "opposite";
      this.digits = [1, 2, 3, 4];
      this.temp = 21;

      this.animT = 0;              // choreography time base, seconds
      this.modeStartMs = 0;        // when the current mode was entered

      this.blendState = BLEND_NONE;
      this.blendOff = new Float64Array(NUM_HANDS);
      this.blendStart = 0;

      this.animating = false;      // a time sweep / settle is running
      this.animStart = 0;
      this.start = new Float64Array(NUM_HANDS);
      this.target = new Float64Array(NUM_HANDS);
      this.settleFrom = "time";
      this.settleDelta = new Float64Array(NUM_HANDS);
      this.settlePrev = new Float64Array(NUM_HANDS);
    }

    // How long a full mode entry takes: the sweep plus the column stagger.
    modeEntryLeadS() {
      return (this.transitionMs + (C.WALL_COLS - 1) * this.staggerMs) / 1000;
    }

    setMode(m, nowMs) {
      // A name off a real wall may be the firmware's rather than ours.
      m = resolveMode(m);
      // And an unknown one is REFUSED rather than stored. Storing it meant
      // frame() looked up undefined and threw on every animation frame, which
      // does not stop the loop - it just kills the wall and floods the console.
      if (!MODES[m]) {
        console.warn(`[clockclock24] unknown mode "${m}" - staying on "${this.mode}"`);
        return;
      }
      if (m === this.mode) return;
      if (m === "time") { this.settleToTime(nowMs); return; }
      // Remember where every hand is; blend_into_mode_() converts these to
      // signed offsets on the first frame, once cur[] holds the animation.
      this.blendOff.set(this.cur);
      this.blendState = BLEND_PENDING;
      this.mode = m;
      this.modeStartMs = nowMs;
      this.animT = 0;              // hold the choreography at its start pose
      this.animating = false;
    }

    // ---- retarget_(): work out where every hand has to end up, and how -----
    settleToTime(nowMs) {
      this.settleFrom = this.mode;
      this.mode = "time";
      // The entry blend is deliberately NOT cancelled. A mode can be left
      // before it has finished fading in - a short window, or two mode changes
      // in quick succession - and cur[] currently includes whatever offset is
      // still outstanding. Dropping it makes the very next frame render the
      // raw choreography instead: a jump of exactly that offset, up to 180
      // degrees, which is measurable and was happening.
      for (let d = 0; d < NUM_DIGITS; d++) {
        const val = this.digits[d];
        const blank = val < 0 || val > 9;
        for (let c = 0; c < CLOCKS_PER_DIGIT; c++) {
          for (let h = 0; h < 2; h++) {
            const hi = (d * CLOCKS_PER_DIGIT + c) * 2 + h;
            const goal = blank ? PARK : FONT[val][c][h];
            const base = wrap360(this.cur[hi]);
            this.start[hi] = base;
            let cw = (goal - base) % 360;
            if (cw < 0) cw += 360;
            const ccw = cw - 360;
            let delta;
            if (cw < 0.001) {
              delta = 0;
            } else if (this.settleFrom !== "time") {
              // Leaving a choreography: ALWAYS the shortest way round,
              // whatever `movement:` says. The hands are already moving, and
              // `long` would run them at ~4x the choreography's own rate.
              delta = cw <= 180 ? cw : ccw;
            } else {
              switch (this.movement) {
                case "clockwise": delta = cw; break;
                case "counter":   delta = ccw; break;
                case "long":      delta = cw >= 180 ? cw : ccw; break;
                default:          delta = h === 0 ? cw : ccw; break;   // opposite
              }
            }
            this.target[hi] = base + delta;
          }
        }
      }
      this.animStart = nowMs;
      this.animating = true;
      if (this.settleFrom !== "time") {
        for (let i = 0; i < NUM_HANDS; i++) {
          this.settlePrev[i] = this.start[i];
          this.settleDelta[i] = this.target[i] - this.start[i];
        }
      }
    }

    // A digit change while showing the time: an ordinary eased sweep.
    setDigits(digits, nowMs) {
      this.digits = digits;
      if (this.mode === "time") { this.settleFrom = "time"; this.settleToTime(nowMs); }
    }

    // ---- blend_into_mode_() -----------------------------------------------
    blendIntoMode(nowMs) {
      if (this.blendState === BLEND_NONE) return;
      if (this.blendState === BLEND_PENDING) {
        // Measured ONCE, so the direction can never flip sign mid-fade.
        for (let i = 0; i < NUM_HANDS; i++)
          this.blendOff[i] = shortestDelta(this.cur[i], this.blendOff[i]);
        this.blendStart = nowMs;
        this.blendState = BLEND_ACTIVE;
      }
      const elapsed = nowMs - this.blendStart;
      let allDone = true;
      for (let i = 0; i < NUM_HANDS; i++) {
        const { col } = wallPos(i >> 1);
        const delay = col * this.staggerMs;
        let k;
        if (this.transitionMs === 0) k = 0;
        else if (elapsed <= delay) { k = 1; allDone = false; }
        else {
          const t = (elapsed - delay) / this.transitionMs;
          if (t >= 1) k = 0;
          else { k = 1 - ease(t); allDone = false; }
        }
        if (k !== 0) this.cur[i] = wrap360(this.cur[i] + this.blendOff[i] * k);
      }
      if (allDone) this.blendState = BLEND_NONE;
      return !allDone;
    }

    // ---- settle_blend_(): the choreography keeps running underneath -------
    settleBlend(nowMs) {
      const elapsed = nowMs - this.animStart;
      let allDone = true;
      for (let i = 0; i < NUM_HANDS; i++) {
        const anim = this.cur[i];
        // delta is carried forward and reduced by the animation's own
        // movement, so `anim + delta` stays pinned on the target throughout.
        this.settleDelta[i] -= shortestDelta(this.settlePrev[i], anim);
        this.settlePrev[i] = anim;

        const { col } = wallPos(i >> 1);
        const delay = col * this.staggerMs;
        let w;
        if (this.transitionMs === 0) w = 1;
        else if (elapsed <= delay) { w = 0; allDone = false; }
        else {
          const t = (elapsed - delay) / this.transitionMs;
          if (t >= 1) w = 1;
          else { w = easeOut(t); allDone = false; }
        }
        this.cur[i] = wrap360(anim + this.settleDelta[i] * w);
      }
      if (allDone) { this.animating = false; this.settleFrom = "time"; }
      return !allDone;
    }

    // A plain eased sweep, for a digit change with no choreography behind it.
    plainSweep(nowMs) {
      const elapsed = nowMs - this.animStart;
      let allDone = true;
      for (let i = 0; i < NUM_HANDS; i++) {
        const { col } = wallPos(i >> 1);
        const delay = col * this.staggerMs;
        let t;
        if (this.transitionMs === 0) t = 1;
        else if (elapsed <= delay) { t = 0; allDone = false; }
        else {
          t = (elapsed - delay) / this.transitionMs;
          if (t >= 1) t = 1; else allDone = false;
        }
        this.cur[i] = wrap360(this.start[i] + (this.target[i] - this.start[i]) * ease(t));
      }
      if (allDone) this.animating = false;
      return !allDone;
    }

    // ---- one frame ---------------------------------------------------------
    frame(nowMs, dtS) {
      const settling = this.animating && this.settleFrom !== "time";
      const live = settling ? this.settleFrom : this.mode;
      // Belt and braces: setMode refuses unknown modes, so this can only fire
      // if something set .mode directly. Showing the time beats throwing.
      const spec = MODES[live] || MODES.time;

      // The choreography clock is HELD AT 0 until the entry blend finishes, so
      // the blend chases a STILL target. Without this every hand fades toward
      // a moving pose and the wall never starts uniform — which is exactly the
      // "wind doesn't start with all hands in the same place" bug.
      //
      // Once we are settling OUT of a mode, the choreography has to keep
      // running underneath, or the wall freezes and then sweeps instead of
      // winding down out of the movement.
      if (this.blendState === BLEND_NONE || settling) this.animT += dtS;

      spec.fn(this.cur, this.animT, {
        modeSpeed: this.modeSpeed, digits: this.digits, temp: this.temp,
      });

      // Applied BEFORE the settle, so an entry blend that is still outstanding
      // keeps fading on top of the choreography rather than vanishing.
      const blending = this.blendState !== BLEND_NONE ? this.blendIntoMode(nowMs) : false;

      if (settling) return this.settleBlend(nowMs) || blending;
      if (blending) return true;
      if (this.animating) return this.plainSweep(nowMs);
      return false;
    }
  }

  window.CC.Wall = Wall;
})();
