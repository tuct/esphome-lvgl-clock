#include "lvgl_clock.h"
#include "esphome/core/log.h"
#include <sys/time.h>
#ifdef USE_ESP32
#include <esp_heap_caps.h>
#endif
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace esphome {
namespace lvgl_clock {

static const char *const TAG = "lvgl_clock";

static const float PI_F = 3.14159265358979323846f;
static const float PARK = 225.0f;  // clockclock idle: both hands to bottom-left
// Added to `hand_width` for clockclock24 only. Its hands are drawn with square
// ends, so they need more body than a rounded one to read as the flat bars the
// real thing uses.
static const int CC_HAND_EXTRA_PX = 4;
// Hand width as a fraction of a mini-clock's radius, used when one clock is
// blown up to fill a whole panel (`partial:`). At that size a fixed pixel
// width is a thread, so the width has to scale - and this divisor, not
// CC_HAND_EXTRA_PX, is what actually sets it there.
static const int CC_HAND_RADIUS_DIV = 8;

// clockclock24 font: target angle (deg, 0 = 12 o'clock, clockwise) of the two
// hands of each of a digit's 6 clocks (order TL, TR, ML, MR, BL, BR).
static const float FONT[10][CLOCKS_PER_DIGIT][2] = {
    {{90, 180}, {180, 270}, {0, 180}, {0, 180}, {0, 90}, {0, 270}},           // 0
    {{PARK, PARK}, {180, 180}, {PARK, PARK}, {0, 180}, {PARK, PARK}, {0, 0}},  // 1
    {{90, 90}, {180, 270}, {90, 180}, {0, 270}, {0, 90}, {270, 270}},         // 2
    {{90, 90}, {180, 270}, {90, 90}, {0, 270}, {90, 90}, {0, 270}},           // 3
    {{180, 180}, {180, 180}, {0, 90}, {0, 180}, {PARK, PARK}, {0, 0}},        // 4
    {{90, 180}, {270, 270}, {0, 90}, {180, 270}, {90, 90}, {0, 270}},         // 5
    {{90, 180}, {270, 270}, {0, 180}, {180, 270}, {0, 90}, {0, 270}},         // 6
    {{90, 90}, {180, 270}, {PARK, PARK}, {0, 180}, {PARK, PARK}, {0, 0}},     // 7
    {{90, 180}, {180, 270}, {0, 90}, {0, 270}, {0, 90}, {0, 270}},            // 8 (two-box)
    {{90, 180}, {180, 270}, {0, 90}, {0, 180}, {90, 90}, {0, 270}},           // 9
};

// Wraps into [0,360).
// LOVE, one letter per digit position, same 2-wide x 3-tall block and the same
// {hand0, hand1} angles as FONT above (0 = 12 o'clock, clockwise; PARK on both
// hands = an unlit cell). Strokes meet at the cell edges, so a hand pointing at
// 180 in one row joins the 0 of the row below.
//
// O is exactly the digit 0. V is two vertical strokes that turn inward on the
// bottom row and meet at the block's bottom centre. Those arms are 105/255:
// a flat-bottomed U would be 90/270, so this is 15 deg BELOW horizontal - just
// enough dip to read as a V rather than a U, without the point dropping far
// below the other letters' baseline the way a 45 deg diagonal does - a 45 deg diagonal in every
// cell instead reads as three stacked chevrons, not a letter.
//
// E's middle row is {0,180} - a pure spine - so the spine runs unbroken from
// top to bottom. A mini-clock has two hands, and the middle-left cell of an E
// wants three directions (up, down and right); something has to give. Giving up
// the right hand costs the left half of the middle bar, which just makes the
// middle arm shorter than the other two - normal for the letter. Giving up the
// down hand instead leaves a visible gap in the spine, which is not.
//
// The top row of every letter points DOWN only ({180,180} where the stroke is
// just a stem). An upward hand there would push L and V a half-cell taller than
// O and E, whose top rows start at the row-0 pivot - so the four letters would
// not share a cap height.
static const float LOVE[NUM_DIGITS][CLOCKS_PER_DIGIT][2] = {
    {{180, 180}, {PARK, PARK}, {0, 180}, {PARK, PARK}, {0, 90}, {270, 270}}, // L
    {{90, 180}, {180, 270}, {0, 180}, {0, 180}, {0, 90}, {0, 270}},          // O
    {{180, 180}, {180, 180}, {0, 180}, {0, 180}, {0, 105}, {0, 255}},        // V
    {{90, 180}, {270, 270}, {0, 180}, {270, 270}, {0, 90}, {270, 270}},      // E
};

// Extra glyphs for `temp`, in the same {hand0, hand1} form as FONT: a degree
// sign, a C, a minus, and a blank. The degree sits in the top two rows only,
// which is what makes it read as a raised ring rather than another O.
enum TempGlyph { TG_DEGREE = 0, TG_C, TG_MINUS, TG_BLANK };
static const float TEMP_GLYPHS[4][CLOCKS_PER_DIGIT][2] = {
    // degree: a closed ring in the upper two thirds
    {{90, 180}, {180, 270}, {0, 90}, {0, 270}, {PARK, PARK}, {PARK, PARK}},
    // c: a SMALL c, raised to sit with the degree sign - top two rows only,
    // bottom row unlit. A full-height C next to a raised ring reads as two
    // unrelated glyphs rather than one "degrees C".
    {{90, 180}, {270, 270}, {0, 90}, {270, 270}, {PARK, PARK}, {PARK, PARK}},
    // minus: one bar on the middle row
    {{PARK, PARK}, {PARK, PARK}, {90, 90}, {270, 270}, {PARK, PARK}, {PARK, PARK}},
    // blank
    {{PARK, PARK}, {PARK, PARK}, {PARK, PARK}, {PARK, PARK}, {PARK, PARK}, {PARK, PARK}},
};

static inline float wrap360(float a) { return fmodf(fmodf(a, 360.0f) + 360.0f, 360.0f); }

// Signed shortest way round from `a` to `b`, in (-180, 180].
static inline float shortest_delta(float a, float b) {
  float d = fmodf(b - a, 360.0f);
  if (d < 0)
    d += 360.0f;
  if (d > 180.0f)
    d -= 360.0f;
  return d;
}

static inline float ease(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

// Decelerate-only: full speed at t=0, stopped at t=1.
//
// For LEAVING a choreography. The choreographies are constant-rate rotations,
// so the hands are already moving when the settle begins; smootherstep starts
// at zero velocity, which makes them stop dead and then creep off - it reads as
// a pause before the clock comes back. This picks up where the animation left
// off and brakes into position instead.
static inline float ease_out(float t) {
  float u = 1.0f - t;
  return 1.0f - u * u * u;
}

static void draw_event_cb(lv_event_t *e) {
  static_cast<LvglClock *>(lv_event_get_user_data(e))->draw_direct_(e);
}

void *alloc_canvas_buf(size_t size) {
#ifdef USE_ESP32
  size = LV_ROUND_UP(size, LV_DRAW_BUF_ALIGN);
  void *buf = heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (buf != nullptr) {
    ESP_LOGD(TAG, "Canvas: %u bytes in internal RAM", (unsigned) size);
    return buf;
  }
  buf = heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (buf != nullptr) {
    ESP_LOGW(TAG,
             "Canvas: %u bytes in PSRAM - it did not fit in internal RAM, so drawing and "
             "flushing will be markedly slower. Shrink the widget or set grayscale.",
             (unsigned) size);
  }
  return buf;
#else
  return lv_malloc_core(size);
#endif
}

void LvglClock::setup() {
  for (int i = 0; i < NUM_HANDS; i++) {
    this->cur_[i] = PARK;
    this->start_[i] = PARK;
    this->target_[i] = PARK;
  }
  this->animating_ = false;
  this->last_key_ = -1;
}

bool LvglClock::now_hms_(int &hh, int &mm, int &ss) {
  if (this->time_ == nullptr)
    return false;
  ESPTime t = this->time_->now();
  if (!t.is_valid())
    return false;
  hh = t.hour;
  mm = t.minute;
  ss = t.second;
  this->pm_ = hh >= 12;  // captured before the 12h conversion loses it
  if (!this->h24_) {
    hh = hh % 12;
    if (hh == 0)
      hh = 12;
  }
  return true;
}

void LvglClock::now_or_fake_hms_(int &hh, int &mm, int &ss) {
  if (this->now_hms_(hh, mm, ss))
    return;
  // No valid time yet - show 00:15 (a nice pose: analog hands aren't stacked
  // on top of each other) with the seconds running off uptime, so every
  // style looks alive while waiting to sync.
  hh = this->h24_ ? 0 : 12;
  mm = 15;
  ss = (int) ((millis() / 1000) % 60);
  this->pm_ = false;
}

// ---------------------------------------------------------------------------
// clockclock24 animation engine
// ---------------------------------------------------------------------------
void LvglClock::set_time_(int hh, int mm) {
  int lead = hh / 10;
  if (lead == 0 && !this->h24_)
    lead = -1;  // blank leading zero in 12h mode
  this->digits_[0] = lead;
  this->digits_[1] = hh % 10;
  this->digits_[2] = mm / 10;
  this->digits_[3] = mm % 10;
}

void LvglClock::retarget_() {
  for (int d = 0; d < NUM_DIGITS; d++) {
    int val = this->digits_[d];
    bool blank = (val < 0 || val > 9);
    for (int c = 0; c < CLOCKS_PER_DIGIT; c++) {
      for (int h = 0; h < 2; h++) {
        int hi = (d * CLOCKS_PER_DIGIT + c) * 2 + h;
        float goal = blank ? PARK : FONT[val][c][h];
        float base = fmodf(this->cur_[hi], 360.0f);
        if (base < 0)
          base += 360.0f;
        this->start_[hi] = base;
        float cw = fmodf(goal - base, 360.0f);
        if (cw < 0)
          cw += 360.0f;
        float ccw = cw - 360.0f;
        float delta;
        if (cw < 0.001f) {
          delta = 0.0f;
        } else if (this->settle_from_ != CC_MODE_TIME) {
          // Settling back out of a choreography: always take the SHORTEST way
          // round, whatever `movement:` says. That option is about the drama of
          // a digit change, where every hand starts from rest and the distance
          // is chosen for effect. Here the hands are already moving and the
          // sweep has to look like the animation winding down - and `long`
          // would send them up to 360 deg in transition_length, roughly four
          // times the speed the choreography was running at.
          delta = (cw <= 180.0f) ? cw : ccw;
        } else {
          switch (this->movement_) {
            case CC_MOVE_CLOCKWISE:
              delta = cw;
              break;
            case CC_MOVE_COUNTER:
              delta = ccw;
              break;
            case CC_MOVE_LONG:
              delta = (cw >= 180.0f) ? cw : ccw;
              break;
            case CC_MOVE_OPPOSITE:
            default:
              delta = (h == 0) ? cw : ccw;
              break;
          }
        }
        this->target_[hi] = base + delta;
      }
    }
  }
  this->anim_start_ = millis();
  this->animating_ = true;
  // Leaving a choreography: seed the live settle from what we just worked out.
  // start_ is where the hands are now, target_ where they have to end up.
  if (this->settle_from_ != CC_MODE_TIME) {
    for (int i = 0; i < NUM_HANDS; i++) {
      this->settle_prev_[i] = this->start_[i];
      this->settle_delta_[i] = this->target_[i] - this->start_[i];
    }
  }
  this->render_us_total_ = 0;
  this->render_us_max_ = 0;
  this->render_frames_ = 0;

  // Logged with the node's own wall clock to the millisecond, not the host's
  // receive time: on a multi-node build (see `partial:`) the whole point is to
  // compare this line across nodes and see how far apart they start the same
  // sweep. Under ~50 ms of skew is invisible on the wall.
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  ESPTime t = ESPTime::from_epoch_local((time_t) tv.tv_sec);
  char digits[8];
  for (int d = 0; d < NUM_DIGITS; d++)
    digits[d] = (this->digits_[d] >= 0 && this->digits_[d] <= 9)
                    ? (char) ('0' + this->digits_[d])
                    : '-';
  digits[NUM_DIGITS] = '\0';
  ESP_LOGI(TAG, "Animation start -> %c%c:%c%c at %02d:%02d:%02d.%03u (uptime %u ms)", digits[0],
           digits[1], digits[2], digits[3], t.hour, t.minute, t.second,
           (unsigned) (tv.tv_usec / 1000), (unsigned) this->anim_start_);
}

void LvglClock::tick_choreography_(ClockMode m, double t) {
  switch (m) {
    case CC_MODE_ROTATE_LEFT:
      this->tick_rotate_(t);
      break;
    case CC_MODE_FLYING_BIRDS:
      this->tick_birds_(t);
      break;
    case CC_MODE_WAVE:
      this->tick_wave_(t);
      break;
    case CC_MODE_SPIRAL:
      this->tick_spiral_(t);
      break;
    case CC_MODE_WIND:
      this->tick_wind_(t);
      break;
    case CC_MODE_LOVE:
      this->tick_love_(t);
      break;
    case CC_MODE_TEMP:
      this->tick_temp_(t);
      break;
    default:
      break;
  }
}

// One frame of the settle, with cur_[] already holding the live choreography.
//
// Each hand is drawn at `animation + delta`, where delta is whatever still
// separates it from its target and is faded out to zero. delta is not
// recomputed from scratch each frame - it is carried forward and reduced by
// exactly the animation's own movement, so `animation + delta` stays pinned on
// the target throughout and the direction can never flip mid-fade.
bool LvglClock::settle_blend_() {
  uint32_t elapsed = millis() - this->anim_start_;
  bool all_done = true;

  for (int i = 0; i < NUM_HANDS; i++) {
    float anim = this->cur_[i];
    // Follow the animation. Its per-frame step is small, so the shortest way
    // round is never ambiguous.
    this->settle_delta_[i] -= shortest_delta(this->settle_prev_[i], anim);
    this->settle_prev_[i] = anim;

    int col, row;
    wall_pos_(i / 2, col, row);
    uint32_t delay = (uint32_t) col * this->anim_stagger_ms_;

    float w;  // 0 = still purely the animation, 1 = arrived on the time
    if (this->transition_ms_ == 0) {
      w = 1.0f;
    } else if (elapsed <= delay) {
      w = 0.0f;
      all_done = false;
    } else {
      float t = (elapsed - delay) / (float) this->transition_ms_;
      if (t >= 1.0f) {
        w = 1.0f;
      } else {
        w = ease_out(t);
        all_done = false;
      }
    }
    this->cur_[i] = wrap360(anim + this->settle_delta_[i] * w);
  }

  this->cc_dirty_ = true;
  return all_done;
}

void LvglClock::advance_animation_() {
  if (!this->animating_)
    return;
  // Read a fresh timestamp rather than accepting one from the caller: retarget_()
  // may have just set anim_start_ via its own, later millis() call, and an
  // earlier/stale now_ms here would underflow (anim_start_ > now_ms) and snap
  // the transition to "complete" on the very frame it started.
  uint32_t now_ms = millis();
  uint32_t elapsed = now_ms - this->anim_start_;
  // anim_stagger_ms_ is 0 for an ordinary digit change, so every hand shares
  // one `t` and this is the plain simultaneous sweep. It is non-zero only when
  // settling back out of a choreography, where each wall column starts that
  // much later than the one to its left - so the wall returns to the time in
  // the same left-to-right order the choreographies cross it in.
  bool all_done = true;
  for (int i = 0; i < NUM_HANDS; i++) {
    uint32_t delay = 0;
    if (this->anim_stagger_ms_ != 0) {
      int col, row;
      wall_pos_(i / 2, col, row);
      delay = (uint32_t) col * this->anim_stagger_ms_;
    }
    float t;
    if (this->transition_ms_ == 0) {
      t = 1.0f;
    } else if (elapsed <= delay) {
      t = 0.0f;  // this column has not started moving yet
    } else {
      t = (elapsed - delay) / (float) this->transition_ms_;
    }
    if (t >= 1.0f) {
      t = 1.0f;
    } else {
      all_done = false;
    }
    // Braking out of a choreography (staggered) vs an ordinary digit change
    // (simultaneous, and starting from rest) need different curves - see
    // ease_out().
    float e = (this->anim_stagger_ms_ != 0) ? ease_out(t) : ease(t);
    this->cur_[i] = this->start_[i] + (this->target_[i] - this->start_[i]) * e;
  }

  if (all_done) {
    for (int i = 0; i < NUM_HANDS; i++) {
      float f = fmodf(this->target_[i], 360.0f);
      this->cur_[i] = (f < 0) ? f + 360.0f : f;
    }
    this->animating_ = false;
    this->anim_stagger_ms_ = 0;  // one-shot: the next digit change is uniform
    if (this->render_frames_ > 0) {
      ESP_LOGI(TAG, "Animation done in %u ms: %u frames, draw avg %.1f ms, max %.1f ms",
               (unsigned) (millis() - this->anim_start_), (unsigned) this->render_frames_,
               this->render_us_total_ / 1000.0f / this->render_frames_,
               this->render_us_max_ / 1000.0f);
    }
  }
  // The hands moved, so the canvas is worth redrawing - see cc_dirty_.
  this->cc_dirty_ = true;
}

void LvglClock::tick_time_(uint32_t now_ms) {
  int hh, mm, ss;
  if (this->now_hms_(hh, mm, ss)) {
    int key = hh * 100 + mm;
    if (key != this->last_key_) {
      this->last_key_ = key;
      this->set_time_(hh, mm);
      this->retarget_();
    }
  }
  this->advance_animation_();
}

// Fastest the animation clock may run away from real time while correcting,
// as a fraction. 2% is about a fifth of a degree per second on the fastest
// animation here - nothing an eye can pick up, and it still closes a 30 ms
// error in under two seconds.
static const double ANIM_SLEW_MAX = 0.02;
// Above this the phase is not worth preserving - first sync, or a clock that
// was wrong by minutes - so take it as a jump and be done.
static const double ANIM_STEP_THRESHOLD = 2.0;

double LvglClock::anim_clock_() {
  uint32_t now = millis();
  // Unsigned arithmetic, so this stays correct across the millis() wrap.
  uint32_t dt_ms = now - this->anim_last_ms_;
  this->anim_last_ms_ = now;
  if (dt_ms > 1000)
    dt_ms = 0;  // first call, or a long stall - don't lurch forward
  double dt = dt_ms / 1000.0;
  this->anim_t_ += dt;

  struct timeval tv;
  gettimeofday(&tv, nullptr);
  if ((uint32_t) tv.tv_sec < 1546300800u)
    return this->anim_t_;  // no synced clock yet: free-run, nothing to align to

  // Where the shared clock says we should be. Full epoch seconds, not seconds
  // since midnight: a double carries 1.8e9 s at sub-microsecond resolution, so
  // there is no wrap anywhere in the chain and therefore no daily seam in any
  // animation whose period happens not to divide a day.
  double wall = (double) tv.tv_sec + tv.tv_usec * 1e-6;
  double err = wall - this->anim_t_;

  if (!this->anim_started_ || fabs(err) > ANIM_STEP_THRESHOLD) {
    this->anim_started_ = true;
    this->anim_t_ += err;  // jump: there is no phase worth keeping
  } else {
    // Slew: chase the error, but never at more than ANIM_SLEW_MAX of real time.
    double corr = err * 0.5;
    double cap = dt * ANIM_SLEW_MAX;
    if (corr > cap)
      corr = cap;
    else if (corr < -cap)
      corr = -cap;
    this->anim_t_ += corr;
  }
  return this->anim_t_;
}

void LvglClock::wall_pos_(int c, int &col, int &row) {
  int digit = c / CLOCKS_PER_DIGIT;
  int cell = c % CLOCKS_PER_DIGIT;
  row = cell / 2;
  col = digit * 2 + (cell % 2);
}



// How much later each wall column starts when the clock moves between the time
// and a choreography - so column 7 begins 7x this after column 0. Used in both
// directions: fading INTO a choreography and settling back out of one.
//
// Big enough to actually read as a left-to-right sweep: at 250 ms the spread is
// 1.75 s against a 4 s transition, so you clearly see the left go first and the
// right finish last. Much smaller and the columns overlap so heavily that the
// wall just looks like it moves at once.
//
// The sweep itself still takes transition_length; this only offsets its start,
// so the whole move takes transition_length + 7x this.
static const uint32_t COLUMN_STAGGER_MS = 250;

// `cycle_modes:` window, fixed rather than configurable. 30 s is long enough
// for any of the choreographies to read as a whole gesture; 10 s past the top
// of the interval keeps it clear of the digit flip at :00. Only the cadence
// (`interval:`) is exposed - the rest is the difference between a clock that
// occasionally does something and a disco.
static const uint32_t CYCLE_WINDOW_S = 35;
static const uint32_t CYCLE_OFFSET_S = 10;

// `wave` timing, in seconds at mode_speed 1.0 - see tick_wave_().
//
// The turn is deliberately unhurried: a real ClockClock moves its hands
// slowly, and the eased profile peaks at 1.875x the average, so a short turn
// looks much faster than its average rate suggests. The pause is only long
// enough to read as a held position before the next sweep starts.
// `flying_birds` timing, at mode_speed 1.0 - see tick_birds_().
//
// The flap rate was 2.0 rad/s, which drove the wing tips at 90 deg/s - fast
// enough to look frantic on a wall. Halved.
static const double BIRDS_RATE_RAD_S = 2.0 * 0.5;
static const double BIRDS_COL_LAG_S = 0.15;  // take-off lag, column to column
static const double BIRDS_RAMP_S = 2.5;      // beating up to full speed

// `wave` timing. A CONSTANT-RATE, never-ending rotation: no rest pose, no
// easing, and neighbouring columns held a fixed number of degrees apart.
//
// Constant rate is the requirement, not a simplification. Ease a bounded turn
// and the gap between two staggered columns breathes - the leader accelerates
// away, then stops dead while the follower closes on it. Only a uniform rate
// keeps the spacing and the speed identical for every column, so nothing ever
// overtakes.
static const double WAVE_TURN_S = 15.3;       // one revolution (was 10.7, i.e. 30% slower again)
// Steady-state angle between a column and its left neighbour. Expressed in
// DEGREES, not as a delay, because the angle is the thing you actually look
// at; the start delay is derived from it below.
//
// 15 deg spreads 105 deg across the eight columns, which is what the real
// ClockClock does: the left edge stands near vertical while the right has gone
// past horizontal. Measured off a photo of one, the per-column step is ~13-15
// deg and every row in a column sits at the SAME angle - the gradient runs
// across the wall, not down it, which is why the lag is per column only.
static const float WAVE_COL_LAG_DEG = 15.0f;
static const float WAVE_REST_DEG = 315.0f;  // 10:30, i.e. the 10:30-4:30 line
static const double WAVE_RAMP_S = 1.0;      // spin-up, per clock

// `spiral` timing. Every clock STARTS from the same pose, and the start rolls
// out along the bottom-left to top-right diagonal; after that each one turns
// forever at the same constant rate, so the offsets they end up with are fixed
// and nothing overtakes. SPIRAL_RAMP_S eases each clock up to speed rather
// than snapping it into motion.
static const double SPIRAL_TURN_S = 7.5;      // one revolution
// Same convention as WAVE_COL_LAG_DEG: the steady-state angle between the two
// far corners of the diagonal, with the start delay derived from it.
static const float SPIRAL_DIAG_LAG_DEG = 40.0f;  // corner to opposite corner
static const double SPIRAL_RAMP_S = 1.0;      // spin-up, per clock

// `wind` timing, in seconds at mode_speed 1.0 - see tick_wind_().
//
// Deliberately asymmetric: a stalk is pushed over in RISE, stays pressed flat
// for HOLD, and only then eases back over a much longer FALL. HOLD is what
// makes the gust read as a gust - the left of the wall is still bent over
// while the right is only just starting, instead of the left having already
// sprung back by then. SPREAD < RISE + HOLD is the condition for that.
//
// Sums to 15 s, so a whole number of gusts fills the window at both
// mode_speed 1.0 and 0.5.
// How far the two free ends SWING: 10:30 -> 1:30 at the top and 4:30 -> 7:30
// at the bottom. The MIDDLE row never moves.
static const float WIND_BEND_DEG = 90.0f;
// RISE and FALL are floored by a hard constraint: NEIGHBOURING COLUMNS MUST
// NEVER DIFFER BY MORE THAN 15 DEG. A column lags its left neighbour by
// SPREAD/(WALL_COLS-1), and an eased ramp peaks at 1.875x its average rate, so
//     max gap = 1.875 * BEND / RAMP * SPREAD / (WALL_COLS - 1)
// which needs RAMP >= 1.607 * SPREAD to stay under 15 deg. Both ramps satisfy
// it with margin - see the numbers in the README. Shorten either and the front
// turns into a visible step down the wall instead of a bend passing through.
// A gust that pushes the wall over left to right, HOLDS it there, then lets it
// go right to left.
//
// Not a travelling crest. A crest is a moving band, so everything behind it
// springs back as it passes - the left edge would be upright again long before
// the wave reached the right. Here a column stays over once pushed, and only
// releases when the wind lets go, which is what makes the wall look held
// against a gust rather than rippled by one.
//
// Each column has two start times. The push reaches it after `lag`, so the
// left goes first; the release reaches it after the MIRRORED lag, so the right
// goes first. Its hold is whatever sits between them - 2x SPREAD + HOLD at the
// left edge, HOLD at the right - and that difference is the gust crossing and
// draining back.
//
// SPREAD is bounded by the 15 deg-per-column rule: an eased ramp peaks at
// 1.875x its average, so gap = 1.875 * BEND / RAMP * SPREAD / (WALL_COLS - 1),
// which needs RAMP >= 1.6 * SPREAD at 90 deg of swing.
static const double WIND_RISE_S = 2.5;    // a column being pushed over
static const double WIND_HOLD_S = 3.0;    // held there while the gust blows
static const double WIND_FALL_S = 2.5;    // and let go
static const double WIND_SPREAD_S = 1.0;  // one sweep across the wall
static const double WIND_CYCLE_S =
    WIND_RISE_S + 2.0 * WIND_SPREAD_S + WIND_HOLD_S + WIND_FALL_S;

void LvglClock::tick_rotate_(double t) {
  float ang = (float) fmod(t * 45.0 * this->mode_speed_, 360.0);
  float a = 360.0f - ang;
  for (int c = 0; c < NUM_CLOCKS; c++) {
    this->cur_[c * 2 + 0] = a;
    this->cur_[c * 2 + 1] = wrap360(a + 180.0f);
  }
  this->cc_dirty_ = true;
}

// Wings, taking off left to right.
//
// Each column starts its beat BIRDS_COL_LAG_S after the one to its left, so the
// flock lifts off across the wall instead of all at once, and every bird beats
// up to speed over BIRDS_RAMP_S rather than snapping straight into a full flap.
// The ramp is applied to the PHASE (the integral of the rate), which is what
// keeps the wing position continuous while the speed is still changing.
void LvglClock::tick_birds_(double t) {
  const double rate = BIRDS_RATE_RAD_S * this->mode_speed_;  // rad/s, once up to speed
  double ts = t * this->mode_speed_;

  for (int c = 0; c < NUM_CLOCKS; c++) {
    int col, row;
    wall_pos_(c, col, row);
    double x = ts - (double) col * BIRDS_COL_LAG_S;

    double ph;
    if (x <= 0.0) {
      ph = 0.0;  // this column has not lifted off yet
    } else if (x < BIRDS_RAMP_S) {
      ph = rate * x * x / (2.0 * BIRDS_RAMP_S);  // beating up to speed
    } else {
      ph = rate * (x - BIRDS_RAMP_S / 2.0);
    }

    float wing = 85.0f + 45.0f * sinf((float) fmod(ph, 2.0 * M_PI));  // ~40..130 deg
    this->cur_[c * 2 + 0] = wing;
    this->cur_[c * 2 + 1] = 360.0f - wing;
  }
  this->cc_dirty_ = true;
}

// A turn rolling across the wall, left to right, that never stops once it has.
//
// Both hands sit on one line 180 deg apart, so each clock reads as a single
// stroke. Every clock STARTS on the same line - the 10:30-4:30 diagonal - so
// the wall begins uniform; the left column sets off first and the start
// ripples right. From then on every clock turns at the same constant rate
// forever, so the angle each column trails its neighbour by settles at exactly
// WAVE_COL_LAG_DEG and stays there: a fixed fan, nothing overtaking.
void LvglClock::tick_wave_(double t) {
  const double rate = 360.0 / WAVE_TURN_S;  // deg/s, once up to speed
  // Delay per column derived from the angle we want between them, so tuning
  // the look is a single number in degrees.
  const double col_delay = WAVE_COL_LAG_DEG / rate;

  double ts = t * this->mode_speed_;
  for (int c = 0; c < NUM_CLOCKS; c++) {
    int col, row;
    wall_pos_(c, col, row);
    double x = ts - (double) col * col_delay;

    double turned;
    if (x <= 0.0) {
      turned = 0.0;  // this column has not set off yet
    } else if (x < WAVE_RAMP_S) {
      turned = rate * x * x / (2.0 * WAVE_RAMP_S);  // spinning up
    } else {
      turned = rate * (x - WAVE_RAMP_S / 2.0);  // at speed; the fan is now fixed
    }

    float a = wrap360(WAVE_REST_DEG + (float) fmod(turned, 360.0));  // clockwise
    this->cur_[c * 2 + 0] = a;
    this->cur_[c * 2 + 1] = wrap360(a + 180.0f);
  }
  this->cc_dirty_ = true;
}

// A counter-clockwise turn rolling out diagonally, bottom-left to top-right,
// that never stops once it has.
//
// Every clock begins with both hands together on 7:30 - the bottom-left park -
// so the wall STARTS uniform, exactly as it does on the real thing. The
// bottom-left corner sets off first and the start ripples along the diagonal,
// the top-right corner going last. From then on every clock turns at the same
// constant rate forever, so the offsets they picked up during the roll-out are
// fixed: no bunching, no overtaking, and nothing ever stops again.
void LvglClock::tick_spiral_(double t) {
  // Furthest diagonal step: bottom-left (col 0, bottom row) to top-right.
  const float span = (float) ((WALL_COLS - 1) + (WALL_ROWS - 1));
  const double rate = 360.0 / SPIRAL_TURN_S;  // deg/s, once up to speed

  double ts = t * this->mode_speed_;
  for (int c = 0; c < NUM_CLOCKS; c++) {
    int col, row;
    wall_pos_(c, col, row);
    // Row 0 is the top, so the bottom row is the near end of the diagonal.
    float diag = (float) col + (float) ((WALL_ROWS - 1) - row);
    double x = ts - (double) (diag / span) * (SPIRAL_DIAG_LAG_DEG / rate);

    double turned;
    if (x <= 0.0) {
      turned = 0.0;  // this clock has not set off yet - still parked
    } else if (x < SPIRAL_RAMP_S) {
      turned = rate * x * x / (2.0 * SPIRAL_RAMP_S);  // spinning up
    } else {
      turned = rate * (x - SPIRAL_RAMP_S / 2.0);  // at speed; the offset is now fixed
    }

    float a = wrap360(PARK - (float) fmod(turned, 360.0));  // minus = counter-clockwise
    this->cur_[c * 2 + 0] = a;
    this->cur_[c * 2 + 1] = a;
  }
  this->cc_dirty_ = true;
}

// LOVE, held.
//
// Not an animation - it just names the pose. Getting there is the ordinary
// mode-entry blend (staggered left to right) and leaving is the ordinary
// settle, so the letters sweep in and out like any other mode change.
//
// Deliberately only marks the canvas dirty when an angle actually changes:
// once the blend has finished this is a still image, and redrawing a still
// image at the render rate is pure SPI traffic on a board driving three panels.
void LvglClock::tick_love_(double t) {
  (void) t;
  bool changed = false;
  for (int d = 0; d < NUM_DIGITS; d++) {
    for (int c = 0; c < CLOCKS_PER_DIGIT; c++) {
      for (int hnd = 0; hnd < 2; hnd++) {
        int i = (d * CLOCKS_PER_DIGIT + c) * 2 + hnd;
        float a = LOVE[d][c][hnd];
        if (this->cur_[i] != a) {
          this->cur_[i] = a;
          changed = true;
        }
      }
    }
  }
  if (changed)
    this->cc_dirty_ = true;
}

void LvglClock::adopt_temperature(int celsius) {
  if (celsius == this->adopted_temp_)
    return;
  this->adopted_temp_ = celsius;
  if (this->mode_ == CC_MODE_TEMP)
    this->cc_dirty_ = true;  // the face is showing it, so redraw
}

// A local sensor wins over the bus: a node with its own sensor is showing its
// own room, and that is more useful than the master's. Everything else falls
// back to whatever came down the wire.
int LvglClock::temperature_value() const {
#ifdef USE_SENSOR
  if (this->temp_sensor_ != nullptr && this->temp_sensor_->has_state() &&
      !std::isnan(this->temp_sensor_->state))
    return (int) lroundf(this->temp_sensor_->state);
#endif
  return this->adopted_temp_;
}

bool LvglClock::temperature_ready_() const { return this->temperature_value() != TEMP_NONE; }

// A temperature, held: two digits, a degree sign and a C across the four digit
// positions. Like tick_love_() this is a pose rather than an animation, so the
// ordinary mode-entry blend sweeps it in and the settle sweeps it out, and the
// canvas is only marked dirty when a digit actually changes - which for a
// temperature is rarely.
void LvglClock::tick_temp_(double t) {
  (void) t;

  // A new reading while the face is up must not snap the digits from one
  // number to the next - that is the same teleport the choreographies are not
  // allowed. So:
  //   - mid-sweep (a blend is running), the new value waits its turn;
  //   - otherwise it is taken, and the hands SWEEP to it on the ordinary
  //     mode-entry blend, staggered left to right like every other change.
  if (this->blend_state_ == BLEND_NONE) {
    int latest = this->temperature_value();
    if (latest != this->shown_temp_) {
      for (int i = 0; i < NUM_HANDS; i++)
        this->blend_off_[i] = this->cur_[i];  // where the hands are now
      this->shown_temp_ = latest;
      this->blend_state_ = BLEND_PENDING;     // measured on the next frame
    }
  }

  int v = this->shown_temp_;
  bool neg = false;
  if (v == TEMP_NONE)
    v = 0;  // only reachable if driven by an action rather than the cycle
  if (v < 0) {
    neg = true;
    v = -v;
  }
  if (v > 99)
    v = 99;  // two digits is all there is room for

  // [tens or sign] [units] [degree] [C]. Below 10 the first position is blank,
  // or a minus when it is needed - so "-5" and "21" both sit where you expect.
  int glyph[NUM_DIGITS];
  bool is_digit[NUM_DIGITS] = {true, true, false, false};
  if (neg) {
    glyph[0] = TG_MINUS;
    is_digit[0] = false;
    glyph[1] = (v > 9) ? 9 : v;
  } else if (v >= 10) {
    glyph[0] = v / 10;
    glyph[1] = v % 10;
  } else {
    glyph[0] = TG_BLANK;
    is_digit[0] = false;
    glyph[1] = v;
  }
  glyph[2] = TG_DEGREE;
  glyph[3] = TG_C;

  bool changed = false;
  for (int d = 0; d < NUM_DIGITS; d++) {
    const float(*src)[2] = is_digit[d] ? FONT[glyph[d]] : TEMP_GLYPHS[glyph[d]];
    for (int c = 0; c < CLOCKS_PER_DIGIT; c++) {
      for (int hnd = 0; hnd < 2; hnd++) {
        int i = (d * CLOCKS_PER_DIGIT + c) * 2 + hnd;
        if (this->cur_[i] != src[c][hnd]) {
          this->cur_[i] = src[c][hnd];
          changed = true;
        }
      }
    }
  }
  if (changed)
    this->cc_dirty_ = true;
}

// A gust bending a stalk, left to right.
//
// Read a wall COLUMN top to bottom and the three clocks form one continuous
// stroke - it leans in at the top left, runs vertically through the middle,
// and exits bottom right:
//
//     row 0   \            hands 10:30 + 6:00
//              |
//     row 1    |           hands 12:00 + 6:00  (never moves)
//              |
//     row 2    |           hands 12:00 + 4:30
//               \ .        (the trailing '.' only keeps the backslash off the
//                           end of the line - a '//' comment ending in '\' is
//                           a line continuation, and splices the next one in)
//
// Each clock's hands meet its neighbours' at the panel edges, so the join is
// continuous: row 0's 6:00 meets row 1's 12:00, and row 1's 6:00 meets row 2's
// 12:00. Only the two FREE ends move - the tip poking out at the top and the
// one at the bottom - and they shear past each other: the top tip sweeps right
// across the top (10:30 -> 1:30) and the bottom tip left across the bottom
// (4:30 -> 7:30). The middle row is the trunk and stays put.
void LvglClock::tick_wind_(double t) {
  // Rest pose per row: {hand 0, hand 1}, and which hand the wind moves.
  // WALL_ROWS is 3; index is the row.
  static const float REST[3][2] = {{315.0f, 180.0f},   // top:    10:30 + 6:00
                                   {0.0f, 180.0f},     // middle: 12:00 + 6:00
                                   {0.0f, 135.0f}};    // bottom: 12:00 + 4:30
  static const int MOVES[3] = {0, -1, 1};  // which hand the gust bends, -1 = none
  // Both free ends turn CLOCKWISE by the same amount, which is physically
  // opposite: the top tip sweeps right across the top (10:30 -> 1:30) while the
  // bottom tip sweeps left across the bottom (4:30 -> 7:30). The stalk shears.
  static const float SENSE[3] = {1.0f, 0.0f, 1.0f};

  double ts = t * this->mode_speed_;

  for (int c = 0; c < NUM_CLOCKS; c++) {
    int col, row;
    wall_pos_(c, col, row);

    double u = fmod(ts, WIND_CYCLE_S);
    if (u < 0)
      u += WIND_CYCLE_S;

    double lag = (double) col / (WALL_COLS - 1) * WIND_SPREAD_S;
    double push_at = lag;                                                   // left first
    double release_at = WIND_RISE_S + WIND_HOLD_S + 2.0 * WIND_SPREAD_S - lag;  // right first

    float lean;
    if (u < push_at) {
      lean = 0.0f;  // the gust has not reached this column yet
    } else if (u < push_at + WIND_RISE_S) {
      lean = WIND_BEND_DEG * ease((float) ((u - push_at) / WIND_RISE_S));
    } else if (u < release_at) {
      lean = WIND_BEND_DEG;  // held over - the wind is still blowing
    } else if (u < release_at + WIND_FALL_S) {
      lean = WIND_BEND_DEG * (1.0f - ease((float) ((u - release_at) / WIND_FALL_S)));
    } else {
      lean = 0.0f;  // let go, back upright
    }
    lean *= SENSE[row];

    for (int hnd = 0; hnd < 2; hnd++) {
      float a = REST[row][hnd];
      if (MOVES[row] == hnd)
        a += lean;
      this->cur_[c * 2 + hnd] = wrap360(a);
    }
  }
  this->cc_dirty_ = true;
}

// Take the master's fake minute instead of counting our own. Resets the local
// interval timer too, so this node does not immediately tick a second time.
void LvglClock::adopt_demo_min(int m) {
  m = ((m % (24 * 60)) + 24 * 60) % (24 * 60);
  if (m == this->demo_min_)
    return;
  this->demo_min_ = m;
  this->demo_last_ms_ = millis();
}

void LvglClock::tick_demo_(uint32_t now_ms) {
  if (now_ms - this->demo_last_ms_ >= this->demo_interval_ms_) {
    this->demo_last_ms_ = now_ms;
    this->demo_min_ = (this->demo_min_ + this->demo_step_) % (24 * 60);
  }
  int hh = this->demo_min_ / 60;
  int mm = this->demo_min_ % 60;
  if (!this->h24_) {
    hh = hh % 12;
    if (hh == 0)
      hh = 12;
  }
  int key = hh * 100 + mm;
  if (key != this->last_key_) {
    this->last_key_ = key;
    this->set_time_(hh, mm);
    this->retarget_();
  }
  this->advance_animation_();
}

void LvglClock::set_mode(ClockMode m) {
  if (m == this->mode_)
    return;
  // While a random choreography is playing, outside requests are remembered
  // rather than obeyed - otherwise the master's once-a-second `show_time`
  // action (and the same mode arriving over the bus) would chop the animation
  // up. The recorded mode is what we drop back to when the window closes.
  if (this->cycle_active_) {
    this->base_mode_ = m;
    return;
  }
  this->apply_mode_(m);
}

void LvglClock::apply_mode_(ClockMode m) {
  if (m == this->mode_)
    return;
  ClockMode from = this->mode_;
  this->mode_ = m;
  this->cc_dirty_ = true;
  // Leaving a choreography for the clock: settle back column by column, left
  // to right, rather than the whole wall arriving at once. The choreographies
  // cross the wall in that direction, so the return reads as the end of the
  // same gesture instead of a cut. Ordinary digit changes keep the stagger at
  // 0 and stay simultaneous.
  if (m == CC_MODE_TIME && is_idle_animation_(from)) {
    this->anim_stagger_ms_ = COLUMN_STAGGER_MS;
    this->settle_from_ = from;  // keep it running underneath the settle
  }
  if (m == CC_MODE_TEMP)
    this->shown_temp_ = this->temperature_value();
  if (is_idle_animation_(m)) {
    // Restart the choreography's own clock, so it always begins at its rest
    // pose and its first frame is the left edge. Without this, a mode entered
    // by an action takes its phase from the epoch and lands wherever the cycle
    // happens to be - which for `wind` means opening on the fully bent pose
    // and a gust already halfway across.
    //
    // update_mode_cycle_() has already set a window-aligned base by the time it
    // calls us, and that one is deterministic across every node on the wall, so
    // it wins.
    // Start the choreography's clock only once the fade-in has finished. While
    // the fade runs, the tick below is pinned to phase 0 - its rest pose - so
    // the blend has a STATIC target and every hand is in the same position
    // before the first thing moves. Without this lead-in the animation runs
    // during the fade, the blend chases a moving target, and the wall arrives
    // already mid-gesture and out of step.
    if (!this->cycle_active_)
      this->anim_phase_base_ = this->anim_clock_() + this->mode_entry_lead_s_();
    // Stash where every hand actually is. The choreography's first frame turns
    // this into the offset that gets eased away, so the hands sweep into the
    // animation instead of snapping to it. Going the other way needs nothing:
    // time/demo retarget from wherever they find the hands.
    for (int i = 0; i < NUM_HANDS; i++)
      this->blend_off_[i] = this->cur_[i];
    this->blend_state_ = BLEND_PENDING;
    this->animating_ = false;  // a digit sweep in flight is superseded
  } else {
    this->blend_state_ = BLEND_NONE;
  }
  if (m == CC_MODE_TIME) {
    this->last_key_ = -1;
    this->animating_ = false;
  } else if (m == CC_MODE_DEMO) {
    this->last_key_ = -1;
    this->demo_min_ = 0;
    this->demo_last_ms_ = 0;
  }
}


double LvglClock::mode_entry_lead_s_() const {
  return (this->transition_ms_ + (double) (WALL_COLS - 1) * COLUMN_STAGGER_MS) / 1000.0;
}

void LvglClock::blend_into_mode_() {
  if (this->blend_state_ == BLEND_NONE)
    return;

  if (this->blend_state_ == BLEND_PENDING) {
    // cur_[] now holds the choreography's first frame, and blend_off_[] still
    // holds where the hands were. Convert to "how far, and which way round" -
    // measured once, so it cannot flip sign later mid-fade.
    for (int i = 0; i < NUM_HANDS; i++)
      this->blend_off_[i] = shortest_delta(this->cur_[i], this->blend_off_[i]);
    this->blend_start_ = millis();
    this->blend_state_ = BLEND_ACTIVE;
  }

  uint32_t elapsed = millis() - this->blend_start_;
  // Staggered by wall column, exactly like the settle back to the time: the
  // left edge starts moving into the choreography first and the right edge
  // last. The choreographies themselves cross the wall left to right, so the
  // move into one reads as the beginning of that gesture rather than as the
  // whole wall lurching at once.
  bool all_done = true;
  for (int i = 0; i < NUM_HANDS; i++) {
    int col, row;
    wall_pos_(i / 2, col, row);
    uint32_t delay = (uint32_t) col * COLUMN_STAGGER_MS;

    // k is how much of this hand's original offset is still applied: 1 before
    // it starts, 0 once it has arrived on the pure animation.
    float k;
    if (this->transition_ms_ == 0) {
      k = 0.0f;
    } else if (elapsed <= delay) {
      k = 1.0f;  // this column has not been reached yet - hold where it was
      all_done = false;
    } else {
      float t = (elapsed - delay) / (float) this->transition_ms_;
      if (t >= 1.0f) {
        k = 0.0f;
      } else {
        k = 1.0f - ease(t);
        all_done = false;
      }
    }
    if (k != 0.0f)
      this->cur_[i] = wrap360(this->cur_[i] + this->blend_off_[i] * k);
  }

  if (all_done)
    this->blend_state_ = BLEND_NONE;  // cur_[] is now the pure animation
  this->cc_dirty_ = true;
}

void LvglClock::update_mode_cycle_() {
  // A follower never picks - its mode arrives over the bus. One picker per
  // wall is the whole point: two independent choosers is how you get eight
  // boards running eight different choreographies.
  if (this->mode_follower_)
    return;
  if (this->cycle_interval_s_ == 0 || this->cycle_modes_.empty())
    return;

  struct timeval tv;
  gettimeofday(&tv, nullptr);
  // Needs a real clock: the whole scheme is "every node hashes the same
  // interval number", which is only true once they agree what time it is.
  if ((uint32_t) tv.tv_sec < 1546300800u) {
    if (this->cycle_active_) {
      this->cycle_active_ = false;
      this->apply_mode_(this->base_mode_);
    }
    return;
  }

  // Shift the whole grid by CYCLE_OFFSET_S so a window opens that many seconds
  // after the top of the interval, keeping the animation clear of the digit
  // flip at :00.
  uint32_t t = (uint32_t) tv.tv_sec - CYCLE_OFFSET_S;
  uint32_t slot = t / this->cycle_interval_s_;
  // The window has to hold the whole gesture: fade in to the start pose, the
  // choreography itself, and the fade back to the time. So it CLOSES one fade
  // early - otherwise the settle would run on past the window and eat into the
  // clock's own time. What is left in the middle is the animation proper.
  double into = (double) (t % this->cycle_interval_s_);
  bool want = into < (double) CYCLE_WINDOW_S - this->mode_entry_lead_s_();

  if (want) {
    // Walked in order rather than picked at random, so the sequence is what
    // the config says and a repeated entry simply comes round more often.
    // `temp` is stepped over when there is no sensor or it has not published
    // yet - showing a blank face for 35 s would be worse than not showing it.
    const size_t n = this->cycle_modes_.size();
    ClockMode m = this->mode_;
    bool picked = false;
    for (size_t k = 0; k < n && !picked; k++) {
      ClockMode cand = this->cycle_modes_[(slot + k) % n];
      if (cand == CC_MODE_TEMP && !this->temperature_ready_())
        continue;
      m = cand;
      picked = true;
    }
    if (!picked)
      return;  // nothing showable in the list right now - stay on the time
    // Phase the choreography from the window's own start rather than from the
    // epoch, so it opens at its rest position and - because every cycle length
    // divides the window - closes back on one instead of being cut mid-turn.
    this->anim_phase_base_ =
        (double) ((uint64_t) slot * this->cycle_interval_s_ + CYCLE_OFFSET_S) +
        this->mode_entry_lead_s_();
    if (!this->cycle_active_) {
      this->cycle_active_ = true;
      this->base_mode_ = this->mode_;
      ESP_LOGI(TAG, "Choreography: %s for %us (back to %s after)", clock_mode_name(m),
               (unsigned) CYCLE_WINDOW_S, clock_mode_name(this->base_mode_));
    }
    this->apply_mode_(m);
  } else if (this->cycle_active_) {
    this->cycle_active_ = false;
    // anim_phase_base_ is deliberately left alone: the choreography carries on
    // running underneath the settle and only stops once that has finished.
    this->apply_mode_(this->base_mode_);
  }
}

void LvglClock::loop() {
  uint32_t now_ms = millis();
  if (this->style_ == STYLE_CLOCKCLOCK24 && this->startup_align_ms_ > 0 &&
      now_ms < this->startup_align_ms_) {
    // Startup alignment: every hand straight up, nothing else running. When
    // the window closes last_key_ is already -1, so the next tick retargets
    // and the first real sweep starts from 12 on every node at once.
    //
    // Swept, not snapped: the hands start at PARK, and dropping them onto 12 in
    // one frame is the same impossible move as any other teleport. Reuses the
    // mode-entry blend - target every frame, offset eased away over
    // transition_length, then it simply holds at 12.
    if (!this->startup_aligned_) {
      this->startup_aligned_ = true;
      for (int i = 0; i < NUM_HANDS; i++)
        this->blend_off_[i] = this->cur_[i];
      this->blend_state_ = BLEND_PENDING;
      this->animating_ = false;
      this->last_key_ = -1;
      ESP_LOGI(TAG, "Startup alignment: hands to 12 for %u ms",
               (unsigned) this->startup_align_ms_);
    }
    for (int i = 0; i < NUM_HANDS; i++)
      this->cur_[i] = 0.0f;  // 0 deg = 12 o'clock
    this->blend_into_mode_();
    this->cc_dirty_ = true;
  } else if (this->style_ == STYLE_CLOCKCLOCK24) {
    // Decide the mode before drawing it, so a window that opens this frame is
    // honoured this frame rather than one behind.
    this->update_mode_cycle_();
    // Seconds into the current choreography. Negative while the staggered
    // fade-in is still running, and clamped to 0 there so every choreography
    // holds its rest pose until the whole wall has arrived on it.
    double choreo_t = this->anim_clock_() - this->anim_phase_base_;
    if (choreo_t < 0.0)
      choreo_t = 0.0;
    switch (this->mode_) {
      case CC_MODE_ROTATE_LEFT:
        this->tick_rotate_(choreo_t);
        this->blend_into_mode_();
        break;
      case CC_MODE_FLYING_BIRDS:
        this->tick_birds_(choreo_t);
        this->blend_into_mode_();
        break;
      case CC_MODE_WAVE:
        this->tick_wave_(choreo_t);
        this->blend_into_mode_();
        break;
      case CC_MODE_SPIRAL:
        this->tick_spiral_(choreo_t);
        this->blend_into_mode_();
        break;
      case CC_MODE_WIND:
        this->tick_wind_(choreo_t);
        this->blend_into_mode_();
        break;
      case CC_MODE_LOVE:
        this->tick_love_(choreo_t);
        this->blend_into_mode_();
        break;
      case CC_MODE_TEMP:
        this->tick_temp_(choreo_t);
        this->blend_into_mode_();
        break;
      case CC_MODE_DEMO:
        this->tick_demo_(now_ms);
        break;
      case CC_MODE_TIME:
      default:
        if (this->settle_from_ != CC_MODE_TIME && this->animating_) {
          // Winding down out of a choreography: keep it running and fade each
          // hand's remaining distance to the time away on top of it, rather
          // than freezing the animation and sweeping from a still pose.
          this->tick_choreography_(this->settle_from_, choreo_t);
          if (this->settle_blend_()) {
            for (int i = 0; i < NUM_HANDS; i++)
              this->cur_[i] = wrap360(this->target_[i]);
            this->animating_ = false;
            this->anim_stagger_ms_ = 0;
            this->settle_from_ = CC_MODE_TIME;
            this->anim_phase_base_ = 0.0;
          }
        } else {
          this->tick_time_(now_ms);
        }
        break;
    }
  }
  if (this->obj == nullptr || now_ms - this->last_render_ms_ < this->render_interval_ms_)
    return;
  // clockclock24 is static between minute changes - its hands only move during
  // a transition or an idle animation. Redrawing (and re-blitting the whole
  // panel over SPI) 60 times a second regardless is pure waste, and on a
  // single-core C3 pushing 115 KB per frame it is most of the CPU and bus
  // budget. Every writer of cur_[] sets cc_dirty_; skip the frame otherwise.
  // The other styles change every second (or sub-second) anyway, so they keep
  // redrawing unconditionally.
  // The blinking sync dot changes twice a second on its own, so it counts as
  // a reason to redraw even when no hand has moved. It also goes dark for good
  // the moment this node syncs, and sync_dot_on_() folds that in - so the
  // transition to false here is what clears the last dot off the panel.
  if (this->sync_dot_) {
    bool on = this->sync_dot_on_();
    if (on != this->sync_dot_last_) {
      this->sync_dot_last_ = on;
      this->cc_dirty_ = true;
    }
  }
  if (this->style_ == STYLE_CLOCKCLOCK24 && !this->cc_dirty_)
    return;
  this->cc_dirty_ = false;
  this->last_render_ms_ = now_ms;
  if (this->direct_) {
    // The drawing happens in draw_direct_() when LVGL refreshes; all we do is
    // mark the area dirty. Timing is accumulated there.
    if (!this->draw_cb_attached_) {
      this->draw_cb_attached_ = true;
      lv_obj_add_event_cb(this->obj, draw_event_cb, LV_EVENT_DRAW_MAIN, this);
      // LVGL paints the widget background for us; match it to `background`.
      if (this->transparent_) {
        lv_obj_set_style_bg_opa(this->obj, LV_OPA_TRANSP, 0);
      } else {
        lv_obj_set_style_bg_color(
            this->obj, lv_color_make(this->background_.r, this->background_.g, this->background_.b),
            0);
        lv_obj_set_style_bg_opa(this->obj, LV_OPA_COVER, 0);
      }
      lv_obj_set_style_border_width(this->obj, 0, 0);
      lv_obj_set_style_pad_all(this->obj, 0, 0);
      ESP_LOGD(TAG, "Direct draw: no canvas, drawing into LVGL's buffer");
    }
    lv_obj_invalidate(this->obj);
    return;
  }
  uint32_t t0 = micros();
  this->render_();
  uint32_t dt = micros() - t0;
  this->render_us_total_ += dt;
  this->render_frames_++;
  if (dt > this->render_us_max_)
    this->render_us_max_ = dt;
}

// ---------------------------------------------------------------------------
// digital (seven-segment) layout
// ---------------------------------------------------------------------------
float LvglClock::sub_second_(int ss) {
  uint32_t now = millis();
  if (ss != this->last_sec_) {
    this->last_sec_ = ss;
    this->last_sec_ms_ = now;
  }
  float frac = (now - this->last_sec_ms_) / 1000.0f;
  return (frac > 1.0f) ? 1.0f : frac;
}

// segment on/off bitmask per digit (a=1,b=2,c=4,d=8,e=16,f=32,g=64)
static const uint8_t SEG7[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

void LvglClock::seg_rects_(int digit, int dx, int dy, int dw, int dh, int rects[7][4],
                              bool active[7], bool horiz[7]) {
  int t = std::max(2, dw / 6);
  int vh = (dh - 3 * t) / 2;
  if (vh < t)
    vh = t;
  int xr = dx + dw - t;
  int y1 = dy + t + vh, y2 = dy + 2 * t + vh, yd = dy + 2 * t + 2 * vh;
  int seg[7][4] = {  // {isHorizontal, x, y, len} for a,b,c,d,e,f,g
      {1, dx + t, dy, dw - 2 * t}, {0, xr, dy + t, vh},          {0, xr, y2, vh},
      {1, dx + t, yd, dw - 2 * t}, {0, dx, y2, vh},              {0, dx, dy + t, vh},
      {1, dx + t, y1, dw - 2 * t},
  };
  // shrink each segment along its length so the (classic) tapered tips stop
  // short of the corners, leaving a visible dark gap between segments - the
  // thin unlit lines of a real 7-segment display.
  int g = std::max(1, t / 2);
  int mask = (digit >= 0 && digit <= 9) ? SEG7[digit] : (digit == -2 ? (1 << 6) : 0);
  for (int i = 0; i < 7; i++) {
    active[i] = mask & (1 << i);
    int hz = seg[i][0];
    horiz[i] = hz;
    int len = std::max(1, seg[i][3] - 2 * g);
    rects[i][0] = seg[i][1] + (hz ? g : 0);
    rects[i][1] = seg[i][2] + (hz ? 0 : g);
    rects[i][2] = hz ? len : t;  // w
    rects[i][3] = hz ? t : len;  // h
  }
}

int LvglClock::digital_cells_(int w, int h, DigitalCell out[MAX_CELLS]) {
  int x = 1;
  w -= 2;  // small margin so the rounded ends never clip at the edges
  int hh, mm, ss;
  this->now_or_fake_hms_(hh, mm, ss);  // fake 00:15 + uptime seconds pre-sync
  bool colon_on = !(this->digital_blink_ && ((millis() / 1000) & 1));
  int colon_val = colon_on ? 10 : 11;
  // flipclock can drop the divider cells entirely (show_dots: false) - the
  // groups are then only separated by the normal inter-card gap.
  bool dividers = !(this->style_ == STYLE_FLIPCLOCK && !this->flip_show_dots_);
  // flipclock 12h: a dedicated AM/PM card in front (val 12); 7-segment 12h
  // instead reserves a narrow left column drawn by canvas_digital_.
  bool ampm_card = (this->style_ == STYLE_FLIPCLOCK && !this->h24_);
  int vals[MAX_CELLS];
  int nv = 0;
  if (ampm_card)
    vals[nv++] = 12;
  vals[nv++] = (this->digital_blank_leading_ && hh < 10) ? -1 : hh / 10;
  vals[nv++] = hh % 10;
  if (dividers)
    vals[nv++] = colon_val;
  vals[nv++] = mm / 10;
  vals[nv++] = mm % 10;
  if (this->show_seconds_) {
    if (dividers)
      vals[nv++] = colon_val;
    vals[nv++] = ss / 10;
    vals[nv++] = ss % 10;
  }
  const float GAP = 0.18f, COLON_W = 0.45f;
  // 12h 7-segment: reserve a left-hand column for the AM/PM markers (drawn
  // by canvas_digital_), sized like a slightly narrow digit.
  const float AMPM_W = 0.8f;
  bool ampm = (this->style_ == STYLE_DIGITAL && !this->h24_);
  auto uw = [&](int v) { return (v == 10 || v == 11) ? COLON_W : 1.0f; };
  float units = (nv - 1) * GAP;
  for (int i = 0; i < nv; i++)
    units += uw(vals[i]);
  if (ampm)
    units += AMPM_W + GAP;
  float dwf = w / units;                                       // one digit width
  // flip cards are squarer than 7-segment digits (~1.45 vs 1.9 tall)
  float aspect = (this->style_ == STYLE_FLIPCLOCK) ? 1.45f : 1.9f;
  int dh = (int) std::min((float) h, dwf * aspect);
  int dy = (h - dh) / 2;
  float total = (nv - 1) * GAP * dwf;
  for (int i = 0; i < nv; i++)
    total += uw(vals[i]) * dwf;
  float band = ampm ? (AMPM_W + GAP) * dwf : 0.0f;
  total += band;
  float cur = x + (w - total) / 2.0f;
  if (ampm) {
    this->ampm_x_ = (int) lroundf(cur);
    this->ampm_y_ = dy;
    this->ampm_w_ = (int) lroundf(AMPM_W * dwf);
    this->ampm_h_ = dh;
  } else {
    this->ampm_w_ = 0;
  }
  cur += band;
  for (int i = 0; i < nv; i++) {
    float cwf = uw(vals[i]) * dwf;
    out[i] = {vals[i], (int) lroundf(cur), dy, (int) lroundf(cwf), dh};
    cur += cwf + GAP * dwf;
  }
  return nv;
}

int LvglClock::min_width() const {
  switch (this->style_) {
    case STYLE_ANALOG:
      return 24;
    case STYLE_DIGITAL:
    case STYLE_FLIPCLOCK:
      return 24;  // really font/digit-size dependent
    case STYLE_SEG_MATRIX:
      return 24 * 6;  // fixed 24 columns, ~6px per small display
    case STYLE_CLOCKCLOCK24:
    default:
      // One mini-clock on its own display is just a tiny analogue face; a
      // whole digit is two of them side by side.
      if (this->partial_ >= 0)
        return this->partial_digit_ ? 48 : 24;
      return (int) std::ceil(16.0f * (8.0f + this->spacing_));
  }
}
int LvglClock::min_height() const {
  switch (this->style_) {
    case STYLE_ANALOG:
      return 24;
    case STYLE_DIGITAL:
    case STYLE_FLIPCLOCK:
      return 12;
    case STYLE_SEG_MATRIX:
      return 6 * 10;  // fixed 6 rows
    case STYLE_CLOCKCLOCK24:
    default:
      if (this->partial_ >= 0)
        return this->partial_digit_ ? 72 : 24;
      return 48;
  }
}

void LvglClock::dump_config() {
  static const char *const STYLES[] = {"clockclock24", "analog", "digital", "flipclock", "seg_matrix"};
  static const char *const MOVES[] = {"opposite", "clockwise", "counter", "long"};
  ESP_LOGCONFIG(TAG, "LvglClock:");
  ESP_LOGCONFIG(TAG, "  Style: %s", STYLES[this->style_]);
  bool is_clockclock24 = this->style_ == STYLE_CLOCKCLOCK24;
  ESP_LOGCONFIG(TAG, "  24-hour: %s", YESNO(this->h24_));
  if (this->show_seconds_ && is_clockclock24) {
    ESP_LOGCONFIG(TAG, "  Seconds: ignored (clockclock24 has no seconds display)");
  } else {
    ESP_LOGCONFIG(TAG, "  Seconds: %s", YESNO(this->show_seconds_));
  }
  if (is_clockclock24) {
    ESP_LOGCONFIG(TAG, "  Transition: %u ms, movement: %s", (unsigned) this->transition_ms_,
                  MOVES[this->movement_]);
    if (this->partial_ >= 0 && this->partial_digit_) {
      ESP_LOGCONFIG(TAG, "  Partial: digit %d of 4 (clocks %d-%d)", this->partial_,
                    this->partial_ * CLOCKS_PER_DIGIT,
                    this->partial_ * CLOCKS_PER_DIGIT + CLOCKS_PER_DIGIT - 1);
    } else if (this->partial_ >= 0) {
      int cell = this->partial_ % CLOCKS_PER_DIGIT;
      ESP_LOGCONFIG(TAG, "  Partial: clock %d of 24 (digit %d, row %d, col %d)", this->partial_,
                    this->partial_ / CLOCKS_PER_DIGIT, cell / 2, cell % 2);
    }
  }
  ESP_LOGCONFIG(TAG, "  Recommended min: %dx%d px", this->min_width(), this->min_height());
}

// ---------------------------------------------------------------------------
// LVGL canvas rendering (LVGL 9) - `this->obj` is the lv_canvas_t we own
// ---------------------------------------------------------------------------
void LvglClock::fill_bg_() {
  if (this->transparent_) {
    // Clear to fully transparent (needs the ARGB8888 canvas the Python side
    // allocates when `transparent` is set) so widgets behind the clock show
    // through the gaps.
    lv_canvas_fill_bg(this->obj, lv_color_black(), LV_OPA_TRANSP);
  } else {
    lv_canvas_fill_bg(this->obj, lv_color_make(this->background_.r, this->background_.g, this->background_.b),
                      LV_OPA_COVER);
  }
}

void LvglClock::render_() {
  if (this->obj == nullptr)
    return;
  int w = this->canvas_w_;
  int h = this->canvas_h_;
  if (w <= 0 || h <= 0)
    return;
  if (!this->size_checked_) {
    this->size_checked_ = true;
    lv_draw_buf_t *buf = lv_canvas_get_draw_buf(this->obj);
    if (buf == nullptr || buf->data == nullptr) {
      unsigned bpp = this->transparent_ ? 4 : (this->grayscale_ ? 1 : 2);
      ESP_LOGE(TAG,
               "Canvas draw buffer (%dx%d, ~%u bytes) failed to allocate - not enough free RAM. "
               "Reduce width/height, lower lvgl's buffer_size, add PSRAM, or set grayscale (half "
               "the canvas). Disabling rendering.",
               w, h, (unsigned) (w * h * bpp));
      this->render_ok_ = false;
    }
    int mw = this->min_width(), mh = this->min_height();
    if (w < mw || h < mh)
      ESP_LOGW(TAG, "Draw area %dx%d px is below the recommended minimum %dx%d", w, h, mw, mh);
    else
      ESP_LOGI(TAG, "Draw area %dx%d px (min %dx%d) - OK", w, h, mw, mh);
  }
  if (!this->render_ok_)
    return;
  switch (this->style_) {
    case STYLE_ANALOG:
      this->canvas_analog_(w, h);
      break;
    case STYLE_DIGITAL:
      this->canvas_digital_(w, h);
      break;
    case STYLE_FLIPCLOCK:
      this->canvas_flipclock_(w, h);
      break;
    case STYLE_SEG_MATRIX:
      this->canvas_seg_matrix_(w, h);
      break;
    case STYLE_CLOCKCLOCK24:
    default:
      this->canvas_clockclock_(w, h);
      break;
  }
  lv_obj_invalidate(this->obj);
}

void LvglClock::canvas_hand_(lv_layer_t *layer, lv_draw_line_dsc_t *dsc, int cx, int cy,
                                int len, float angle_deg, int start_len) {
  float rad = angle_deg * PI_F / 180.0f;
  int sx = cx + (int) lroundf(sinf(rad) * start_len);
  int sy = cy - (int) lroundf(cosf(rad) * start_len);
  int ex = cx + (int) lroundf(sinf(rad) * len);
  int ey = cy - (int) lroundf(cosf(rad) * len);
  dsc->p1.x = (lv_value_precise_t) sx;
  dsc->p1.y = (lv_value_precise_t) sy;
  dsc->p2.x = (lv_value_precise_t) ex;
  dsc->p2.y = (lv_value_precise_t) ey;
  lv_draw_line(layer, dsc);
}

void LvglClock::canvas_analog_hand_(lv_layer_t *layer, lv_draw_line_dsc_t *dsc, int cx, int cy,
                                       int R, float angle_deg, HandStyle style, Color color,
                                       int baton_width, float len_frac, float tail_frac) {
  int len = (int) (R * len_frac);
  dsc->color = lv_color_make(color.r, color.g, color.b);
  if (style == HAND_STYLE_BATON) {
    // circle -> line -> rounded rectangle: a thin stalk from the centre out
    // to where the thick baton begins, matching the reference (the baton
    // doesn't start flush at the pivot). Both drawn as round-capped lines so
    // the rounded ends are rendered by LVGL consistently with the body (a
    // separate hand-drawn end circle can't match LVGL's asymmetric integer
    // width split and shows a ~1px step at some angles).
    int hub_r = std::max(2, R / 20);
    int stalk_len = hub_r * 3;
    dsc->round_start = dsc->round_end = true;
    dsc->width = std::max(1, R / 35);
    this->canvas_hand_(layer, dsc, cx, cy, stalk_len, angle_deg);
    dsc->width = baton_width;
    this->canvas_hand_(layer, dsc, cx, cy, len, angle_deg, stalk_len);
    if (tail_frac > 0.0f)
      this->canvas_hand_(layer, dsc, cx, cy, (int) (R * tail_frac), angle_deg + 180.0f);
    return;
  }
  if (style == HAND_STYLE_SBB) {
    this->canvas_sbb_hand_(layer, cx, cy, len, angle_deg, color, baton_width);
    if (tail_frac > 0.0f)
      this->canvas_sbb_hand_(layer, cx, cy, (int) (R * tail_frac), angle_deg + 180.0f, color,
                             baton_width);
    return;
  }
  if (style == HAND_STYLE_LOLLIPOP) {
    // Mondaine/SBB second hand: thin line ending inside the ball (the ball
    // is the terminus, not a mid-shaft decoration the line pokes past).
    int ball_r = std::max(2, R / 11);
    int ball_dist = (int) (len * 0.65f);
    dsc->width = std::max(1, R / 50);
    this->canvas_hand_(layer, dsc, cx, cy, ball_dist, angle_deg);
    if (tail_frac > 0.0f)
      this->canvas_hand_(layer, dsc, cx, cy, (int) (R * tail_frac), angle_deg + 180.0f);
    lv_draw_rect_dsc_t dot;
    lv_draw_rect_dsc_init(&dot);
    dot.radius = LV_RADIUS_CIRCLE;
    dot.bg_color = dsc->color;
    dot.bg_opa = LV_OPA_COVER;  // solid fill, not just an outline
    float ar = angle_deg * PI_F / 180.0f;
    int ppx = cx + (int) lroundf(sinf(ar) * ball_dist);
    int ppy = cy - (int) lroundf(cosf(ar) * ball_dist);
    lv_area_t da = {ppx - ball_r, ppy - ball_r, ppx + ball_r, ppy + ball_r};
    lv_draw_rect(layer, &dot, &da);
    return;
  }
  // plain line: `line` = flat/square ends, `line_rounded` = rounded ends.
  dsc->width = std::max(1, R / 50);
  dsc->round_start = dsc->round_end = (style == HAND_STYLE_LINE_ROUNDED);
  this->canvas_hand_(layer, dsc, cx, cy, len, angle_deg);
  if (tail_frac > 0.0f)
    this->canvas_hand_(layer, dsc, cx, cy, (int) (R * tail_frac), angle_deg + 180.0f);
}

void LvglClock::canvas_sbb_hand_(lv_layer_t *layer, int cx, int cy, int len, float angle_deg,
                                    Color color, int base_width) {
  // A plain rectangle, flat-cut ends (no rounding, no taper, no point) - a
  // single thick line with square caps, not 2 triangles (which showed a
  // visible seam artifact along their shared diagonal edge).
  lv_draw_line_dsc_t dsc;
  lv_draw_line_dsc_init(&dsc);
  dsc.color = lv_color_make(color.r, color.g, color.b);
  dsc.width = (int) lroundf(base_width * 1.2f);
  dsc.round_start = dsc.round_end = false;
  this->canvas_hand_(layer, &dsc, cx, cy, len, angle_deg);
}

void LvglClock::canvas_center_(lv_layer_t *layer, int cx, int cy, int r, CenterStyle style,
                                  Color color) {
  if (style == CENTER_STYLE_NONE)
    return;
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.radius = LV_RADIUS_CIRCLE;
  d.bg_opa = LV_OPA_COVER;
  if (style == CENTER_STYLE_ROUND) {
    d.bg_color = lv_color_make(color.r, color.g, color.b);
    lv_area_t a = {cx - r, cy - r, cx + r, cy + r};
    lv_draw_rect(layer, &d, &a);
    return;
  }
  // CENTER_STYLE_CIRCLE: a ring in `color` around a black centre - draw the
  // coloured circle first, then a smaller black one on top.
  d.bg_color = lv_color_make(color.r, color.g, color.b);
  lv_area_t a = {cx - r, cy - r, cx + r, cy + r};
  lv_draw_rect(layer, &d, &a);
  int r2 = std::max(1, r - std::max(2, r / 2));
  d.bg_color = lv_color_make(0, 0, 0);
  lv_area_t a2 = {cx - r2, cy - r2, cx + r2, cy + r2};
  lv_draw_rect(layer, &d, &a2);
}

// What clockclock24 draws, wherever it is drawing: the whole 8x3 grid, one
// digit, or one mini-clock.
void LvglClock::draw_clockclock_(lv_layer_t *layer, int x0, int y0, int w, int h) {
  if (this->partial_ >= 0) {
    if (this->partial_digit_)
      this->draw_cells_(layer, x0, y0, w, h, this->partial_ * CLOCKS_PER_DIGIT, 2, 3);
    else
      this->draw_cells_(layer, x0, y0, w, h, this->partial_, 1, 1);
    return;
  }
  this->draw_grid_(layer, x0, y0, w, h);
}

// LV_EVENT_DRAW_MAIN handler for direct-draw mode: LVGL has already painted
// the widget's background into its own draw buffer and hands us the layer, so
// the hands go straight in - no canvas, and no canvas-to-buffer copy.
void LvglClock::draw_direct_(lv_event_t *e) {
  lv_layer_t *layer = lv_event_get_layer(e);
  lv_area_t coords;
  lv_obj_get_coords(this->obj, &coords);
  int w = lv_area_get_width(&coords), h = lv_area_get_height(&coords);
  if (w <= 0 || h <= 0)
    return;
  uint32_t t0 = micros();
  this->draw_clockclock_(layer, coords.x1, coords.y1, w, h);
  uint32_t dt = micros() - t0;
  this->render_us_total_ += dt;
  this->render_frames_++;
  if (dt > this->render_us_max_)
    this->render_us_max_ = dt;
}

void LvglClock::canvas_clockclock_(int w, int h) {
  this->fill_bg_();
  if (this->partial_ >= 0) {
    // one mini-clock, or the 2x3 block that makes up one digit
    if (this->partial_digit_)
      this->canvas_clockclock_cells_(w, h, this->partial_ * CLOCKS_PER_DIGIT, 2, 3);
    else
      this->canvas_clockclock_cells_(w, h, this->partial_, 1, 1);
    return;
  }
  lv_layer_t layer;
  lv_canvas_init_layer(this->obj, &layer);
  this->draw_grid_(&layer, 0, 0, w, h);
  lv_canvas_finish_layer(this->obj, &layer);
}

// The full 8x3 grid, into any layer at any offset.
void LvglClock::draw_grid_(lv_layer_t *layer, int x0, int y0, int w, int h) {
  auto to_lv = [](Color c) { return lv_color_make(c.r, c.g, c.b); };
  float cols = 8.0f + this->spacing_;
  float rows = 3.0f;
  // Padding eats into the area the cells are sized against: `pad_outside_` at
  // each edge, and `pad_inside_` in each of the 7 column / 2 row gutters.
  int pad_in = this->pad_inside_, pad_out = this->pad_outside_;
  float avail_w = (float) (w - 2 * pad_out - 7 * pad_in);
  float avail_h = (float) (h - 2 * pad_out - 2 * pad_in);
  // Odd diameter -> true centre pixel per clock (even => hands jump when spinning).
  int cell = (int) std::min(avail_w / cols, avail_h / rows);
  if ((cell & 1) == 0)
    cell -= 1;
  if (cell < 3)
    return;
  int radius = cell / 2;
  int len = (int) (radius * 0.86f);
  float grid_w = cell * cols + 7 * pad_in, grid_h = cell * rows + 2 * pad_in;
  int ox = x0 + (int) lroundf((w - grid_w) / 2.0f);
  int oy = y0 + (int) lroundf((h - grid_h) / 2.0f);
  lv_draw_line_dsc_t hand;
  lv_draw_line_dsc_init(&hand);
  hand.color = to_lv(this->pointer_color_());
  // Square ends and +2 px: the real ClockClock's hands are flat-ended bars,
  // and rounded caps at these sizes round the whole hand away. The +2 is on
  // top of `hand_width`, so that option still scales the look.
  hand.width = this->hand_width_ + CC_HAND_EXTRA_PX;
  // Round at the pivot, square at the tip. The rounded inner cap reaches half
  // a width past the start point, so it covers a disc around the pivot and the
  // two hands join cleanly at any angle - a square inner end leaves a notch
  // between them, worst at 90 deg. The tip stays flat, which is the shape the
  // real ClockClock's hands have.
  hand.round_start = true;
  hand.round_end = false;

  bool faces = this->show_face_ && radius > 3;
  lv_draw_rect_dsc_t face;
  if (faces) {
    lv_draw_rect_dsc_init(&face);
    face.radius = LV_RADIUS_CIRCLE;
    face.bg_color = to_lv(this->face_fill_color_());
    face.bg_opa = LV_OPA_COVER;
    face.border_color = to_lv(this->face_border_color_());
    face.border_width = 1;
    face.border_opa = LV_OPA_COVER;
  }
  for (int c = 0; c < NUM_CLOCKS; c++) {
    int digit = c / CLOCKS_PER_DIGIT;
    int cell_i = c % CLOCKS_PER_DIGIT;
    int col = cell_i % 2, row = cell_i / 2;
    int col_i = digit * 2 + col;  // 0..7 - also the number of gutters to its left
    float gcol = col_i + (digit >= 2 ? this->spacing_ : 0.0f);
    int ccx = ox + (int) lroundf(gcol * cell) + col_i * pad_in + radius;  // integer centre
    int ccy = oy + row * (cell + pad_in) + radius;
    if (faces) {
      lv_area_t area = {ccx - radius + 1, ccy - radius + 1, ccx + radius - 1, ccy + radius - 1};
      lv_draw_rect(layer, &face, &area);
    }
    float a0 = this->cur_[c * 2 + 0];
    float a1 = this->cur_[c * 2 + 1];
    this->canvas_hand_(layer, &hand, ccx, ccy, len, a0);
    float delta = fmodf(fabsf(a1 - a0), 360.0f);
    if (delta > 0.5f && delta < 359.5f)
      this->canvas_hand_(layer, &hand, ccx, ccy, len, a1);
  }
  if (this->sync_dot_ && this->sync_dot_on_())
    this->draw_sync_dot_(layer, x0 + w / 2, y0 + h - std::max(2, cell / 8),
                         std::max(2, cell / 10));
}

bool LvglClock::sync_dot_on_() const {
  // A synced node shows nothing: the dot marks a fault, not a heartbeat.
  if (this->synced_)
    return false;
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return tv.tv_usec < 120000;  // first 120 ms of every second
}

void LvglClock::draw_sync_dot_(lv_layer_t *layer, int cx, int cy, int r) {
  lv_draw_rect_dsc_t dot;
  lv_draw_rect_dsc_init(&dot);
  dot.radius = LV_RADIUS_CIRCLE;
  dot.bg_color = lv_color_make(this->pointer_color_().r, this->pointer_color_().g,
                               this->pointer_color_().b);
  dot.bg_opa = LV_OPA_COVER;
  lv_area_t area = {cx - r, cy - r, cx + r, cy + r};
  lv_draw_rect(layer, &dot, &area);
}

// A rectangular block of the 24 clocks, scaled to fill the canvas - the
// `partial:` modes that let separate displays act as one physical ClockClock
// 24. `first` is the index of the top-left clock, and indices run row-major
// two-wide, which is exactly how a digit is laid out (cell = row * 2 + col):
//
//   1x1 starting at 7  -> just clock 7
//   2x3 starting at 6  -> all of digit 1
//
// The animation engine above still runs all 24 clocks on every node; this only
// picks which of them get drawn, so every node stays in step as long as their
// clocks agree.
void LvglClock::canvas_clockclock_cells_(int w, int h, int first, int cols, int rows) {
  lv_layer_t layer;
  lv_canvas_init_layer(this->obj, &layer);
  this->draw_cells_(&layer, 0, 0, w, h, first, cols, rows);
  lv_canvas_finish_layer(this->obj, &layer);
}

void LvglClock::draw_cells_(lv_layer_t *layer, int x0, int y0, int w, int h, int first, int cols,
                            int rows) {
  auto to_lv = [](Color c) { return lv_color_make(c.r, c.g, c.b); };
  int pad_in = this->pad_inside_, pad_out = this->pad_outside_;
  // Odd diameter -> a true centre pixel (even => the hands wobble when spinning).
  int cell = std::min((w - 2 * pad_out - (cols - 1) * pad_in) / cols,
                      (h - 2 * pad_out - (rows - 1) * pad_in) / rows);
  if ((cell & 1) == 0)
    cell -= 1;
  if (cell < 3)
    return;
  int radius = cell / 2;
  int len = (int) (radius * 0.86f);
  int grid_w = cell * cols + (cols - 1) * pad_in, grid_h = cell * rows + (rows - 1) * pad_in;
  int ox = x0 + (w - grid_w) / 2, oy = y0 + (h - grid_h) / 2;

  bool faces = this->show_face_ && radius > 3;
  lv_draw_rect_dsc_t face;
  if (faces) {
    lv_draw_rect_dsc_init(&face);
    face.radius = LV_RADIUS_CIRCLE;
    face.bg_color = to_lv(this->face_fill_color_());
    face.bg_opa = LV_OPA_COVER;
    face.border_color = to_lv(this->face_border_color_());
    // The full-grid path draws a 1px rim on a ~20px clock; scale it here so a
    // 240px face doesn't get a hairline border.
    face.border_width = std::max(1, radius / 20);
    face.border_opa = LV_OPA_COVER;
  }

  lv_draw_line_dsc_t hand;
  lv_draw_line_dsc_init(&hand);
  hand.color = to_lv(this->pointer_color_());
  // hand_width_ is tuned for ~20px mini-clocks in the grid; blown up to a
  // full panel a fixed 1px hand would be a thread, so scale with the radius.
  // Square ends, same as the grid path - see there.
  hand.width = std::max(this->hand_width_ + CC_HAND_EXTRA_PX, radius / CC_HAND_RADIUS_DIV);
  // Round at the pivot, square at the tip - see the grid path for why.
  hand.round_start = true;
  hand.round_end = false;

  for (int c = 0; c < cols * rows; c++) {
    int index = first + c;
    if (index < 0 || index >= NUM_CLOCKS)
      continue;
    int ccx = ox + (c % cols) * (cell + pad_in) + radius;
    int ccy = oy + (c / cols) * (cell + pad_in) + radius;
    if (faces) {
      lv_area_t area = {ccx - radius + 1, ccy - radius + 1, ccx + radius - 1, ccy + radius - 1};
      lv_draw_rect(layer, &face, &area);
    }
    float a0 = this->cur_[index * 2 + 0];
    float a1 = this->cur_[index * 2 + 1];
    this->canvas_hand_(layer, &hand, ccx, ccy, len, a0);
    // Both hands overlapping exactly is how a "blank" cell is drawn - skip the
    // second one so it doesn't fatten the first.
    float delta = fmodf(fabsf(a1 - a0), 360.0f);
    if (delta > 0.5f && delta < 359.5f)
      this->canvas_hand_(layer, &hand, ccx, ccy, len, a1);
    // Sync dot on the first cell only: one blink per node is the point.
    if (c == 0 && this->sync_dot_ && this->sync_dot_on_()) {
      // 1:30 on the face (45 deg), out from the centre - clear of both hands
      // at most angles and never mistaken for one.
      const float k = 0.7071f;  // sin(45) = cos(45)
      int rp = (int) (radius * 0.72f);
      this->draw_sync_dot_(layer, ccx + (int) lroundf(k * rp), ccy - (int) lroundf(k * rp),
                           std::max(2, radius / 14));
    }
  }
}

int LvglClock::tick_width_(int R, TickSize s) {
  switch (s) {
    case TICK_SIZE_SMALL:
      return std::max(1, R / 60);  // old minute-tick default
    case TICK_SIZE_LARGE:
      return std::max(2, R / 18);  // old hour-tick default
    case TICK_SIZE_MEDIUM:
    default:
      return std::max(1, R / 28);  // midpoint
  }
}

float LvglClock::tick_inner_(TickSize s) {
  switch (s) {
    case TICK_SIZE_SMALL:
      return 0.89f;  // old minute-tick default
    case TICK_SIZE_LARGE:
      return 0.74f;  // old hour-tick default
    case TICK_SIZE_MEDIUM:
    default:
      return 0.815f;  // midpoint
  }
}

void LvglClock::canvas_analog_(int w, int h) {
  auto to_lv = [](Color c) { return lv_color_make(c.r, c.g, c.b); };
  this->fill_bg_();
  int cx = w / 2, cy = h / 2;
  int R = std::min(w, h) / 2 - 1;
  if (R < 6)
    return;

  lv_layer_t layer;
  lv_canvas_init_layer(this->obj, &layer);
  lv_draw_line_dsc_t ln;
  lv_draw_line_dsc_init(&ln);

  // Filled dial - without it, black ticks are invisible on the black canvas.
  if (this->show_face_) {
    lv_draw_rect_dsc_t face;
    lv_draw_rect_dsc_init(&face);
    face.radius = LV_RADIUS_CIRCLE;
    face.bg_color = to_lv(this->face_fill_color_());
    face.bg_opa = LV_OPA_COVER;
    face.border_color = to_lv(this->face_border_color_());
    face.border_width = std::max(1, R / 40);
    face.border_opa = LV_OPA_COVER;
    lv_area_t area = {cx - R, cy - R, cx + R, cy + R};
    lv_draw_rect(&layer, &face, &area);
  }
  for (int i = 0; i < 60; i++) {
    bool hour_pos = (i % 5 == 0);
    // If hour ticks are off, minute ticks (when on) fill in at the hour
    // positions too, so you get a full ring instead of 12 gaps.
    bool hour = hour_pos && this->hour_ticks_;
    if (!hour && !this->minute_ticks_)
      continue;
    float a = i * 6.0f * PI_F / 180.0f;
    float inner = hour ? this->tick_inner_(this->hour_ticks_length_)
                       : this->tick_inner_(this->minute_ticks_length_);
    ln.width = hour ? this->tick_width_(R, this->hour_ticks_width_)
                    : this->tick_width_(R, this->minute_ticks_width_);
    ln.color = to_lv(hour ? this->hour_tick_color_() : this->minute_tick_color_());
    ln.round_start = ln.round_end = hour ? this->hour_ticks_rounded_ : this->minute_ticks_rounded_;
    ln.p1.x = (lv_value_precise_t) (cx + sinf(a) * R * inner);
    ln.p1.y = (lv_value_precise_t) (cy - cosf(a) * R * inner);
    ln.p2.x = (lv_value_precise_t) (cx + sinf(a) * R * 0.96f);
    ln.p2.y = (lv_value_precise_t) (cy - cosf(a) * R * 0.96f);
    lv_draw_line(&layer, &ln);
  }
  int hh, mm, ss;
  this->now_or_fake_hms_(hh, mm, ss);
  {
    lv_draw_line_dsc_t hand;
    lv_draw_line_dsc_init(&hand);
    hand.round_start = hand.round_end = true;
    float cs = ss + this->sub_second_(ss);
    float cm = mm + cs / 60.0f;
    float ch = (hh % 12) + cm / 60.0f;
    // Each hand renders fully (line/shape, then its own centre marker) before
    // the next one starts, like a real watch: hour, then minute on top of
    // it, then second on top of both - so the second hand and its marker
    // stay visible even where they cross the hour/minute hub rings.
    this->canvas_analog_hand_(&layer, &hand, cx, cy, R, ch * 30.0f, this->hour_hand_style_,
                              this->hour_hand_color_(), std::max(2, R / 12), 0.55f,
                              this->hour_extend_);
    this->canvas_center_(&layer, cx, cy, std::max(2, R / 14), this->hour_center_style_,
                        this->hour_hand_color_());
    this->canvas_analog_hand_(&layer, &hand, cx, cy, R, cm * 6.0f, this->minute_hand_style_,
                              this->minute_hand_color_(), std::max(2, R / 16), 0.82f,
                              this->minute_extend_);
    this->canvas_center_(&layer, cx, cy, std::max(2, R / 18), this->minute_center_style_,
                        this->minute_hand_color_());
    if (this->show_seconds_) {
      this->canvas_analog_hand_(&layer, &hand, cx, cy, R, cs * 6.0f, this->second_hand_style_,
                                this->second_color_(), std::max(1, R / 20), 0.90f,
                                this->second_extend_);
      this->canvas_center_(&layer, cx, cy, std::max(1, R / 24), this->second_center_style_,
                          this->second_color_());
    }
  }
  lv_canvas_finish_layer(this->obj, &layer);
}

void LvglClock::canvas_draw_segment_(lv_layer_t *layer, int x, int y, int w, int h, bool horiz,
                                        lv_color_t color) {
  if (this->segment_style_ == SEGMENT_STYLE_ROUNDED) {
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color = color;
    rd.bg_opa = LV_OPA_COVER;
    rd.radius = std::min(w, h) / 2;  // rounded ends
    lv_area_t a = {x, y, x + w - 1, y + h - 1};
    lv_draw_rect(layer, &rd, &a);
    return;
  }
  // classic: a square core rect plus a tapered triangular point at each end
  // (along the segment's length), meeting the neighbouring segment's own
  // point exactly at the shared corner - since the layout in seg_rects_
  // already insets horizontal segments by the segment thickness `t`, and a
  // taper of half that thickness from each of the two meeting segments adds
  // up to exactly that gap.
  lv_draw_rect_dsc_t rd;
  lv_draw_rect_dsc_init(&rd);
  rd.bg_color = color;
  rd.bg_opa = LV_OPA_COVER;
  rd.radius = 0;
  lv_area_t a = {x, y, x + w - 1, y + h - 1};
  lv_draw_rect(layer, &rd, &a);

  lv_draw_triangle_dsc_t td;
  lv_draw_triangle_dsc_init(&td);
  td.color = color;
  td.opa = LV_OPA_COVER;
  if (horiz) {
    float taper = h / 2.0f;
    td.p[0].x = (lv_value_precise_t) x;
    td.p[0].y = (lv_value_precise_t) y;
    td.p[1].x = (lv_value_precise_t) x;
    td.p[1].y = (lv_value_precise_t) (y + h);
    td.p[2].x = (lv_value_precise_t) (x - taper);
    td.p[2].y = (lv_value_precise_t) (y + h / 2.0f);
    lv_draw_triangle(layer, &td);
    td.p[0].x = (lv_value_precise_t) (x + w);
    td.p[0].y = (lv_value_precise_t) y;
    td.p[1].x = (lv_value_precise_t) (x + w);
    td.p[1].y = (lv_value_precise_t) (y + h);
    td.p[2].x = (lv_value_precise_t) (x + w + taper);
    td.p[2].y = (lv_value_precise_t) (y + h / 2.0f);
    lv_draw_triangle(layer, &td);
  } else {
    float taper = w / 2.0f;
    td.p[0].x = (lv_value_precise_t) x;
    td.p[0].y = (lv_value_precise_t) y;
    td.p[1].x = (lv_value_precise_t) (x + w);
    td.p[1].y = (lv_value_precise_t) y;
    td.p[2].x = (lv_value_precise_t) (x + w / 2.0f);
    td.p[2].y = (lv_value_precise_t) (y - taper);
    lv_draw_triangle(layer, &td);
    td.p[0].x = (lv_value_precise_t) x;
    td.p[0].y = (lv_value_precise_t) (y + h);
    td.p[1].x = (lv_value_precise_t) (x + w);
    td.p[1].y = (lv_value_precise_t) (y + h);
    td.p[2].x = (lv_value_precise_t) (x + w / 2.0f);
    td.p[2].y = (lv_value_precise_t) (y + h + taper);
    lv_draw_triangle(layer, &td);
  }
}

// Tiny vector "font" for the AM/PM markers - the 7-segment style is
// deliberately font-free, so the letters are drawn as line strokes and
// auto-scale with the widget like everything else. Only A/M/P exist.
void LvglClock::canvas_stroke_text_(lv_layer_t *layer, const char *txt, float x, float y,
                                       float lw, float lh, lv_color_t color) {
  struct Stroke {
    char ch;
    float x1, y1, x2, y2;  // in a unit box, y down
  };
  static const Stroke STROKES[] = {
      {'A', 0.0f, 1.0f, 0.5f, 0.0f},   {'A', 0.5f, 0.0f, 1.0f, 1.0f},
      {'A', 0.2f, 0.62f, 0.8f, 0.62f},
      {'M', 0.0f, 1.0f, 0.0f, 0.0f},   {'M', 0.0f, 0.0f, 0.5f, 0.5f},
      {'M', 0.5f, 0.5f, 1.0f, 0.0f},   {'M', 1.0f, 0.0f, 1.0f, 1.0f},
      {'P', 0.0f, 1.0f, 0.0f, 0.0f},   {'P', 0.0f, 0.0f, 0.8f, 0.0f},
      {'P', 0.8f, 0.0f, 0.8f, 0.52f},  {'P', 0.8f, 0.52f, 0.0f, 0.52f},
  };
  lv_draw_line_dsc_t ln;
  lv_draw_line_dsc_init(&ln);
  ln.color = color;
  ln.width = std::max(1, (int) (lh / 6.0f));
  ln.round_start = ln.round_end = true;
  float cx = x;
  for (const char *p = txt; *p != '\0'; p++) {
    for (const auto &s : STROKES) {
      if (s.ch != *p)
        continue;
      ln.p1.x = (lv_value_precise_t) (cx + s.x1 * lw);
      ln.p1.y = (lv_value_precise_t) (y + s.y1 * lh);
      ln.p2.x = (lv_value_precise_t) (cx + s.x2 * lw);
      ln.p2.y = (lv_value_precise_t) (y + s.y2 * lh);
      lv_draw_line(layer, &ln);
    }
    cx += lw * 1.35f;
  }
}

void LvglClock::canvas_digital_(int w, int h) {
  auto to_lv = [](Color c) { return lv_color_make(c.r, c.g, c.b); };
  this->fill_bg_();
  DigitalCell cells[MAX_CELLS];
  int n = this->digital_cells_(w, h, cells);
  lv_color_t fg = to_lv(this->pointer_color_());
  lv_color_t off = to_lv(this->digital_off_color_());

  lv_layer_t layer;
  lv_canvas_init_layer(this->obj, &layer);
  lv_draw_rect_dsc_t dot;
  lv_draw_rect_dsc_init(&dot);
  dot.radius = LV_RADIUS_CIRCLE;
  dot.bg_opa = LV_OPA_COVER;

  for (int i = 0; i < n; i++) {
    DigitalCell &c = cells[i];
    if (c.val == 10 || c.val == 11) {  // colon on / ghost
      dot.bg_color = (c.val == 10) ? fg : off;
      int r = std::max(1, c.w / 3), mx = c.x + c.w / 2, y1 = c.y + c.h / 3, y2 = c.y + 2 * c.h / 3;
      lv_area_t a1 = {mx - r, y1 - r, mx + r, y1 + r};
      lv_area_t a2 = {mx - r, y2 - r, mx + r, y2 + r};
      lv_draw_rect(&layer, &dot, &a1);
      lv_draw_rect(&layer, &dot, &a2);
    } else if (c.val >= 0 || c.val == -2) {
      int rects[7][4];
      bool act[7];
      bool horiz[7];
      this->seg_rects_(c.val, c.x, c.y, c.w, c.h, rects, act, horiz);
      for (int sg = 0; sg < 7; sg++)
        this->canvas_draw_segment_(&layer, rects[sg][0], rects[sg][1], rects[sg][2], rects[sg][3],
                                   horiz[sg], act[sg] ? fg : off);
    }
  }
  // 12h mode: AM over PM in the reserved left-hand column - the active one
  // lit, the other as an off_color "ghost" like unlit segments.
  if (this->ampm_w_ > 0) {
    float lh = this->ampm_h_ * 0.26f;
    float lw = std::min(lh * 0.85f, this->ampm_w_ / 2.5f);
    float ax = this->ampm_x_;
    this->canvas_stroke_text_(&layer, "AM", ax, (float) this->ampm_y_, lw, lh,
                              this->pm_ ? off : fg);
    this->canvas_stroke_text_(&layer, "PM", ax, this->ampm_y_ + this->ampm_h_ - lh, lw, lh,
                              this->pm_ ? fg : off);
  }
  lv_canvas_finish_layer(this->obj, &layer);
}

// ---------------------------------------------------------------------------
// flipclock - split-flap cards with font-rendered digits
// ---------------------------------------------------------------------------
void LvglClock::flip_card_(lv_layer_t *layer, int x, int y, int w, int h, char ch, int clip_y1,
                              int clip_y2, bool text_bottom) {
  if (clip_y1 > clip_y2)
    return;
  // Restrict the layer's clip area to the requested band - LVGL documents
  // _clip_area as settable before adding draw tasks, and the canvas layer is
  // in canvas-local coordinates, so cell coords can be used directly.
  lv_area_t saved = layer->_clip_area;
  lv_area_t band = {x, std::max(clip_y1, (int) saved.y1), x + w - 1,
                    std::min(clip_y2, (int) saved.y2)};
  layer->_clip_area = band;

  Color cc = this->flip_card_color_();
  lv_draw_rect_dsc_t card;
  lv_draw_rect_dsc_init(&card);
  card.bg_color = lv_color_make(cc.r, cc.g, cc.b);
  card.bg_opa = LV_OPA_COVER;
  card.radius = std::max(2, std::min(w, h) / 10);
  lv_area_t a = {x, y, x + w - 1, y + h - 1};
  lv_draw_rect(layer, &card, &a);

  // 'A'/'P' are the AM/PM card's two states - render the two-letter word in
  // the smaller am_pm_font_ (falling back to the digit font); every other
  // char is a single digit glyph in the main font.
  bool is_ampm = (ch == 'A' || ch == 'P');
  const lv_font_t *font = is_ampm ? (this->am_pm_font_ ? this->am_pm_font_ : this->flip_font_)
                                  : this->flip_font_;
  if (ch != ' ' && font != nullptr) {
    char txt[3];
    if (is_ampm) {
      txt[0] = ch;
      txt[1] = 'M';
      txt[2] = 0;
    } else {
      txt[0] = ch;
      txt[1] = 0;
    }
    lv_point_t sz;
    lv_text_get_size(&sz, txt, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    Color fg = this->pointer_color_();
    lv_draw_label_dsc_t ld;
    lv_draw_label_dsc_init(&ld);
    ld.color = lv_color_make(fg.r, fg.g, fg.b);
    ld.font = font;
    ld.text = txt;
    ld.text_local = 1;  // txt is stack-local - LVGL must copy it into the draw task
    int tx = x + (w - sz.x) / 2;
    // text_bottom centres the text within the lower half of the card (used by
    // the AM/PM card); otherwise centre it in the whole card (digit cards).
    int ty = text_bottom ? (y + h / 2 + (h / 2 - sz.y) / 2) : (y + (h - sz.y) / 2);
    lv_area_t ta = {tx, ty, tx + sz.x, ty + sz.y};
    lv_draw_label(layer, &ld, &ta);
  }
  layer->_clip_area = saved;
}

void LvglClock::canvas_flipclock_(int w, int h) {
  auto to_lv = [](Color c) { return lv_color_make(c.r, c.g, c.b); };
  this->fill_bg_();
  DigitalCell cells[MAX_CELLS];
  int n = this->digital_cells_(w, h, cells);
  uint32_t now = millis();

  lv_layer_t layer;
  lv_canvas_init_layer(this->obj, &layer);

  lv_draw_rect_dsc_t dot;
  lv_draw_rect_dsc_init(&dot);
  dot.radius = LV_RADIUS_CIRCLE;
  dot.bg_opa = LV_OPA_COVER;
  dot.bg_color = to_lv(this->pointer_color_());

  for (int i = 0; i < n; i++) {
    DigitalCell &c = cells[i];
    if (c.val == 10 || c.val == 11) {
      // divider dots between groups - a blinked-off divider just disappears
      // (flip clocks have no "ghost" state). Deliberately small - flip-clock
      // dividers are subtle, unlike the chunky 7-segment colon.
      if (c.val == 10) {
        int r = std::max(1, c.w / 6), mx = c.x + c.w / 2;
        int y1 = c.y + c.h / 3, y2 = c.y + 2 * c.h / 3;
        lv_area_t a1 = {mx - r, y1 - r, mx + r, y1 + r};
        lv_area_t a2 = {mx - r, y2 - r, mx + r, y2 + r};
        lv_draw_rect(&layer, &dot, &a1);
        lv_draw_rect(&layer, &dot, &a2);
      }
      continue;
    }
    if (c.val == 12) {  // dedicated AM/PM card (12h mode) - static, no flip
      char ch = this->pm_ ? 'P' : 'A';
      // draw the whole card once with the label sitting in the bottom half,
      // horizontally centred (so the seam doesn't cut through it).
      this->flip_card_(&layer, c.x, c.y, c.w, c.h, ch, c.y, c.y + c.h - 1, /*text_bottom=*/true);
      lv_draw_rect_dsc_t seam_d;
      lv_draw_rect_dsc_init(&seam_d);
      seam_d.bg_color = to_lv(this->background_);
      seam_d.bg_opa = LV_OPA_COVER;
      int seam = c.y + c.h / 2, st = std::max(1, c.h / 40);
      lv_area_t sa = {c.x, seam - (st + 1) / 2, c.x + c.w - 1, seam + st / 2};
      lv_draw_rect(&layer, &seam_d, &sa);
      continue;
    }
    char ch = (c.val >= 0) ? (char) ('0' + c.val) : (c.val == -2 ? '-' : ' ');
    if (this->flip_shown_[i] == 0) {  // first render: adopt without animating
      this->flip_shown_[i] = ch;
      this->flip_prev_[i] = ch;
      this->flip_start_[i] = now - this->flip_ms_;
    } else if (this->flip_shown_[i] != ch) {
      this->flip_prev_[i] = this->flip_shown_[i];
      this->flip_shown_[i] = ch;
      this->flip_start_[i] = now;
    }
    float t = 1.0f;
    if (this->flip_ms_ > 0)
      t = (now - this->flip_start_[i]) / (float) this->flip_ms_;
    int seam = c.y + c.h / 2;
    if (t >= 1.0f) {
      this->flip_card_(&layer, c.x, c.y, c.w, c.h, ch, c.y, c.y + c.h - 1);
    } else {
      char prev = this->flip_prev_[i];
      int half = c.h / 2;
      float s = t * t;  // ease-in: the flap accelerates like a falling flap
      if (s < 0.5f) {
        // flap (blank card back) falling over the top half, hinged at the
        // seam - the new digit is revealed above it, the old one still shows
        // below the seam.
        int flap_h = (int) lroundf(half * (1.0f - 2.0f * s));
        this->flip_card_(&layer, c.x, c.y, c.w, c.h, prev, seam, c.y + c.h - 1);
        this->flip_card_(&layer, c.x, c.y, c.w, c.h, ch, c.y, seam - flap_h - 1);
        if (flap_h > 0) {
          lv_draw_rect_dsc_t flap;
          lv_draw_rect_dsc_init(&flap);
          Color cc = this->flip_card_color_();
          flap.bg_color = lv_color_darken(lv_color_make(cc.r, cc.g, cc.b), LV_OPA_30);
          flap.bg_opa = LV_OPA_COVER;
          flap.radius = std::max(2, std::min(c.w, c.h) / 10);
          lv_area_t fa = {c.x, seam - flap_h, c.x + c.w - 1, seam - 1};
          lv_draw_rect(&layer, &flap, &fa);
        }
      } else {
        // flap landing on the bottom half, its face showing the new digit -
        // the old digit is still visible below its leading edge.
        int flap_h = (int) lroundf(half * (2.0f * s - 1.0f));
        this->flip_card_(&layer, c.x, c.y, c.w, c.h, ch, c.y, seam - 1);
        this->flip_card_(&layer, c.x, c.y, c.w, c.h, prev, seam + flap_h, c.y + c.h - 1);
        if (flap_h > 0)
          this->flip_card_(&layer, c.x, c.y, c.w, c.h, ch, seam, seam + flap_h - 1);
      }
    }
    // the horizontal seam across the middle of every card
    lv_draw_rect_dsc_t seam_d;
    lv_draw_rect_dsc_init(&seam_d);
    seam_d.bg_color = to_lv(this->background_);
    seam_d.bg_opa = LV_OPA_COVER;
    seam_d.radius = 0;
    int st = std::max(1, c.h / 40);
    lv_area_t sa = {c.x, seam - (st + 1) / 2, c.x + c.w - 1, seam + st / 2};
    lv_draw_rect(&layer, &seam_d, &sa);
  }
  lv_canvas_finish_layer(this->obj, &layer);
}

// ---------------------------------------------------------------------------
// seg_matrix - big HH:MM digits on a 6 x 24 grid of small 7-segment displays.
// The per-display segment patterns are the hand-crafted font from the
// reference "7-segment display array clock" (hackaday.io/project/169632),
// ported verbatim. Each byte is a MAX7219 no-decode segment mask
// (bit6=A .. bit0=G); panel[x][y] with x = 0..5 (rows), y = 0..23 (columns).
// ---------------------------------------------------------------------------
namespace {
void seg_matrix_build(uint8_t panel[6][24], int hh, int mm, bool colon) {
  for (int x = 0; x < 6; x++)
    for (int y = 0; y < 24; y++)
      panel[x][y] = 0;
  auto P = [&](int x, int y, uint8_t b) {
    if (x >= 0 && x < 6 && y >= 0 && y < 24)
      panel[x][y] = b;
  };
  auto A = [&](int x, int y, uint8_t b) {
    if (x >= 0 && x < 6 && y >= 0 && y < 24)
      panel[x][y] |= b;
  };
  auto F = [&](int x1, int y1, int x2, int y2, uint8_t b) {
    for (int i = x1; i <= x2; i++)
      for (int j = y1; j <= y2; j++)
        P(i, j, b);
  };
  auto digit = [&](int place, int value) {
    switch (value) {
      case 0:
        P(0, place + 2, 0b00011000); P(0, place + 3, 0b00111101); P(0, place + 4, 0b01111111);
        P(0, place + 5, 0b00011111); P(1, place + 1, 0b00010000); F(1, place + 2, 1, place + 5, 0b01111111);
        P(1, place + 3, 0b01100111); P(1, place + 4, 0b01111011); P(2, place + 2, 0b01101111);
        P(2, place + 5, 0b01001111); P(2, place + 1, 0b01111101); P(2, place + 4, 0b00110000);
        F(3, place + 1, 3, place + 4, 0b01111111); P(3, place + 3, 0b00010000); P(3, place + 2, 0b00000110);
        A(3, place + 5, 0b00000110); F(4, place + 1, 4, place + 3, 0b01111111); P(4, place + 2, 0b00011111);
        P(4, place + 4, 0b01000111); P(5, place + 1, 0b01110011); P(5, place + 2, 0b01111111);
        P(5, place + 3, 0b01100111);
        break;
      case 1:
        P(0, place + 3, 0b00010000); P(0, place + 4, 0b01111111); P(0, place + 5, 0b00011111);
        F(1, place + 3, 1, place + 4, 0b01111111); P(1, place + 5, 0b00000110); P(2, place + 2, 0b00111101);
        P(2, place + 3, 0b01111111); P(2, place + 4, 0b01001111); F(3, place + 2, 3, place + 3, 0b01111111);
        P(3, place + 1, 0b00110000); P(3, place + 4, 0b00000110); F(4, place + 1, 4, place + 2, 0b01111111);
        P(4, place + 3, 0b01000110); P(5, place + 0, 0b00100000); P(5, place + 1, 0b01111111);
        P(5, place + 2, 0b01111111); P(5, place + 3, 0b00000000);
        break;
      case 2:
        P(0, place + 2, 0b00011000); P(0, place + 3, 0b00111101); P(0, place + 4, 0b01111111);
        P(0, place + 5, 0b00011111); P(1, place + 1, 0b00110000); P(1, place + 2, 0b01111111);
        P(1, place + 3, 0b01100010); P(1, place + 4, 0b01111011); P(1, place + 5, 0b01111111);
        P(2, place + 3, 0b00111000); P(2, place + 4, 0b01111111); P(2, place + 5, 0b01000110);
        P(3, place + 2, 0b00111101); P(3, place + 3, 0b01111111); P(3, place + 4, 0b01000110);
        A(4, place + 0, 0b00010000); F(4, place + 1, 4, place + 2, 0b01111111); P(4, place + 3, 0b01000110);
        P(5, place + 0, 0b00100000); P(5, place + 1, 0b01110011); F(5, place + 2, 5, place + 3, 0b01111111);
        P(5, place + 4, 0b01100111);
        break;
      case 3:
        P(0, place + 2, 0b00011000); F(0, place + 3, 0, place + 4, 0b01111111); P(0, place + 5, 0b00011111);
        P(1, place + 1, 0b00100000); P(1, place + 2, 0b01111111); P(1, place + 3, 0b01100010);
        P(1, place + 4, 0b01111011); P(1, place + 5, 0b01111111); P(2, place + 2, 0b00011000);
        F(2, place + 3, 4, place + 4, 0b01111111); P(2, place + 3, 0b00111101); P(2, place + 5, 0b01000110);
        P(3, place + 2, 0b01100000); P(3, place + 3, 0b01110011); P(3, place + 5, 0b00000100);
        P(4, place + 2, 0b00011101); P(4, place + 1, 0b00011000); P(4, place + 3, 0b00111101);
        A(4, place + 5, 0b00000110); P(5, place + 0, 0b00100000); P(5, place + 1, 0b01110011);
        F(5, place + 2, 5, place + 3, 0b01111111); P(5, place + 4, 0b01000010);
        break;
      case 4:
        P(0, place + 2, 0b00111000); P(0, place + 3, 0b01111111); P(0, place + 4, 0b00010000);
        P(0, place + 5, 0b01111111); P(1, place + 1, 0b00010000); F(1, place + 2, 2, place + 2, 0b01111111);
        P(1, place + 3, 0b01000110); P(1, place + 4, 0b01111101); P(1, place + 5, 0b01001111);
        P(2, place + 1, 0b01111101); P(2, place + 3, 0b00111000); P(2, place + 4, 0b01111111);
        P(2, place + 5, 0b00000110); F(3, place + 1, 3, place + 4, 0b01111111); P(3, place + 5, 0b00000000);
        A(3, place + 0, 0b00010000); P(4, place + 2, 0b00010000); P(4, place + 3, 0b01111111);
        P(4, place + 4, 0b00000110); P(5, place + 2, 0b01111111); P(5, place + 3, 0b01100111);
        break;
      case 5:
        P(0, place + 2, 0b00111101); F(0, place + 3, 0, place + 5, 0b01111111); P(1, place + 1, 0b00110000);
        P(1, place + 2, 0b01111111); P(1, place + 3, 0b01011111); P(1, place + 4, 0b00001100);
        P(2, place + 3, 0b01111111); P(2, place + 2, 0b01110011); P(2, place + 4, 0b01111111);
        P(2, place + 5, 0b00000110); P(3, place + 3, 0b00110000); F(3, place + 4, 4, place + 4, 0b01111111);
        P(3, place + 5, 0b00000110); F(4, place + 1, 4, place + 2, 0b00011101); P(4, place + 3, 0b00111101);
        P(4, place + 5, 0b00000010); P(5, place + 0, 0b00100000); F(5, place + 1, 5, place + 2, 0b01111111);
        P(5, place + 3, 0b01100111); P(5, place + 4, 0b01000010);
        break;
      case 6:
        P(0, place + 3, 0b00011000); P(0, place + 4, 0b01111111); P(0, place + 5, 0b00000110);
        F(1, place + 2, 2, place + 4, 0b01111111); P(1, place + 2, 0b00111101); P(1, place + 4, 0b01000010);
        P(2, place + 1, 0b00111000); P(2, place + 5, 0b00000100); A(3, place + 0, 0b00010000);
        F(3, place + 1, 4, place + 4, 0b01111111); P(3, place + 2, 0b01000110); P(3, place + 3, 0b01100000);
        P(3, place + 5, 0b00000110); A(4, place + 0, 0b00110000); P(4, place + 2, 0b00011111);
        P(4, place + 3, 0b00111101); P(5, place + 1, 0b01110011); P(5, place + 2, 0b01111111);
        P(5, place + 3, 0b01100111);
        break;
      case 7:
        P(0, place + 1, 0b00110000); F(0, place + 2, 0, place + 4, 0b01111111); P(0, place + 5, 0b00011111);
        P(1, place + 1, 0b00100000); P(1, place + 2, 0b01100011); P(1, place + 3, 0b01100011);
        P(1, place + 4, 0b01111111); P(1, place + 5, 0b01111111); P(2, place + 5, 0b01000110);
        P(2, place + 4, 0b01111111); P(2, place + 3, 0b00111101); P(3, place + 2, 0b00111101);
        P(3, place + 3, 0b01111111); P(3, place + 4, 0b01100111); P(4, place + 1, 0b00111101);
        P(4, place + 2, 0b01111111); P(4, place + 3, 0b01000110); F(5, place + 1, 5, place + 2, 0b01111111);
        P(5, place + 0, 0b00100000);
        break;
      case 8:
        P(0, place + 2, 0b00011000); F(0, place + 3, 0, place + 4, 0b01111111); P(0, place + 5, 0b00011111);
        P(1, place + 1, 0b00110000); P(1, place + 2, 0b01111111); P(1, place + 3, 0b01100010);
        P(1, place + 4, 0b01111011); P(1, place + 5, 0b01111111); P(2, place + 1, 0b00100000);
        P(2, place + 2, 0b01111111); F(2, place + 3, 4, place + 4, 0b01111111); P(2, place + 3, 0b00111101);
        P(2, place + 5, 0b01000110); P(3, place + 1, 0b00111101); P(3, place + 2, 0b01100111);
        P(3, place + 3, 0b01110011); P(3, place + 5, 0b00000100); P(4, place + 2, 0b00011101);
        P(4, place + 1, 0b01111111); P(4, place + 3, 0b00111101); A(4, place + 5, 0b00000110);
        A(4, place + 0, 0b00110000); P(5, place + 1, 0b01110011); F(5, place + 2, 5, place + 3, 0b01111111);
        P(5, place + 4, 0b01000010);
        break;
      case 9:
        P(0, place + 2, 0b00011101); P(0, place + 3, 0b01111111); P(0, place + 4, 0b00011111);
        P(0, place + 5, 0b00001100); F(1, place + 1, 3, place + 4, 0b01111111); P(1, place + 1, 0b00111000);
        P(1, place + 3, 0b01100011); P(1, place + 4, 0b01111011); P(1, place + 5, 0b01111111);
        P(2, place + 2, 0b01011111); P(2, place + 3, 0b00011100); P(2, place + 5, 0b01001111);
        P(3, place + 1, 0b01100000); P(3, place + 2, 0b01111011); P(3, place + 5, 0b00000010);
        P(4, place + 1, 0b00011000); P(4, place + 2, 0b01111111); P(4, place + 3, 0b01100111);
        P(5, place + 0, 0b00100000); P(5, place + 1, 0b01111111); P(5, place + 2, 0b01000110);
        break;
      default:
        break;
    }
  };
  digit(0, (hh / 10) % 10);
  digit(5, hh % 10);
  digit(13, (mm / 10) % 10);
  digit(18, mm % 10);
  if (colon) {
    P(1, 12, 0b00011101);
    P(4, 11, 0b00011101);
  }
}
}  // namespace

void LvglClock::canvas_seg_matrix_(int w, int h) {
  auto to_lv = [](Color c) { return lv_color_make(c.r, c.g, c.b); };
  this->fill_bg_();
  const int COLS = 24, ROWS = 6;  // the reference panel / font is fixed at 6x24
  int cw = w / COLS, ch = h / ROWS;
  if (cw < 3 || ch < 5)
    return;

  int hh, mm, ss;
  this->now_or_fake_hms_(hh, mm, ss);
  bool colon = !(this->digital_blink_ && ((millis() / 1000) & 1));
  uint8_t panel[6][24];
  seg_matrix_build(panel, hh, mm, colon);

  // Each small display keeps the 7-segment aspect (taller than wide), centred
  // in its grid cell, so the little displays aren't stretched to the cell.
  int dh = (int) (ch * 0.94f);
  int dw = (int) (dh * 0.55f);
  if (dw > (int) (cw * 0.94f)) {
    dw = (int) (cw * 0.94f);
    dh = (int) (dw / 0.55f);
  }
  int gx = (w - cw * COLS) / 2, gy = (h - ch * ROWS) / 2;

  lv_color_t fg = to_lv(this->pointer_color_());
  lv_color_t off = to_lv(this->digital_off_color_());
  lv_layer_t layer;
  lv_canvas_init_layer(this->obj, &layer);
  for (int x = 0; x < ROWS; x++) {      // panel x -> screen row (vertical)
    for (int y = 0; y < COLS; y++) {    // panel y -> screen column (horizontal)
      uint8_t b = panel[x][y];
      int dx = gx + y * cw + (cw - dw) / 2;
      int dy = gy + x * ch + (ch - dh) / 2;
      int rects[7][4];
      bool act[7], horiz[7];
      this->seg_rects_(8, dx, dy, dw, dh, rects, act, horiz);  // 8 = all seven present
      for (int s = 0; s < 7; s++) {  // s: 0=a..6=g ; MAX7219 A=0x40 .. G=0x01
        bool on = b & (0x40 >> s);
        this->canvas_draw_segment_(&layer, rects[s][0], rects[s][1], rects[s][2], rects[s][3],
                                   horiz[s], on ? fg : off);
      }
    }
  }
  lv_canvas_finish_layer(this->obj, &layer);
}

}  // namespace lvgl_clock
}  // namespace esphome
