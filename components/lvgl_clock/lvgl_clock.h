#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/color.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif

#include <vector>

namespace esphome {
namespace lvgl_clock {

// Allocates the widget's canvas buffer, preferring internal SRAM.
//
// ESPHome points every LVGL allocation at PSRAM first (lv_malloc_core() uses
// MALLOC_CAP_SPIRAM), which is right for big static buffers but wrong for this
// canvas: it is vector-drawn pixel by pixel on every frame, and PSRAM write
// latency then dominates the frame time. Falls back to PSRAM when the canvas
// is too large to fit internally.
void *alloc_canvas_buf(size_t size);

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
//
// The numbering is wire format - it travels between nodes in the UART sync
// packet - so new modes are APPENDED, never inserted. That way a wall being
// flashed one board at a time never has two firmwares disagreeing about what
// mode 3 means.
enum ClockMode {
  CC_MODE_TIME = 0,
  CC_MODE_ROTATE_LEFT,
  CC_MODE_FLYING_BIRDS,
  // Testing aid: advances a fake minute counter every `demo_interval` instead
  // of reading the real clock, so the digit-flip animation can be watched
  // repeatedly without waiting on real time.
  CC_MODE_DEMO,
  // Hands rest on the 10:30-4:30 diagonal, then turn clockwise column by
  // column, left to right.
  CC_MODE_WAVE,
  // A counter-clockwise turn sweeping diagonally, bottom-left to top-right.
  CC_MODE_SPIRAL,
  // A stalk bent by a gust: each wall column is one continuous stroke, and its
  // two free ends are pushed downwind while the middle row stays put.
  CC_MODE_WIND,
  // Spells LOVE across the four digit positions and holds it.
  CC_MODE_LOVE,
  // Shows a temperature as two digits plus a degree sign and a C. Needs a
  // sensor; without one it is skipped rather than shown blank.
  CC_MODE_TEMP,
  // Keep last: the sync platform validates incoming modes against this.
  CC_MODE_LAST = CC_MODE_TEMP,
};

// Mode name for logs. The wire format is an integer, and "mode 5" in a log is
// useless when you are stood in front of a wall trying to match it against
// what the panels are doing - so both ends of the bus print the name.
inline const char *clock_mode_name(ClockMode m) {
  switch (m) {
    case CC_MODE_TIME:
      return "time";
    case CC_MODE_ROTATE_LEFT:
      return "rotate_left";
    case CC_MODE_FLYING_BIRDS:
      return "flying_birds";
    case CC_MODE_DEMO:
      return "demo";
    case CC_MODE_WAVE:
      return "wave";
    case CC_MODE_SPIRAL:
      return "spiral";
    case CC_MODE_WIND:
      return "wind";
    case CC_MODE_LOVE:
      return "love";
    case CC_MODE_TEMP:
      return "temp";
  }
  return "unknown";
}

// The wall the choreographies are laid out on: 4 digits of 2 columns each,
// 3 rows tall. A widget rendering only part of it (`partial:`) still indexes
// clocks 0..23 globally, so every node computes the same figure for its own
// slice with no coordination beyond the shared clock.
static const int WALL_COLS = 8;
static const int WALL_ROWS = 3;

// clockclock24-only: how the two hands travel to a new digit.
enum MovementMode {
  CC_MOVE_OPPOSITE = 0,
  CC_MOVE_CLOCKWISE,
  CC_MOVE_COUNTER,
  CC_MOVE_LONG,
};

// "no temperature" - outside any plausible reading, and what goes on the wire
// when the master has no sensor or it has not published yet.
static const int TEMP_NONE = -1000;

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
  // 8 bit greyscale (LV_COLOR_FORMAT_L8) instead of RGB565: half the canvas
  // RAM (a 240x240 face is 57.6 KB instead of 115 KB) on a chip with no PSRAM.
  // Every drawn colour is reduced to its luminance, so the face is greyscale -
  // which costs nothing for the usual white-on-black clock.
  //
  // Not 1-bit (I1): LVGL's I1 blend resolves an anti-aliased pixel as
  // mask/255 in integer maths, so every partially covered pixel is discarded
  // and thin lines disappear. L8 mixes with lv_color_8_8_mix() and renders
  // anti-aliasing correctly.
  void set_grayscale(bool g) { this->grayscale_ = g; }
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
  // Draw straight into LVGL's draw buffer from a DRAW_MAIN event instead of
  // owning a canvas. Saves the canvas RAM *and* the per-frame copy of that
  // canvas into LVGL's buffer, which measurements showed to be the dominant
  // cost with several displays on one MCU. clockclock24 only for now.
  void set_direct_draw(bool d) { this->direct_ = d; }
  // Called from the LV_EVENT_DRAW_MAIN callback - public so the static
  // trampoline can reach it.
  void draw_direct_(lv_event_t *e);

  // --- clockclock24 ---
  void set_hand_width(int px) { this->hand_width_ = px; }
  void set_transition_length(uint32_t ms) { this->transition_ms_ = ms; }
  void set_spacing(float clocks) { this->spacing_ = clocks; }
  // Gap in px between neighbouring mini-clocks. `spacing` already separates
  // the HH and MM groups in clock-widths; this is a plain pixel gutter applied
  // between every pair, which is what you want when each clock is a physical
  // panel with its own bezel.
  void set_padding_inside(int px) { this->pad_inside_ = px; }
  // Margin in px between the clock block and the edge of the canvas.
  void set_padding_outside(int px) { this->pad_outside_ = px; }
  void set_movement(MovementMode m) { this->movement_ = m; }
  void set_mode_speed(float s) { this->mode_speed_ = s; }
  void set_mode(ClockMode m);
  void set_time_mode() { this->set_mode(CC_MODE_TIME); }
  void set_rotate_left_mode() { this->set_mode(CC_MODE_ROTATE_LEFT); }
  void set_flying_birds_mode() { this->set_mode(CC_MODE_FLYING_BIRDS); }
  void set_demo_mode() { this->set_mode(CC_MODE_DEMO); }
  void set_demo_interval(uint32_t ms) { this->demo_interval_ms_ = ms; }
  // Break out into the next choreography every `interval` seconds. The list is
  // walked IN ORDER and wraps, so repeating an entry simply shows it more
  // often - [wave, wind, wave, spiral] gives wave half the slots. How long the
  // window lasts and where it sits inside the interval are NOT configurable -
  // see CYCLE_WINDOW_S / CYCLE_OFFSET_S in the .cpp. Only the cadence is,
  // because that is the knob with a defensible range; the other two only offer
  // ways to make a wall look like a disco. Needs a synced clock; nothing fires
  // until the time is valid.
  void set_cycle_interval(uint32_t interval_s) { this->cycle_interval_s_ = interval_s; }
  void add_cycle_mode(ClockMode m) { this->cycle_modes_.push_back(m); }
#ifdef USE_SENSOR
  // Source for `mode: temp`. Optional: with no sensor - or before it has
  // published a reading - the temp mode is skipped in the cycle rather than
  // shown as blanks, so a wall never sits on an empty face waiting for data.
  void set_temperature_sensor(sensor::Sensor *s) { this->temp_sensor_ = s; }
#endif
  // A temperature handed in from outside - the UART sync platform pushes the
  // master's reading here on every node, so slaves show `temp` without a
  // sensor of their own. TEMP_NONE clears it.
  void adopt_temperature(int celsius);
  // What `temp` would draw right now, or TEMP_NONE. The sync platform reads
  // this to decide what to broadcast.
  int temperature_value() const;
  bool temperature_ready_() const;
  // Marks this widget as taking its mode off the bus instead of choosing one.
  //
  // Exactly ONE node on a wall may pick, and the rest must be told, or the
  // eight boards drift into eight different choreographies the first time
  // their clocks or their config disagree by a hair. So a listening sync
  // platform sets this on its widgets and their `cycle_modes:` never fires -
  // the master picks, and the choice travels in the mode field of the sync
  // packet like any other mode change. `cycle_modes:` can therefore stay in
  // the shared config; only the master acts on it.
  void set_mode_follower(bool f) { this->mode_follower_ = f; }
  // Hold every hand at 12 o'clock for this long after boot before the clock
  // takes over (0 = off). On a wall built from 24 separate displays it is the
  // quickest check that every panel is alive and mounted the right way up -
  // any module whose "up" is not up is obvious at a glance - and the first
  // sweep then starts from a known position on every node.
  void set_startup_align(uint32_t ms) { this->startup_align_ms_ = ms; }
  // A dot that flashes for the first 120 ms of every wall-clock second, shown
  // ONLY while this node is out of sync. It is a fault light, not a heartbeat:
  // a healthy wall shows nothing at all, and any panel still blinking is one
  // that has not heard a usable time from the master.
  //
  // Because the dot is driven off the shared clock, the ones that *are*
  // blinking still blink together - so a whole column coming up at once reads
  // as "the master is late", while a single odd panel reads as "that node's
  // bus drop is bad".
  void set_sync_dot(bool on) { this->sync_dot_ = on; }
  // Sync state, pushed in by the UART time platform (see sync_time.cpp).
  // Defaults to true so that anything without a sync bus - the master, or a
  // plain single-display clock - never shows the dot without being told to.
  // Only a listening node ever sets this false.
  void set_synced(bool s) { this->synced_ = s; }
  // How many fake minutes each demo tick jumps. 1 walks the clock a minute at
  // a time, which only ever moves the minutes digits - the hours digits sit
  // still for 5 to 50 minutes of real time. A stride like 137 (2h17m) changes
  // all four digits on every tick, so any node is worth watching during
  // bring-up regardless of which clock it renders.
  void set_demo_step(int minutes) { this->demo_step_ = minutes; }
  ClockMode get_mode() const { return this->mode_; }
  // `mode: demo` counts fake minutes off the local millis(), so two nodes that
  // entered demo at different moments would show different times. The UART
  // sync carries the master's counter and slaves adopt it through these.
  int get_demo_min() const { return this->demo_min_; }
  void adopt_demo_min(int m);
  // Render only part of the 24-clock grid, filling the canvas (-1 = the whole
  // 8x3 grid, the default). For building a physical ClockClock 24 out of
  // separate displays: every node runs the identical animation engine over all
  // 24 clocks and just draws its share, so the digit sweeps stay in step.
  //   set_partial(i)        - one mini-clock, i = digit * 6 + cell (0-23)
  //   set_partial_digit(d)  - one whole digit as a 2x3 block (0-3)
  void set_partial(int index) { this->partial_ = index; }
  void set_partial_digit(int digit) {
    this->partial_ = digit;
    this->partial_digit_ = true;
  }

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
  void tick_demo_(uint32_t now_ms);
  // The idle choreographies. They take the SYNCED wall clock, not millis():
  // every node's millis() starts at its own power-on, so a millis()-driven
  // figure is a different figure on every board. Off the shared clock the whole
  // wall draws one animation, which is the entire point of the sync bus.
  void tick_rotate_(double t);
  void tick_birds_(double t);
  void tick_wave_(double t);
  void tick_spiral_(double t);
  void tick_wind_(double t);
  void tick_love_(double t);
  void tick_temp_(double t);
  // The animation time base: seconds, monotonic, smooth, and phase-aligned
  // with every other node on the wall.
  //
  // It cannot just be gettimeofday(). A listening node has settimeofday()
  // called on it once a second from the bus, and each of those corrections
  // steps the clock back by however long the packet took to arrive - a few ms,
  // varying. Invisible on a digit clock; on a continuous animation it is a
  // twitch every single second. SNTP stepping the master does the same thing
  // more rarely.
  //
  // So this free-runs off millis() (monotonic, never stepped) and is *slewed*
  // toward the synced clock at a capped rate: the phase converges within a
  // second or so, but the animation never runs more than ANIM_SLEW_MAX faster
  // or slower than real time, which is far below what an eye can see. Only a
  // gross error - the first sync, where there is no phase to preserve - is
  // applied as a jump.
  double anim_clock_();
  // Wall position (0..7, 0..2) of a global clock index. See the README's
  // numbering table.
  static void wall_pos_(int c, int &col, int &row);
  // Applies a mode unconditionally - set_mode() defers to this once it has
  // decided the request is not being held off by an active random window.
  void apply_mode_(ClockMode m);
  // Eases the hands from wherever they are into a newly started choreography.
  //
  // The idle animations write cur_[] straight out, so without this, entering
  // one teleports all 48 hands - which no real clock can do, and which is very
  // obvious on a wall. Leaving one is already smooth, because time/demo mode
  // goes through retarget_() and sweeps from wherever the hands were left.
  //
  // Implemented as a decaying OFFSET rather than an interpolation toward the
  // live angle: the offset is measured once, when the choreography's first
  // frame is known, and then faded out. Interpolating toward a moving target
  // would flip sign the moment that target passed the hand's antipode - a jump
  // of its own, in the middle of the fix.
  void blend_into_mode_();
  // Runs whichever choreography `m` names, writing cur_[]. Lets the settle
  // keep the outgoing animation alive underneath it.
  void tick_choreography_(ClockMode m, double t);
  // One frame of the settle out of a choreography into the time, with the
  // choreography still running. Returns true once every hand has arrived.
  bool settle_blend_();
  // True for the choreographies, i.e. the modes that drive cur_[] directly.
  static bool is_idle_animation_(ClockMode m) {
    return m == CC_MODE_ROTATE_LEFT || m == CC_MODE_FLYING_BIRDS || m == CC_MODE_WAVE ||
           m == CC_MODE_SPIRAL || m == CC_MODE_WIND || m == CC_MODE_LOVE ||
           m == CC_MODE_TEMP;
  }
  // Starts/ends the choreography window. Called every loop.
  void update_mode_cycle_();
  // How long the staggered fade into a choreography takes, in seconds: the
  // sweep itself plus the left-to-right stagger across the wall. The
  // choreography's own clock is held at zero for this long so every hand
  // reaches the rest pose BEFORE anything starts moving.
  double mode_entry_lead_s_() const;

  // rendering: draws the current state onto `this->obj` (owned lv_canvas_t)
  void render_();
  // Clears the canvas at the start of each frame: to `background_` normally,
  // or to fully transparent when transparent_ (needs an ARGB8888 canvas).
  void fill_bg_();
  void canvas_clockclock_(int w, int h);
  // `partial:` - draws a cols x rows block of the 24 clocks starting at
  // `first`, filling the canvas. 1x1 = one clock, 2x3 = one digit.
  void canvas_clockclock_cells_(int w, int h, int first, int cols, int rows);
  // The drawing itself, into any layer at any offset - shared by the canvas
  // and direct-draw paths.
  void draw_cells_(lv_layer_t *layer, int x0, int y0, int w, int h, int first, int cols, int rows);
  // Whatever clockclock24 should show, at an offset: the full 8x3 grid or the
  // `partial:` selection.
  void draw_clockclock_(lv_layer_t *layer, int x0, int y0, int w, int h);
  void draw_grid_(lv_layer_t *layer, int x0, int y0, int w, int h);
  // True when the dot should be lit right now: enabled, out of sync, and
  // inside the first 120 ms of the wall-clock second. See set_sync_dot.
  bool sync_dot_on_() const;
  void draw_sync_dot_(lv_layer_t *layer, int cx, int cy, int r);
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
  bool direct_{false};
  bool draw_cb_attached_{false};
  bool grayscale_{false};
  uint32_t render_interval_ms_{16};
  uint32_t last_render_ms_{0};
  int canvas_w_{0};
  int canvas_h_{0};

  // clockclock24
  int hand_width_{1};
  uint32_t transition_ms_{2000};
  float spacing_{0.0f};
  int pad_inside_{0};
  int pad_outside_{0};
  MovementMode movement_{CC_MOVE_OPPOSITE};
  float mode_speed_{1.0f};
  ClockMode mode_{CC_MODE_TIME};
  // Animation time base - see anim_clock_(). Never wrapped: a double holds
  // seconds at sub-microsecond resolution for longer than any of this will
  // run, and wrapping it would put a discontinuity into every animation whose
  // period does not divide the wrap exactly.
  double anim_t_{0.0};
  uint32_t anim_last_ms_{0};
  bool anim_started_{false};
  // Mode-entry blend - see blend_into_mode_(). PENDING means the mode has just
  // changed and we are waiting for the choreography's first frame so we can
  // measure how far each hand has to travel; ACTIVE means that offset is being
  // faded out. Runs over transition_length, the same sweep time a digit change
  // uses, so entering an animation looks like every other move the clock makes.
  enum BlendState : uint8_t { BLEND_NONE = 0, BLEND_PENDING, BLEND_ACTIVE };
  BlendState blend_state_{BLEND_NONE};
  // Per-column start delay for the CURRENT sweep, in ms. 0 = every hand moves
  // together, which is the normal digit change; set only when settling back
  // out of a choreography, and cleared when that sweep finishes.
  uint32_t anim_stagger_ms_{0};
  // Which choreography we are settling OUT of, or CC_MODE_TIME when we are
  // not. While it is set the animation keeps running underneath the settle,
  // and each hand is drawn at (animation + a delta that shrinks to zero) - so
  // the wall winds down out of the movement instead of freezing and then
  // sweeping. settle_prev_ tracks the animation frame to frame so the delta
  // can follow it without ever having to re-pick a direction mid-fade.
#ifdef USE_SENSOR
  sensor::Sensor *temp_sensor_{nullptr};
#endif
  int adopted_temp_{TEMP_NONE};
  // The value currently DRAWN, which is not always the latest one: a reading
  // that lands mid-sweep is held back until the hands have arrived, so the
  // face never jumps from one number to another. See tick_temp_().
  int shown_temp_{TEMP_NONE};
  // Seconds left in the current choreography window, or effectively forever
  // when the mode was set by an action rather than the cycle. tick_temp_()
  // checks it before starting a sweep to a new reading.
  double cycle_left_s_{1e9};
  ClockMode settle_from_{CC_MODE_TIME};
  float settle_delta_[NUM_HANDS];
  float settle_prev_[NUM_HANDS];
  // Epoch second the current choreography window opened, or 0 when no
  // window is running. The choreographies take their phase relative to this so
  // one always begins at its rest position exactly as the window opens - see
  // update_mode_cycle_(). Every node computes the same value from the shared
  // clock, so the wall stays in phase.
  double anim_phase_base_{0.0};
  uint32_t blend_start_{0};
  // Doubles as scratch: holds each hand's position at mode change until the
  // first frame turns it into an offset.
  float blend_off_[NUM_HANDS];
  // Mode cycle. interval 0 = disabled.
  uint32_t cycle_interval_s_{0};
  std::vector<ClockMode> cycle_modes_;
  bool cycle_active_{false};
  // Set by a listening sync platform - see set_mode_follower().
  bool mode_follower_{false};
  // What to go back to when the window closes. While a window is open,
  // set_mode() records here instead of taking effect.
  ClockMode base_mode_{CC_MODE_TIME};
  int digits_[NUM_DIGITS]{-1, -1, -1, -1};
  int last_key_{-1};
  float cur_[NUM_HANDS];
  float start_[NUM_HANDS];
  float target_[NUM_HANDS];
  bool animating_{false};
  // Per-transition render timing, reported when the sweep finishes. Counts
  // only our own canvas drawing - LVGL's flush of that canvas to the panel
  // happens in its own pass and is not included here (watch `loop_time` for
  // the whole picture).
  uint32_t render_us_total_{0};
  uint32_t render_us_max_{0};
  uint32_t render_frames_{0};
  // clockclock24 only: set by anything that moves a hand, cleared once the
  // frame is drawn. Between minute changes the face is completely static, so
  // without this the widget re-blits an identical 240x240 panel over SPI at
  // the full render_interval - which is what makes a C3 look "slow".
  // Starts true so the parked hands get drawn once at boot.
  bool cc_dirty_{true};
  uint32_t anim_start_{0};
  uint32_t startup_align_ms_{0};
  bool startup_aligned_{false};
  bool sync_dot_{false};
  bool sync_dot_last_{false};
  bool synced_{true};
  uint32_t demo_interval_ms_{5000};
  int demo_step_{1};
  uint32_t demo_last_ms_{0};
  int demo_min_{0};
  // -1 = draw the full 8x3 grid. Otherwise the index of what to draw, centred
  // and scaled to the whole canvas: a mini-clock (0-23), or a digit (0-3) when
  // partial_digit_ is set.
  int partial_{-1};
  bool partial_digit_{false};

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
