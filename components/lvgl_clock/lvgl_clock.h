#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/color.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/lvgl/lvgl_esphome.h"

namespace esphome {
namespace lvgl_clock {

// A clock rendered onto an LVGL 9 canvas. Pick a `style`:
//   - clockclock24 : a digital clock made of 24 tiny analogue clocks
//   - analog       : one classic analogue clock face
//   - digital      : HH:MM(:SS) as a rounded 7-segment display
//   - flipclock    : HH:MM(:SS) as split-flap cards with font-rendered digits
// It's a native `lvgl:` widget: add it under `lvgl: widgets: - lvgl_clock: ...`,
// like `canvas` or `line`. It owns its canvas and redraws itself from loop().

enum ClockStyle {
  STYLE_CLOCKCLOCK24 = 0,
  STYLE_ANALOG,
  STYLE_DIGITAL,
  STYLE_FLIPCLOCK,
  // Big HH:MM digits drawn on a grid of small 7-segment displays - each small
  // display's segments act as "pixels" of the large numerals.
  STYLE_SEG_MATRIX,
};

// analog-only: how a single hand is drawn.
enum HandStyle {
  HAND_STYLE_BATON = 0,    // thick rounded bar (default hour/minute look)
  HAND_STYLE_LINE,         // thin plain line, flat/square ends
  HAND_STYLE_LOLLIPOP,     // thin line + a round "blob" partway along it
  HAND_STYLE_SBB,          // tapered needle: wide at the pivot, a sharp point at the tip
  HAND_STYLE_LINE_ROUNDED, // thin plain line, rounded ends
};

// analog-only: how a hand's own centre marker is drawn (each hand draws its
// own, stacked hour -> minute -> second so later ones nest on top).
enum CenterStyle {
  CENTER_STYLE_CIRCLE = 0,  // a ring in the hand's colour around a black centre
  CENTER_STYLE_ROUND,       // a plain filled circle in the hand's colour
  CENTER_STYLE_NONE,        // no centre marker for this hand
};

// analog-only: relative size for a tick ring's width or length (s/m/l -
// "m" matches the previous hardcoded default look).
enum TickSize {
  TICK_SIZE_SMALL = 0,
  TICK_SIZE_MEDIUM,
  TICK_SIZE_LARGE,
};

// digital (seven-segment)-only: shape of each segment.
enum SegmentStyle {
  SEGMENT_STYLE_CLASSIC = 0,  // tapered/hexagonal ends meeting at pointed corners (LCD look)
  SEGMENT_STYLE_ROUNDED,      // fully rounded capsule ends
};

// clockclock24-only: idle animations, driven from YAML actions.
enum ClockMode {
  CC_MODE_TIME = 0,
  CC_MODE_ROTATE_LEFT,
  CC_MODE_FLYING_BIRDS,
  // Testing aid: advances a fake minute counter every `demo_interval` instead
  // of reading the real clock, so the digit-flip animation can be watched
  // repeatedly without waiting on real time.
  CC_MODE_DEMO,
};

// clockclock24-only: how the two hands travel to a new digit.
enum MovementMode {
  CC_MOVE_OPPOSITE = 0,
  CC_MOVE_CLOCKWISE,
  CC_MOVE_COUNTER,
  CC_MOVE_LONG,
};

static const int NUM_DIGITS = 4;
static const int CLOCKS_PER_DIGIT = 6;
static const int NUM_CLOCKS = NUM_DIGITS * CLOCKS_PER_DIGIT;  // 24
static const int NUM_HANDS = NUM_CLOCKS * 2;                  // 48

// One glyph of the digital layout. val: 0-9 = digit, 10 = colon (on),
// 11 = colon (blinked off), 12 = AM/PM card (flipclock 12h mode),
// -1 = blank space, -2 = dash ("--:--").
struct DigitalCell {
  int val;
  int x, y, w, h;
};

// max cells one layout can produce: AM/PM card + HH + : + MM + : + SS
static const int MAX_CELLS = 10;

class LvglClock : public Component, public lvgl::LvCompound {
 public:
  // --- shared ---
  void set_time(time::RealTimeClock *t) { this->time_ = t; }
  void set_style(ClockStyle s) { this->style_ = s; }
  void set_twenty_four_hour(bool on) { this->h24_ = on; }
  void set_show_face(bool show) { this->show_face_ = show; }
  void set_foreground(Color c) { this->fg_ = c; }
  void set_background(Color c) { this->background_ = c; }
  // Transparent background: clears the canvas to fully transparent each frame
  // instead of filling `background`, so widgets layered behind the clock show
  // through the gaps between hands/ticks/digits. Needs the canvas allocated
  // in an alpha format (ARGB8888) - the Python side does that when this is set.
  void set_transparent(bool t) { this->transparent_ = t; }
  void set_pointer_color(Color c) {
    this->pointer_ = c;
    this->has_pointer_ = true;
  }
  void set_face_border_color(Color c) {
    this->face_border_ = c;
    this->has_face_border_ = true;
  }
  void set_face_fill_color(Color c) {
    this->face_fill_ = c;
    this->has_face_fill_ = true;
  }
  void set_show_seconds(bool show) { this->show_seconds_ = show; }
  // Canvas size, known from config at codegen time - render_() uses this
  // rather than lv_obj_get_width/height(this->obj), which isn't reliably
  // resolved yet the first few times our own loop() runs (LVGL only
  // computes actual layout during its own refresh pass).
  void set_canvas_size(int w, int h) {
    this->canvas_w_ = w;
    this->canvas_h_ = h;
  }
  void set_render_interval(uint32_t ms) { this->render_interval_ms_ = ms; }

  // --- clockclock24 ---
  void set_hand_width(int px) { this->hand_width_ = px; }
  void set_transition_length(uint32_t ms) { this->transition_ms_ = ms; }
  void set_spacing(float clocks) { this->spacing_ = clocks; }
  void set_movement(MovementMode m) { this->movement_ = m; }
  void set_mode_speed(float s) { this->mode_speed_ = s; }
  void set_mode(ClockMode m);
  void set_time_mode() { this->set_mode(CC_MODE_TIME); }
  void set_rotate_left_mode() { this->set_mode(CC_MODE_ROTATE_LEFT); }
  void set_flying_birds_mode() { this->set_mode(CC_MODE_FLYING_BIRDS); }
  void set_demo_mode() { this->set_mode(CC_MODE_DEMO); }
  void set_demo_interval(uint32_t ms) { this->demo_interval_ms_ = ms; }
  ClockMode get_mode() const { return this->mode_; }

  // --- analog (classic) ---
  void set_minute_ticks(bool on) { this->minute_ticks_ = on; }
  void set_hour_ticks(bool on) { this->hour_ticks_ = on; }
  void set_hour_ticks_rounded(bool on) { this->hour_ticks_rounded_ = on; }
  void set_minute_ticks_rounded(bool on) { this->minute_ticks_rounded_ = on; }
  void set_hour_ticks_width(TickSize s) { this->hour_ticks_width_ = s; }
  void set_minute_ticks_width(TickSize s) { this->minute_ticks_width_ = s; }
  void set_hour_ticks_length(TickSize s) { this->hour_ticks_length_ = s; }
  void set_minute_ticks_length(TickSize s) { this->minute_ticks_length_ = s; }
  void set_hour_ticks_color(Color c) {
    this->hour_ticks_color_ = c;
    this->has_hour_ticks_color_ = true;
  }
  void set_minute_ticks_color(Color c) {
    this->minute_ticks_color_ = c;
    this->has_minute_ticks_color_ = true;
  }
  void set_hour_hand_style(HandStyle s) { this->hour_hand_style_ = s; }
  void set_minute_hand_style(HandStyle s) { this->minute_hand_style_ = s; }
  void set_second_hand_style(HandStyle s) { this->second_hand_style_ = s; }
  void set_hour_hand_color(Color c) {
    this->hour_hand_ = c;
    this->has_hour_hand_ = true;
  }
  void set_minute_hand_color(Color c) {
    this->minute_hand_ = c;
    this->has_minute_hand_ = true;
  }
  void set_second_hand_color(Color c) {
    this->second_ = c;
    this->has_second_ = true;
  }
  void set_hour_center_style(CenterStyle s) { this->hour_center_style_ = s; }
  void set_minute_center_style(CenterStyle s) { this->minute_center_style_ = s; }
  void set_second_center_style(CenterStyle s) { this->second_center_style_ = s; }
  // 0..0.5 - extends the hand a bit past the pivot on the opposite side (e.g.
  // the second hand's small counterweight tail).
  void set_hour_extend(float f) { this->hour_extend_ = f; }
  void set_minute_extend(float f) { this->minute_extend_ = f; }
  void set_second_extend(float f) { this->second_extend_ = f; }

  // --- digital (seven-segment) ---
  void set_segment_style(SegmentStyle s) { this->segment_style_ = s; }
  void set_digital_blink(bool on) { this->digital_blink_ = on; }
  void set_digital_blank_leading(bool on) { this->digital_blank_leading_ = on; }
  void set_digital_off_color(Color c) {
    this->digital_off_ = c;
    this->has_digital_off_ = true;
  }

  // --- flipclock ---
  // Takes either a built-in LVGL font (`&lv_font_montserrat_48`) or an
  // ESPHome `font:` component - the second overload bridges the latter the
  // same way lvgl_esphome.h's own style-setter overloads do.
  void set_flip_font(const lv_font_t *f) { this->flip_font_ = f; }
#if defined(USE_FONT) && defined(USE_LVGL_FONT)
  void set_flip_font(const font::Font *f) { this->flip_font_ = f->get_lv_font(); }
#endif
  void set_card_color(Color c) {
    this->card_color_ = c;
    this->has_card_color_ = true;
  }
  void set_flip_duration(uint32_t ms) { this->flip_ms_ = ms; }
  void set_flip_show_dots(bool on) { this->flip_show_dots_ = on; }
  // Small AM/PM marker font, only drawn in 12h mode (twenty_four_hour: false).
  void set_am_pm_font(const lv_font_t *f) { this->am_pm_font_ = f; }
#if defined(USE_FONT) && defined(USE_LVGL_FONT)
  void set_am_pm_font(const font::Font *f) { this->am_pm_font_ = f->get_lv_font(); }
#endif

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  int min_width() const;
  int min_height() const;

 protected:
  // helpers
  bool now_hms_(int &hh, int &mm, int &ss);  // returns is_valid, applies 12/24h
  // Like now_hms_, but falls back to a fake 00:15 + uptime-driven seconds
  // when no valid time is available yet, so the face looks alive while
  // waiting to sync. Used by every style except clockclock24, which keeps
  // its own wifi-phase behaviour (blank/park + idle-animation actions).
  void now_or_fake_hms_(int &hh, int &mm, int &ss);
  Color pointer_color_() const { return this->has_pointer_ ? this->pointer_ : this->fg_; }
  Color face_border_color_() const { return this->has_face_border_ ? this->face_border_ : this->fg_; }
  Color face_fill_color_() const { return this->has_face_fill_ ? this->face_fill_ : this->background_; }
  // Swiss railway second hand is red by default.
  Color second_color_() const { return this->has_second_ ? this->second_ : Color(0xE0, 0x30, 0x30); }
  Color hour_hand_color_() const { return this->has_hour_hand_ ? this->hour_hand_ : this->pointer_color_(); }
  Color minute_hand_color_() const { return this->has_minute_hand_ ? this->minute_hand_ : this->pointer_color_(); }
  Color hour_tick_color_() const {
    return this->has_hour_ticks_color_ ? this->hour_ticks_color_ : this->face_border_color_();
  }
  Color minute_tick_color_() const {
    return this->has_minute_ticks_color_ ? this->minute_ticks_color_ : this->face_border_color_();
  }
  // Tick width in px and length as an "inner" fraction (0..1, distance from
  // centre where the tick starts - the outer end is always at 0.96*R). One
  // shared scale for hour and minute ticks - "s"/"m"/"l" mean the same
  // absolute size on either (s = the old minute-tick default, l = the old
  // hour-tick default, m = the midpoint; the schema defaults each ring to
  // its own historical size - minute: s, hour: l).
  static int tick_width_(int R, TickSize s);
  static float tick_inner_(TickSize s);
  // Fraction (0..1) of the way through the current second, from millis - lets
  // all three hands sweep continuously instead of jumping.
  float sub_second_(int ss);
  Color digital_off_color_() const {
    return this->has_digital_off_ ? this->digital_off_ : Color(0x22, 0x22, 0x22);
  }
  Color flip_card_color_() const {
    return this->has_card_color_ ? this->card_color_ : Color(0x2A, 0x2A, 0x2A);
  }

  // shared 7-segment layout. seg_rects_ returns each segment's bounding rect,
  // orientation and which are lit for `digit` (0-9, or -2 for a dash).
  int digital_cells_(int w, int h, DigitalCell out[MAX_CELLS]);
  static void seg_rects_(int digit, int dx, int dy, int dw, int dh, int rects[7][4],
                         bool active[7], bool horiz[7]);

  // clockclock24 animation engine
  void set_time_(int hh, int mm);
  void retarget_();
  // Steps `cur_[]` toward `target_[]` per `transition_ms_` - shared by every
  // mode that calls retarget_() (tick_time_, tick_demo_). Reads its own
  // millis() rather than taking one from the caller - see the .cpp for why.
  void advance_animation_();
  void tick_time_(uint32_t now_ms);
  void tick_rotate_(uint32_t now_ms);
  void tick_birds_(uint32_t now_ms);
  void tick_demo_(uint32_t now_ms);

  // rendering: draws the current state onto `this->obj` (owned lv_canvas_t)
  void render_();
  // Clears the canvas at the start of each frame: to `background_` normally,
  // or to fully transparent when transparent_ (needs an ARGB8888 canvas).
  void fill_bg_();
  void canvas_clockclock_(int w, int h);
  void canvas_analog_(int w, int h);
  void canvas_digital_(int w, int h);
  void canvas_flipclock_(int w, int h);
  // Big HH:MM digits sampled onto a seg_cols_ x seg_rows_ grid of small
  // 7-segment displays: each small segment lights when it falls under a lit
  // stroke of the large 7-segment numerals.
  void canvas_seg_matrix_(int w, int h);
  // One flip card (rounded rect + centred font glyph), drawn only inside the
  // vertical band [clip_y1, clip_y2] - the flip animation renders the same
  // card up to three times per frame with different bands/characters.
  void flip_card_(lv_layer_t *layer, int x, int y, int w, int h, char ch, int clip_y1,
                  int clip_y2, bool text_bottom = false);
  void canvas_hand_(lv_layer_t *layer, lv_draw_line_dsc_t *dsc, int cx, int cy, int len,
                    float angle_deg, int start_len = 0);
  // Draws one analog hand per its HandStyle: baton = thick rounded bar,
  // line = thin bar, lollipop = thin bar + a hollow ring at 0.62*R.
  // tail_frac (0 = none) adds a short line the opposite direction from the
  // pivot, e.g. the second hand's small counterweight tail.
  void canvas_analog_hand_(lv_layer_t *layer, lv_draw_line_dsc_t *dsc, int cx, int cy, int R,
                           float angle_deg, HandStyle style, Color color, int baton_width,
                           float len_frac, float tail_frac = 0.0f);
  // sbb hand shape: a single triangle - wide flat base at the pivot,
  // tapering to a sharp point at the tip. No rounded caps.
  void canvas_sbb_hand_(lv_layer_t *layer, int cx, int cy, int len, float angle_deg, Color color,
                       int base_width);
  // One hand's own centre marker, radius `r` - circle = ring (colour) around
  // a black centre, round = plain filled circle, none = nothing. Hands draw
  // their own markers stacked hour -> minute -> second (see canvas_analog_),
  // so give each hand a different `r` for a nested look.
  void canvas_center_(lv_layer_t *layer, int cx, int cy, int r, CenterStyle style, Color color);
  // Draws one 7-segment bar per segment_style_: rounded = capsule (rounded-end
  // rect), classic = a core rect with a tapered triangular point at each end
  // (the two ends the segment's *length* runs along - left/right if `horiz`,
  // top/bottom otherwise), meeting the neighbouring segment's point exactly at
  // the shared corner.
  void canvas_draw_segment_(lv_layer_t *layer, int x, int y, int w, int h, bool horiz,
                            lv_color_t color);
  // Vector-stroke letters (A/M/P only) for the 7-segment AM/PM markers -
  // keeps the digital style font-free, auto-scaling with the widget.
  // lw/lh = one letter's width/height in px.
  void canvas_stroke_text_(lv_layer_t *layer, const char *txt, float x, float y, float lw,
                           float lh, lv_color_t color);

  // shared config
  time::RealTimeClock *time_{nullptr};
  ClockStyle style_{STYLE_CLOCKCLOCK24};
  bool h24_{true};
  bool show_face_{false};
  Color fg_{Color(255, 255, 255)};
  Color background_{Color(0, 0, 0)};
  Color pointer_{}, face_border_{}, face_fill_{}, second_{}, hour_hand_{}, minute_hand_{};
  bool has_pointer_{false}, has_face_border_{false}, has_face_fill_{false}, has_second_{false};
  bool has_hour_hand_{false}, has_minute_hand_{false};
  bool size_checked_{false};
  // False if the canvas draw buffer failed to allocate (logged once in
  // render_()) - render_() then bails out instead of writing through a null
  // buffer pointer, which otherwise hard-crashes (StoreProhibited).
  bool render_ok_{true};
  bool show_seconds_{false};
  bool transparent_{false};
  uint32_t render_interval_ms_{16};
  uint32_t last_render_ms_{0};
  int canvas_w_{0};
  int canvas_h_{0};

  // clockclock24
  int hand_width_{1};
  uint32_t transition_ms_{2000};
  float spacing_{0.0f};
  MovementMode movement_{CC_MOVE_OPPOSITE};
  float mode_speed_{1.0f};
  ClockMode mode_{CC_MODE_TIME};
  int digits_[NUM_DIGITS]{-1, -1, -1, -1};
  int last_key_{-1};
  float cur_[NUM_HANDS];
  float start_[NUM_HANDS];
  float target_[NUM_HANDS];
  bool animating_{false};
  uint32_t anim_start_{0};
  uint32_t demo_interval_ms_{5000};
  uint32_t demo_last_ms_{0};
  int demo_min_{0};

  // analog (classic)
  bool minute_ticks_{true};
  bool hour_ticks_{true};
  bool hour_ticks_rounded_{true};
  bool minute_ticks_rounded_{true};
  TickSize hour_ticks_width_{TICK_SIZE_LARGE};
  TickSize minute_ticks_width_{TICK_SIZE_SMALL};
  TickSize hour_ticks_length_{TICK_SIZE_LARGE};
  TickSize minute_ticks_length_{TICK_SIZE_SMALL};
  Color hour_ticks_color_{}, minute_ticks_color_{};
  bool has_hour_ticks_color_{false}, has_minute_ticks_color_{false};
  HandStyle hour_hand_style_{HAND_STYLE_BATON};
  HandStyle minute_hand_style_{HAND_STYLE_BATON};
  HandStyle second_hand_style_{HAND_STYLE_LOLLIPOP};
  CenterStyle hour_center_style_{CENTER_STYLE_CIRCLE};
  CenterStyle minute_center_style_{CENTER_STYLE_CIRCLE};
  CenterStyle second_center_style_{CENTER_STYLE_CIRCLE};
  float hour_extend_{0.0f};
  float minute_extend_{0.0f};
  float second_extend_{0.0f};
  int last_sec_{-1};
  uint32_t last_sec_ms_{0};

  // digital (seven-segment)
  SegmentStyle segment_style_{SEGMENT_STYLE_CLASSIC};
  bool digital_blink_{false};
  bool digital_blank_leading_{false};
  Color digital_off_{};
  bool has_digital_off_{false};
  // left-hand AM/PM column, computed by digital_cells_ each layout pass
  // (w == 0 when unused, i.e. 24h mode or non-7-segment styles)
  int ampm_x_{0}, ampm_y_{0}, ampm_w_{0}, ampm_h_{0};

  // flipclock
  const lv_font_t *flip_font_{nullptr};
  const lv_font_t *am_pm_font_{nullptr};
  Color card_color_{};
  bool has_card_color_{false};
  uint32_t flip_ms_{450};
  bool flip_show_dots_{true};
  // whether the current (real) time is >= 12:00 - set by now_hms_ before its
  // 12h conversion; false while running on the pre-sync fake time.
  bool pm_{false};
  // per-cell flip state, indexed like digital_cells_' output (colon slots
  // unused). 0 = never shown yet (first render adopts without animating).
  char flip_shown_[MAX_CELLS]{};
  char flip_prev_[MAX_CELLS]{};
  uint32_t flip_start_[MAX_CELLS]{};
};

// Action for clockclock24 idle animations: lvgl_clock.show_time / .rotate_left
// / .flying_birds.
template<typename... Ts> class SetModeAction : public Action<Ts...> {
 public:
  SetModeAction(LvglClock *parent, ClockMode mode) : parent_(parent), mode_(mode) {}
  void play(Ts... x) override { this->parent_->set_mode(this->mode_); }

 protected:
  LvglClock *parent_;
  ClockMode mode_;
};

}  // namespace lvgl_clock
}  // namespace esphome
