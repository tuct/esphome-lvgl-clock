// ClockClock 24 engine, ported from components/lvgl_clock/lvgl_clock.cpp.
//
// This is a DELIBERATE line-for-line port, not a reimplementation: the same
// constants, the same tables, the same per-frame maths, so a choreography
// prototyped here drops into the C++ with only syntax changes. Where the C++
// writes `this->cur_[i]`, this writes `cur[i]`; where it uses `wall_pos_(c)`,
// this uses `wallPos(c)`. Keep it that way.
//
// Loaded as a CLASSIC script (no ES modules), so index.html opens straight
// from file:// with no server. Everything lands on window.CC at the bottom.
//
// Angles are degrees, 0 = 12 o'clock, clockwise. cur[] is 48 hands:
// hand index = clock * 2 + hand, clock = digit * 6 + cell, cell = row * 2 + col.

const NUM_DIGITS = 4;
const CLOCKS_PER_DIGIT = 6;
const NUM_CLOCKS = NUM_DIGITS * CLOCKS_PER_DIGIT;   // 24
const NUM_HANDS = NUM_CLOCKS * 2;                   // 48
const WALL_COLS = 8;
const WALL_ROWS = 3;

const PARK = 225.0;   // clockclock idle: both hands to bottom-left

// ---------------------------------------------------------------- glyphs ----
// Target angle of the two hands of each of a digit's 6 clocks (TL,TR,ML,MR,BL,BR).
const FONT = [
  [[90,180],[180,270],[0,180],[0,180],[0,90],[0,270]],            // 0
  [[PARK,PARK],[180,180],[PARK,PARK],[0,180],[PARK,PARK],[0,0]],  // 1
  [[90,90],[180,270],[90,180],[0,270],[0,90],[270,270]],          // 2
  [[90,90],[180,270],[90,90],[0,270],[90,90],[0,270]],            // 3
  [[180,180],[180,180],[0,90],[0,180],[PARK,PARK],[0,0]],         // 4
  [[90,180],[270,270],[0,90],[180,270],[90,90],[0,270]],          // 5
  [[90,180],[270,270],[0,180],[180,270],[0,90],[0,270]],          // 6
  [[90,90],[180,270],[PARK,PARK],[0,180],[PARK,PARK],[0,0]],      // 7
  [[90,180],[180,270],[0,90],[0,270],[0,90],[0,270]],             // 8
  [[90,180],[180,270],[0,90],[0,180],[90,90],[0,270]],            // 9
];

const LOVE = [
  [[180,180],[PARK,PARK],[0,180],[PARK,PARK],[0,90],[270,270]],   // L
  [[90,180],[180,270],[0,180],[0,180],[0,90],[0,270]],            // O
  [[180,180],[180,180],[0,180],[0,180],[0,105],[0,255]],          // V
  [[90,180],[270,270],[0,180],[270,270],[0,90],[270,270]],        // E
];

const TG_DEGREE = 0, TG_C = 1, TG_MINUS = 2, TG_BLANK = 3;
const TEMP_GLYPHS = [
  [[90,180],[180,270],[0,90],[0,270],[PARK,PARK],[PARK,PARK]],                 // degree
  [[PARK,PARK],[PARK,PARK],[90,180],[270,270],[0,90],[270,270]],               // c
  [[PARK,PARK],[PARK,PARK],[90,90],[270,270],[PARK,PARK],[PARK,PARK]],         // minus
  [[PARK,PARK],[PARK,PARK],[PARK,PARK],[PARK,PARK],[PARK,PARK],[PARK,PARK]],   // blank
];

// --------------------------------------------------------------- helpers ----
const wrap360 = a => ((a % 360) + 360) % 360;

function shortestDelta(a, b) {
  let d = (b - a) % 360;
  if (d < 0) d += 360;
  if (d > 180) d -= 360;
  return d;
}

// smootherstep: starts and ends at rest. For moves that begin from a hold.
const ease = t => t * t * t * (t * (t * 6 - 15) + 10);
// Decelerate-only: full speed at t=0. For LEAVING a choreography, which is
// already moving — smootherstep there reads as a pause before the clock returns.
const easeOut = t => { const u = 1 - t; return 1 - u * u * u; };

function wallPos(c) {
  const digit = Math.floor(c / CLOCKS_PER_DIGIT);
  const cell = c % CLOCKS_PER_DIGIT;
  return { row: Math.floor(cell / 2), col: digit * 2 + (cell % 2) };
}

// ------------------------------------------------------------- constants ----
const K = {
  COLUMN_STAGGER_MS: 250,
  CYCLE_WINDOW_S: 35,
  CYCLE_OFFSET_S: 10,
  TEMP_UPDATE_BUFFER_S: 1.0,

  BIRDS_RATE_RAD_S: 2.0 * 0.5,
  BIRDS_COL_LAG_S: 0.15,
  BIRDS_RAMP_S: 2.5,

  WAVE_TURN_S: 15.3,
  WAVE_COL_LAG_DEG: 15.0,
  WAVE_REST_DEG: 315.0,
  WAVE_RAMP_S: 1.0,

  SPIRAL_TURN_S: 7.5,
  SPIRAL_DIAG_LAG_DEG: 40.0,
  SPIRAL_RAMP_S: 1.0,

  WIND_BEND_DEG: 90.0,
  WIND_RISE_S: 2.0,
  WIND_HOLD_S: 0.0,
  WIND_FALL_S: 2.0,
  WIND_SPREAD_S: 1.2,
  get WIND_CYCLE_S() {
    return this.WIND_RISE_S + 2 * this.WIND_SPREAD_S + this.WIND_HOLD_S + this.WIND_FALL_S;
  },
};

// ---------------------------------------------------------- choreographies --
// Each takes (cur, t, ctx) and writes 48 angles into cur. t is seconds of
// animation time; ctx carries { modeSpeed, digits, temp }.
//
// ADD A NEW MODE HERE. Register it in MODES at the bottom, and it appears in
// the UI automatically.

function tickRotate(cur, t, { modeSpeed }) {
  const ang = (t * 45.0 * modeSpeed) % 360;
  const a = 360 - ang;
  for (let c = 0; c < NUM_CLOCKS; c++) {
    cur[c * 2 + 0] = a;
    cur[c * 2 + 1] = wrap360(a + 180);
  }
}

function tickBirds(cur, t, { modeSpeed }) {
  const rate = K.BIRDS_RATE_RAD_S * modeSpeed;
  const ts = t * modeSpeed;
  for (let c = 0; c < NUM_CLOCKS; c++) {
    const { col } = wallPos(c);
    const x = ts - col * K.BIRDS_COL_LAG_S;
    let ph;
    if (x <= 0) ph = 0;
    else if (x < K.BIRDS_RAMP_S) ph = rate * x * x / (2 * K.BIRDS_RAMP_S);
    else ph = rate * (x - K.BIRDS_RAMP_S / 2);
    const wing = 85 + 45 * Math.sin(ph % (2 * Math.PI));
    cur[c * 2 + 0] = wing;
    cur[c * 2 + 1] = 360 - wing;
  }
}

function tickWave(cur, t, { modeSpeed }) {
  const rate = 360 / K.WAVE_TURN_S;
  const colDelay = K.WAVE_COL_LAG_DEG / rate;
  const ts = t * modeSpeed;
  for (let c = 0; c < NUM_CLOCKS; c++) {
    const { col } = wallPos(c);
    const x = ts - col * colDelay;
    let turned;
    if (x <= 0) turned = 0;
    else if (x < K.WAVE_RAMP_S) turned = rate * x * x / (2 * K.WAVE_RAMP_S);
    else turned = rate * (x - K.WAVE_RAMP_S / 2);
    const a = wrap360(K.WAVE_REST_DEG + (turned % 360));
    cur[c * 2 + 0] = a;
    cur[c * 2 + 1] = wrap360(a + 180);
  }
}

function tickSpiral(cur, t, { modeSpeed }) {
  const span = (WALL_COLS - 1) + (WALL_ROWS - 1);
  const rate = 360 / K.SPIRAL_TURN_S;
  const ts = t * modeSpeed;
  for (let c = 0; c < NUM_CLOCKS; c++) {
    const { col, row } = wallPos(c);
    const diag = col + ((WALL_ROWS - 1) - row);
    const x = ts - (diag / span) * (K.SPIRAL_DIAG_LAG_DEG / rate);
    let turned;
    if (x <= 0) turned = 0;
    else if (x < K.SPIRAL_RAMP_S) turned = rate * x * x / (2 * K.SPIRAL_RAMP_S);
    else turned = rate * (x - K.SPIRAL_RAMP_S / 2);
    const a = wrap360(PARK - (turned % 360));
    cur[c * 2 + 0] = a;
    cur[c * 2 + 1] = a;
  }
}

// A gust bending a stalk. Read a column top to bottom and the three clocks are
// one continuous stroke; only the two free ends move, and they shear past each
// other — top tip 10:30→1:30 over the top, bottom tip 4:30→7:30 underneath.
function tickWind(cur, t, { modeSpeed }) {
  const REST = [[315, 180], [0, 180], [0, 135]];
  const MOVES = [0, -1, 1];      // which hand the gust bends, -1 = none
  const SENSE = [1, 0, 1];
  const ts = t * modeSpeed;

  for (let c = 0; c < NUM_CLOCKS; c++) {
    const { col, row } = wallPos(c);
    let u = ts % K.WIND_CYCLE_S;
    if (u < 0) u += K.WIND_CYCLE_S;

    // The bottom row reads the wall from the other end in BOTH phases:
    //   top     push  left→right    release  right→left
    //   bottom  push  right→left    release  left→right
    const lag = col / (WALL_COLS - 1) * K.WIND_SPREAD_S;
    const eff = (row === WALL_ROWS - 1) ? (K.WIND_SPREAD_S - lag) : lag;
    const pushAt = eff;
    const releaseAt = K.WIND_RISE_S + K.WIND_HOLD_S + 2 * K.WIND_SPREAD_S - eff;

    let lean;
    if (u < pushAt) lean = 0;
    else if (u < pushAt + K.WIND_RISE_S) lean = K.WIND_BEND_DEG * ease((u - pushAt) / K.WIND_RISE_S);
    else if (u < releaseAt) lean = K.WIND_BEND_DEG;
    else if (u < releaseAt + K.WIND_FALL_S) lean = K.WIND_BEND_DEG * (1 - ease((u - releaseAt) / K.WIND_FALL_S));
    else lean = 0;
    lean *= SENSE[row];

    for (let hnd = 0; hnd < 2; hnd++) {
      let a = REST[row][hnd];
      if (MOVES[row] === hnd) a += lean;
      cur[c * 2 + hnd] = wrap360(a);
    }
  }
}

// A held pose, not an animation: the entry blend and the settle do the moving.
function poseFromTable(cur, table) {
  for (let d = 0; d < NUM_DIGITS; d++)
    for (let c = 0; c < CLOCKS_PER_DIGIT; c++)
      for (let h = 0; h < 2; h++)
        cur[(d * CLOCKS_PER_DIGIT + c) * 2 + h] = table[d][c][h];
}

function tickLove(cur) { poseFromTable(cur, LOVE); }

function tickTemp(cur, t, { temp }) {
  let v = temp, neg = false;
  if (v < 0) { neg = true; v = -v; }
  if (v > 99) v = 99;
  // [tens or sign] [units] [degree] [C]
  const glyph = [0, 0, TG_DEGREE, TG_C];
  const isDigit = [true, true, false, false];
  if (neg) { glyph[0] = TG_MINUS; isDigit[0] = false; glyph[1] = v > 9 ? 9 : v; }
  else if (v >= 10) { glyph[0] = Math.floor(v / 10); glyph[1] = v % 10; }
  else { glyph[0] = TG_BLANK; isDigit[0] = false; glyph[1] = v; }

  for (let d = 0; d < NUM_DIGITS; d++) {
    const src = isDigit[d] ? FONT[glyph[d]] : TEMP_GLYPHS[glyph[d]];
    for (let c = 0; c < CLOCKS_PER_DIGIT; c++)
      for (let h = 0; h < 2; h++)
        cur[(d * CLOCKS_PER_DIGIT + c) * 2 + h] = src[c][h];
  }
}

// ------------------------------------------------------ rotating_maze -------
// A chevron per clock, alternating by COLUMN - even columns point down (a
// peak), odd columns point up (a trough) - turning at one rate, with the
// DIRECTION alternating by ROW:
//
//   rows 0, 2   clockwise
//   row 1      counter-clockwise
//
// Both hands of a clock always turn the SAME way, so the pair stays a rigid
// 90 deg chevron and simply rotates - never opening or shutting in place.
//
// At t = 0 the wall is a clean interlocking pattern; the counter-rotating rows
// then shear past each other and it opens and closes through it, which is
// where the name comes from. The rate is not constant - see MAZE_FLOOR.
// One revolution AVERAGE, at mode_speed 1.0. With MAZE_STOPS = 4 that is a
// dwell every 5 s, and a whole revolution inside a 35 s cycle_modes window with
// room to spare.
const MAZE_TURN_S = 20.0;
// The turn does not run at a constant rate: it eases off where the pattern
// lands on an aligned figure, then runs on through the untidy angles between.
//
// Done as a TIME WARP, not a velocity: integrating a speed that depends on
// angle is an ODE, but bending the phase is closed-form and exact.
//
//   rate(x) = (1 - d G(x)) / (1 - d mean(G))        d set from MAZE_FLOOR
//   G(x)    = ((1 + cos x) / 2) ^ MAZE_SHARP          x = N (u - phi)
//
// The profile is a narrow BUMP, not a cosine. A plain cosine (MAZE_SHARP = 1)
// sits near its minimum for most of the cycle - 28% of every segment below half
// speed - which is why it read as stopping on each figure rather than easing
// past it. Raising the exponent narrows the dip WITHOUT deepening it: full
// speed through most of a segment, a brief ease as the figure lines up.
//
// G has a closed-form integral at any integer exponent, which is what keeps
// this exact rather than a lookup table:
//
//   G(x)  = cos^2k(x/2) = 4^-k [ C(2k,k) + 2 SUM_j C(2k,k-j) cos(jx) ]
//   IG(x) = 4^-k [ C(2k,k) x + 2 SUM_j C(2k,k-j) sin(jx)/j ]
//
// so theta(u) = (u - (d/N) IG(N(u - phi))) / (1 - d mean(G)), and
// theta(u + 2pi) = theta(u) + 2pi exactly - one whole turn per turn, however the
// rate is shaped.
// FOUR - one dwell every 90 deg, phased onto 45. The hands land on a 45 deg
// multiple every 45 deg of turn, which gives eight aligned figures per
// revolution in two families:
//
//   theta = 0, 90, 180, 270    chevrons - < > pairs, a diagonal lattice
//   theta = 45, 135, 225, 315  right-angle corners, a rectangular grid
//
// Only the FIRST family gets a dwell - the one where every hand sits on a
// DIAGONAL, which is the base pose the mode starts from. MAZE_PHASE_DEG = 0
// selects it; 45 picks the axis-aligned grid instead. Dwelling on both (STOPS
// 8) put a pause every 45 deg and made neither stand out.
//
// The rhythm per figure is MAZE_TURN_S / MAZE_STOPS.
const MAZE_STOPS = 4;          // slow-downs per revolution
// How NARROW the ease-off is - the exponent in the closed-form integral above,
// so a positive integer. 1 is a plain cosine and slows over most of the
// segment; higher concentrates the slowing into a short patch centred on the
// figure and leaves the rest of the segment at full speed.
const MAZE_SHARP = 8;
// The SLOWEST rate, as a fraction of the average - the one knob worth having
// here, because it is the thing you judge by eye. Any value in (0, 1] is safe
// by construction: the dip depth derived from it in MAZE_G always comes out
// <= 1, so the rate can never go negative and the hands can never run
// backwards. Lower = more of a pause on each figure, 1.0 = a constant rate.
const MAZE_FLOOR = 0.40;
const MAZE_PHASE_DEG = 0;      // which angle the slow-downs land on

// Binomial row C(2k, .), the mean of G, and the dip depth that lands the
// slowest rate exactly on MAZE_FLOOR. Worked out once at load:
//
//   floor = (1 - d) / (1 - d mean)   =>   d = (1 - floor) / (1 - floor mean)
//
const MAZE_G = (() => {
  const k = MAZE_SHARP, C = [1];
  for (let i = 1; i <= 2 * k; i++) {           // one row of Pascal's triangle
    C[i] = 1;
    for (let j = i - 1; j > 0; j--) C[j] = C[j] + C[j - 1];
  }
  const scale = Math.pow(4, -k);
  const mean = C[k] * scale;
  return { k, scale, coef: C, mean,
           dwell: (1 - MAZE_FLOOR) / (1 - MAZE_FLOOR * mean) };
})();

// The warped angle for a linear phase u, in radians. Monotonic in u, and
// mazeRaw(u + 2pi) = mazeRaw(u) + 2pi.
function mazeRaw(u) {
  const { k, scale, coef, mean, dwell } = MAZE_G;
  // MAZE_PHASE_DEG is the OUTPUT angle a dwell should land on, but the offset
  // has to be applied to the LINEAR phase - and the normalisation below divides
  // that by (1 - dwell*mean). So scale it first, or the dwells come out at
  // phase/(1 - dwell*mean): asking for 45 deg silently gave 51.6. Invisible
  // while the constant was 0, since 0 scales to 0.
  //
  //   minima land at  theta = MAZE_PHASE_DEG + k * 360/MAZE_STOPS
  //
  const phi = MAZE_PHASE_DEG * Math.PI / 180 * (1 - dwell * mean);
  const x = MAZE_STOPS * (u - phi);
  let ig = coef[k] * x;                        // the integral of G, term by term
  for (let j = 1; j <= k; j++) ig += 2 * coef[k - j] * Math.sin(j * x) / j;
  ig *= scale;
  return (u - (dwell / MAZE_STOPS) * ig) / (1 - dwell * mean);
}

// The linear phase at which the turn is back at zero.
//
// A non-zero MAZE_PHASE_DEG shifts the whole curve, so mazeRaw(0) is no longer
// 0 and the mode would START part-way round - the wall coming up on some
// arbitrary angle instead of its base pose. Solving for the u where the turn is
// zero and beginning there fixes the start WITHOUT moving the dwells, which
// subtracting mazeRaw(0) would have done. Bisection is fine: mazeRaw is
// monotonic by construction.
const MAZE_U0 = (() => {
  let lo = -Math.PI, hi = Math.PI;
  for (let i = 0; i < 100; i++) {
    const m = (lo + hi) / 2;
    if (mazeRaw(m) < 0) lo = m; else hi = m;
  }
  return (lo + hi) / 2;
})();

// Linear phase in, eased-around-the-figures angle out. Degrees.
function mazePhase(ts) {
  const TAU = Math.PI * 2;
  // Wrap the LINEAR phase, not the output: mazeRaw(u + TAU) = mazeRaw(u) + TAU,
  // so the seam is a whole turn and the hand does not step.
  let u = (ts / MAZE_TURN_S) * TAU % TAU;
  if (u < 0) u += TAU;
  return mazeRaw(u + MAZE_U0) * 180 / Math.PI;
}

function cellAt(cur, row, col, a, b) {
  const c = (col >> 1) * CLOCKS_PER_DIGIT + row * 2 + (col & 1);
  cur[c * 2 + 0] = a;
  cur[c * 2 + 1] = b;
}

function tickRotatingMaze(cur, t, { modeSpeed }) {
  const turned = mazePhase(t * modeSpeed);

  for (let row = 0; row < WALL_ROWS; row++) {
    // Whole ROWS turn opposite ways: rows 0 and 2 clockwise, row 1
    // counter-clockwise. Both hands of a clock always take the SAME direction,
    // so the pair stays a rigid 90 deg chevron and simply turns.
    const sense = (row & 1) ? -1 : 1;
    for (let col = 0; col < WALL_COLS; col++) {
      // even column: 135/225, a chevron pointing DOWN
      // odd  column:  45/315, a chevron pointing UP
      const base = (col & 1) ? [45, 315] : [135, 225];
      cellAt(cur, row, col,
             wrap360(base[0] + sense * turned),
             wrap360(base[1] + sense * turned));
    }
  }
}

// ------------------------------------------------------------- zipper ------
// The whole wall rests as one field of top-left-to-bottom-right diagonals, and
// a front travels across it from left to right, unzipping each column into a
// pair of mirrored chevrons and doing it back up behind.
//
//   at rest   \  \  \  \  \  \  \  \
//   the front \  \  \  >  <  /  /  /      the two hands have come apart
//   after     /  /  /  /  /  /  /  /      ...and closed again on the far side
//
// The rest pose puts the hands 180 deg apart (315 and 135), so a column at rest
// is ONE straight stroke. A pass swings each hand by 90 deg, which turns the
// stroke from \\ to / - and the next pass turns it back.
//
// TWO THINGS MAKE THE FIGURE, and it does not appear without either:
//
//   1. The swing is 90 deg, not 180. At 180 the chevron's opening rotates all
//      the way round - > then v then < then ^ - so no single shape is ever on
//      screen for more than an instant.
//   2. Hand 1 does not start until hand 0 has FINISHED (ZIP_HAND_LAG_S >
//      ZIP_FLIP_S). Overlap the two and they are never more than about 40 deg
//      apart, which is a slightly bent line rather than a chevron. Separated,
//      the stroke opens to a full right angle and HOLDS there for
//      HAND_LAG - FLIP seconds before closing.
//
// Which hand leads alternates by column, so the front is a row of mirrored
// chevrons - > < > < - rather than eight identical ones.
// One hand's 90 deg swing. The eased swing peaks at 1.875x its average rate and
// mode_speed scales it, so going much below this breaks the 20 deg/frame
// ceiling in check.js at mode_speed 3.
const ZIP_FLIP_S = 0.9;
// Hand 1 behind hand 0. MUST EXCEED ZIP_FLIP_S - the difference is how long
// the chevron is held fully open before it shuts.
const ZIP_HAND_LAG_S = 1.2;
// A column is in motion for HAND_LAG + FLIP = 2.1 s, so the number of columns
// moving AT ONCE is that divided by the lag between them. Too small and every
// column moves at once and there is no front to see; 0.5 s keeps it to about
// FOUR, with the rest of the wall sitting still on the diagonal.
const ZIP_COL_LAG_S = 0.5;
// Set from the GAP wanted between one front and the next, in columns. A column
// is in motion for HAND_LAG + FLIP = 2.1 s, which at COL_LAG is a front about
// four columns wide; resting a further ZIP_GAP_COLS worth on top of that sets
// how closely the fronts follow each other.
//
//   spatial period = (front width + gap) columns
//   cycle          = motion + gap * COL_LAG = FLIP + HOLD
//
// At 9 the gap is wide enough that a front finishes crossing before the next
// sets off, so there is only ever ONE front on the wall - it arrives, passes
// through, and the wall is a clean field of diagonals again before the next
// one starts. A small gap (2) keeps a front on the wall permanently with the
// next already coming in behind it, which reads as much busier.
const ZIP_GAP_COLS = 9;
// How far a resting column drifts before the next pass reaches it. Applied to
// BOTH hands, so a resting stroke stays straight and simply leans - the wall is
// never quite frozen between passes.
//
// It ramps across the whole rest, so this also SETS THE SPEED: the lean rate is
// ZIP_DRIFT_DEG / ZIP_HOLD_S. At 30 deg over a 5.7 s rest that is ~5.3 deg/s,
// which is clearly moving without the field ever straying far from its
// diagonal.
//
// The direction comes from which pose the column is currently resting on, and
// since a pass flips it between \\ and /, the drift reverses every pass. That is
// what keeps it BOUNDED: it sways between 0 and this and back, instead of a
// constant creep that would have the field far off \\ within a minute.
const ZIP_DRIFT_DEG = 30;
const ZIP_HOLD_S = ZIP_HAND_LAG_S + ZIP_GAP_COLS * ZIP_COL_LAG_S;
const ZIP_CYCLE_S = ZIP_FLIP_S + ZIP_HOLD_S;

// How far through its 90 deg swing one hand is at time x, 0..1.
//
// It SWINGS OUT AND BACK rather than accumulating: pass 0 goes 0 -> 1, pass 1
// goes 1 -> 0, and so on. Accumulating instead - each pass carrying on round -
// leaves every pass starting from a base 90 deg further along, so the second
// pass opens its chevrons downwards (v and ^) instead of sideways. Swinging
// keeps the figure in the same > < family every time.
//
// Continuous at the seam: an even pass ends at 1 and the next odd pass starts
// at 1 - 0 = 1, so the hand holds rather than steps.
function zipSwing(x) {
  // Nothing has happened yet. Without this the columns to the right - whose
  // lag puts them at negative x - would already be part-way through a swing at
  // t = 0, and the wall would not start as one clean field of diagonals for
  // the mode-entry blend to fade into.
  if (x <= 0) return 0;
  const n = Math.floor(x / ZIP_CYCLE_S);
  const u = x - n * ZIP_CYCLE_S;
  const p = u < ZIP_FLIP_S ? ease(u / ZIP_FLIP_S) : 1;
  return (n & 1) ? 1 - p : p;
}

// The slow lean of a resting column, in degrees.
//
// Holds still while the column is mid-swing, then ramps across the rest that
// follows - to ZIP_DRIFT_DEG after an even pass (resting on /), back to 0
// after an odd one (resting on \\). Continuous at every seam, and it never
// leaves [0, ZIP_DRIFT_DEG].
function zipDrift(x) {
  if (x <= 0) return 0;
  const n = Math.floor(x / ZIP_CYCLE_S);
  const u = x - n * ZIP_CYCLE_S;
  const from = (n & 1) ? ZIP_DRIFT_DEG : 0;       // where this cycle starts
  const to = (n & 1) ? 0 : ZIP_DRIFT_DEG;         // and where its rest takes it
  if (u < ZIP_FLIP_S) return from;                // mid-swing: hold
  return from + (to - from) * ((u - ZIP_FLIP_S) / ZIP_HOLD_S);
}

function tickZipper(cur, t, { modeSpeed }) {
  const ts = t * modeSpeed;
  for (let col = 0; col < WALL_COLS; col++) {
    const x = ts - col * ZIP_COL_LAG_S;           // the front, travelling right
    // WHICH hand leads is what makes the figure: whichever goes first drags the
    // stroke open towards its own side. Alternating it by column gives the
    // front a row of mirrored chevrons - > < > < - rather than eight identical
    // ones.
    const lead = (col & 1) === 0;
    const x0 = lead ? x : x - ZIP_HAND_LAG_S;
    const x1 = lead ? x - ZIP_HAND_LAG_S : x;
    // The drift is read at the column's own x and added to BOTH hands, so a
    // resting stroke leans without bending.
    const drift = zipDrift(x);
    const a0 = wrap360(315 + 90 * zipSwing(x0) + drift);
    const a1 = wrap360(135 + 90 * zipSwing(x1) + drift);
    // Every row does the same thing, so the figure repeats down each column and
    // the front is one vertical band crossing the wall.
    //
    // Two variations were tried and are wrong: giving the middle row the
    // opposite travel direction puts the rows at different columns, so the wall
    // stops reading as a single front; giving it the opposite swing sign turns
    // its chevrons through a right angle, which breaks the column into three
    // unrelated marks.
    for (let row = 0; row < WALL_ROWS; row++)
      cellAt(cur, row, col, a0, a1);
  }
}

// -------------------------------------------------------- mirror_wave ------
// Every clock rests as one vertical stroke - both hands on the 12-6 line - and
// then scissors open, the two hands parting like a pair of dividers.
//
// The wall is MIRRORED about its centre line: the left half opens to the right
// and the right half opens to the left, so the chevrons always point inwards at
// the middle and the two halves are reflections of each other at every instant.
//
// The middle row opens the OTHER WAY from the two around it, so a column is
// three alternating chevrons rather than three identical ones.
//
// It starts in the MIDDLE and spreads OUTWARDS. The order is:
//
//   1. the two centre columns (3 and 4), middle row
//   2. their top and bottom rows, MIRROR_ROW_LAG_DEG behind
//   3. the next ring out (2 and 5), then (1 and 6), then (0 and 7), each
//      MIRROR_COL_LAG_DEG behind the ring inside it
//
// Within a row every clock turns at the same rate, so the offsets picked up on
// the way out are fixed: a standing fan, widest in the middle, never bunching
// and never overtaking. ACROSS rows it is not fixed - the top and bottom run
// slower than the middle (MIRROR_OUTER_RATE), so they drift steadily further
// behind and the three rows beat against one another.
//
// 180 deg of travel returns both hands to the vertical - hand 0 lands where
// hand 1 was - so the figure closes back up and repeats without a seam.
const MIRROR_TURN_S = 9.0;         // 180 deg of hand travel: one open-close
const MIRROR_COL_LAG_DEG = 12.0;   // between one ring and the next one out
const MIRROR_ROW_LAG_DEG = 7.0;   // extra for the top and bottom rows
const MIRROR_RAMP_S = 1.0;         // spin-up, per clock
// The top and bottom rows turn slower than the middle one. They therefore fall
// steadily further behind rather than holding a fixed offset - the rows beat
// against each other, coming back into step every MIRROR_TURN_S / (1 - rate)
// seconds, 36 s at these values.
const MIRROR_OUTER_RATE = 0.75;    // rows 0 and 2, as a fraction of the middle

function tickMirrorWave(cur, t, { modeSpeed }) {
  const rate = 180 / MIRROR_TURN_S;               // deg/s once up to speed
  // Lags are written in DEGREES and converted, so the look is tuned by the
  // angle you want between neighbours rather than by a delay that has to be
  // re-derived whenever the rate changes.
  const colDelay = MIRROR_COL_LAG_DEG / rate;
  const rowDelay = MIRROR_ROW_LAG_DEG / rate;
  const ts = t * modeSpeed;
  const mid = (WALL_COLS - 1) / 2;                // 3.5 on an 8-wide wall

  for (let c = 0; c < NUM_CLOCKS; c++) {
    const { col, row } = wallPos(c);
    // Rings out from the centre: columns 3 and 4 are ring 0, 0 and 7 ring 3.
    const ring = Math.abs(col - mid) - 0.5;
    const x = ts - ring * colDelay - (row === 1 ? 0 : rowDelay);

    // Rows 0 and 2 run slower than row 1 - see MIRROR_OUTER_RATE.
    const r = (row === 1) ? rate : rate * MIRROR_OUTER_RATE;
    let turned;
    if (x <= 0) turned = 0;                                  // not away yet
    else if (x < MIRROR_RAMP_S) turned = r * x * x / (2 * MIRROR_RAMP_S);
    else turned = r * (x - MIRROR_RAMP_S / 2);               // at speed

    // Left half opens right, right half opens left - the mirror. The MIDDLE ROW
    // then scissors the other way from the two around it, so a column reads as
    // three chevrons that alternate rather than three doing the same thing, and
    // the rows shear against each other as they open.
    const sign = (col <= mid ? 1 : -1) * (row === 1 ? -1 : 1);
    cur[c * 2 + 0] = wrap360(0 + sign * turned);
    cur[c * 2 + 1] = wrap360(180 - sign * turned);
  }
}

// -------------------------------------------------------------- test -------
// SANDBOX ONLY - the scratch bench, empty again now that zipper has its own
// name. Nothing depends on it, so overwrite the body freely; when something in
// here is worth keeping, copy it out under its own name and leave this back at
// the neutral pose.
//
// Neutral pose: every clock at 12 and 6, the whole wall uniform and still.
function tickTest(cur) {
  for (let c = 0; c < NUM_CLOCKS; c++) {
    cur[c * 2 + 0] = 0;
    cur[c * 2 + 1] = 180;
  }
}

function tickTime(cur, t, { digits }) {
  for (let d = 0; d < NUM_DIGITS; d++) {
    const val = digits[d];
    const blank = val < 0 || val > 9;
    for (let c = 0; c < CLOCKS_PER_DIGIT; c++)
      for (let h = 0; h < 2; h++)
        cur[(d * CLOCKS_PER_DIGIT + c) * 2 + h] = blank ? PARK : FONT[val][c][h];
  }
}

// A trailing * marks a mode that exists HERE ONLY and is not in the firmware.
// Drop the marker when a mode is ported, or the sandbox starts lying about what
// the wall can actually do.
const MODES = {
  time:        { fn: tickTime,   label: "time",        pose: true  },
  rotate_left: { fn: tickRotate, label: "rotate_left"              },
  birds:       { fn: tickBirds,  label: "birds"                    },
  wave:        { fn: tickWave,   label: "wave"                     },
  spiral:      { fn: tickSpiral, label: "spiral"                   },
  wind:        { fn: tickWind,   label: "wind"                     },
  love:        { fn: tickLove,   label: "love",        pose: true  },
  temp:        { fn: tickTemp,   label: "temp",        pose: true  },
  rotating_maze: { fn: tickRotatingMaze, label: "rotating_maze"      },
  zipper:      { fn: tickZipper, label: "zipper"                   },
  mirror_wave: { fn: tickMirrorWave, label: "mirror_wave"          },
  test:        { fn: tickTest,   label: "test *",      pose: true  },
};

// The firmware's mode names are the WIRE FORMAT and do not all match these
// keys: what it broadcasts as `flying_birds` is `birds` here. Anything reading
// a mode name off a real wall - the Lovelace card, the tablet view - has to go
// through this, or it hands Wall a key that does not exist.
//
// An alias table rather than extra MODES entries: the sandbox builds its mode
// buttons from Object.keys(MODES), and an alias in there is a duplicate button.
const MODE_ALIASES = { flying_birds: "birds" };
const resolveMode = (name) =>
  MODES[name] ? name : (MODE_ALIASES[name] || name);

// Everything the page needs, in one namespace.
window.CC = {
  NUM_DIGITS, CLOCKS_PER_DIGIT, NUM_CLOCKS, NUM_HANDS, WALL_COLS, WALL_ROWS,
  PARK, FONT, LOVE, TEMP_GLYPHS, TG_DEGREE, TG_C, TG_MINUS, TG_BLANK,
  wrap360, shortestDelta, ease, easeOut, wallPos, K, MODES,
  MODE_ALIASES, resolveMode,
  tickRotate, tickBirds, tickWave, tickSpiral, tickWind, tickLove, tickTemp, tickTime,
  tickRotatingMaze, tickZipper, tickMirrorWave, tickTest, cellAt, mazePhase,
};
