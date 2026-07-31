#include "lvgl_clock.h"
#include "esphome/core/log.h"
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

// clockclock24 font: target angle (deg, 0 = 12 o'clock, clockwise) of the two
// hands of each of a digit's 6 clocks (order TL, TR, ML, MR, BL, BR).
static const float FONT[10][CLOCKS_PER_DIGIT][2] = {
    {{90, 180}, {180, 270}, {0, 180}, {0, 180}, {0, 90}, {0, 270}},           // 0
    {{PARK, PARK}, {180, 180}, {PARK, PARK}, {0, 180}, {PARK, PARK}, {0, 0}},  // 1
    {{90, 90}, {180, 270}, {90, 180}, {0, 270}, {0, 90}, {270, 270}},         // 2
    {{90, 90}, {180, 270}, {90, 90}, {0, 180}, {90, 90}, {0, 270}},           // 3
    {{180, 180}, {180, 180}, {0, 90}, {0, 180}, {PARK, PARK}, {0, 0}},        // 4
    {{90, 180}, {270, 270}, {0, 90}, {180, 270}, {90, 90}, {0, 270}},         // 5
    {{90, 180}, {270, 270}, {0, 180}, {180, 270}, {0, 90}, {0, 270}},         // 6
    {{90, 90}, {180, 270}, {PARK, PARK}, {0, 180}, {PARK, PARK}, {0, 0}},     // 7
    {{90, 180}, {180, 270}, {0, 90}, {0, 270}, {0, 90}, {0, 270}},            // 8 (two-box)
    {{90, 180}, {180, 270}, {0, 90}, {0, 180}, {90, 90}, {0, 270}},           // 9
};

static inline float ease(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

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
}

void LvglClock::advance_animation_() {
  if (!this->animating_)
    return;
  // Read a fresh timestamp rather than accepting one from the caller: retarget_()
  // may have just set anim_start_ via its own, later millis() call, and an
  // earlier/stale now_ms here would underflow (anim_start_ > now_ms) and snap
  // the transition to "complete" on the very frame it started.
  uint32_t now_ms = millis();
  float t = (this->transition_ms_ == 0) ? 1.0f
                                        : (now_ms - this->anim_start_) / (float) this->transition_ms_;
  if (t >= 1.0f) {
    for (int i = 0; i < NUM_HANDS; i++) {
      float f = fmodf(this->target_[i], 360.0f);
      this->cur_[i] = (f < 0) ? f + 360.0f : f;
    }
    this->animating_ = false;
  } else {
    float e = ease(t);
    for (int i = 0; i < NUM_HANDS; i++)
      this->cur_[i] = this->start_[i] + (this->target_[i] - this->start_[i]) * e;
  }
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

void LvglClock::tick_rotate_(uint32_t now_ms) {
  float ang = fmodf(now_ms / 1000.0f * 45.0f * this->mode_speed_, 360.0f);
  float a = 360.0f - ang;
  for (int c = 0; c < NUM_CLOCKS; c++) {
    this->cur_[c * 2 + 0] = a;
    this->cur_[c * 2 + 1] = fmodf(a + 180.0f, 360.0f);
  }
}

void LvglClock::tick_birds_(uint32_t now_ms) {
  float ph = now_ms / 1000.0f * 2.0f * this->mode_speed_;
  float wing = 85.0f + 45.0f * sinf(ph);  // sweeps ~40..130 deg
  float left = 360.0f - wing;
  for (int c = 0; c < NUM_CLOCKS; c++) {
    this->cur_[c * 2 + 0] = wing;
    this->cur_[c * 2 + 1] = left;
  }
}

void LvglClock::tick_demo_(uint32_t now_ms) {
  if (now_ms - this->demo_last_ms_ >= this->demo_interval_ms_) {
    this->demo_last_ms_ = now_ms;
    this->demo_min_ = (this->demo_min_ + 1) % (24 * 60);
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
  this->mode_ = m;
  if (m == CC_MODE_TIME) {
    this->last_key_ = -1;
    this->animating_ = false;
  } else if (m == CC_MODE_DEMO) {
    this->last_key_ = -1;
    this->demo_min_ = 0;
    this->demo_last_ms_ = 0;
  }
}

void LvglClock::loop() {
  uint32_t now_ms = millis();
  if (this->style_ == STYLE_CLOCKCLOCK24) {
    switch (this->mode_) {
      case CC_MODE_ROTATE_LEFT:
        this->tick_rotate_(now_ms);
        break;
      case CC_MODE_FLYING_BIRDS:
        this->tick_birds_(now_ms);
        break;
      case CC_MODE_DEMO:
        this->tick_demo_(now_ms);
        break;
      case CC_MODE_TIME:
      default:
        this->tick_time_(now_ms);
        break;
    }
  }
  if (this->obj == nullptr || now_ms - this->last_render_ms_ < this->render_interval_ms_)
    return;
  this->last_render_ms_ = now_ms;
  this->render_();
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
      ESP_LOGE(TAG,
               "Canvas draw buffer (%dx%d, ~%u bytes) failed to allocate - not enough free RAM. "
               "Reduce width/height, lower lvgl's buffer_size, or add PSRAM. Disabling rendering.",
               w, h, (unsigned) (w * h * (this->transparent_ ? 4 : 2)));
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

void LvglClock::canvas_clockclock_(int w, int h) {
  auto to_lv = [](Color c) { return lv_color_make(c.r, c.g, c.b); };
  this->fill_bg_();
  float cols = 8.0f + this->spacing_;
  float rows = 3.0f;
  // Odd diameter -> true centre pixel per clock (even => hands jump when spinning).
  int cell = (int) std::min((float) w / cols, (float) h / rows);
  if ((cell & 1) == 0)
    cell -= 1;
  if (cell < 3)
    return;
  int radius = cell / 2;
  int len = (int) (radius * 0.86f);
  float grid_w = cell * cols, grid_h = cell * rows;
  int ox = (int) lroundf((w - grid_w) / 2.0f), oy = (int) lroundf((h - grid_h) / 2.0f);

  lv_layer_t layer;
  lv_canvas_init_layer(this->obj, &layer);
  lv_draw_line_dsc_t hand;
  lv_draw_line_dsc_init(&hand);
  hand.color = to_lv(this->pointer_color_());
  hand.width = this->hand_width_;
  hand.round_start = hand.round_end = true;

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
    float gcol = digit * 2 + col + (digit >= 2 ? this->spacing_ : 0.0f);
    int ccx = ox + (int) lroundf(gcol * cell) + radius;  // integer centre
    int ccy = oy + row * cell + radius;
    if (faces) {
      lv_area_t area = {ccx - radius + 1, ccy - radius + 1, ccx + radius - 1, ccy + radius - 1};
      lv_draw_rect(&layer, &face, &area);
    }
    float a0 = this->cur_[c * 2 + 0];
    float a1 = this->cur_[c * 2 + 1];
    this->canvas_hand_(&layer, &hand, ccx, ccy, len, a0);
    float delta = fmodf(fabsf(a1 - a0), 360.0f);
    if (delta > 0.5f && delta < 359.5f)
      this->canvas_hand_(&layer, &hand, ccx, ccy, len, a1);
  }
  lv_canvas_finish_layer(this->obj, &layer);
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
