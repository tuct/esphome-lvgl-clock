// Headless LVGL harness that renders the real lvgl_clock component to an
// in-memory canvas and dumps one PPM per frame. No ESPHome, no SDL - the
// component's C++ is compiled unchanged against desktop LVGL (see shim/).
//
// Usage: gifgen <style> <W> <H> <frames> <dt_ms> <outdir>
//   style: clockclock24 | analog | digital | flipclock
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "lvgl.h"
#include "lvgl_clock.h"

using esphome::Color;
using esphome::lvgl_clock::LvglClock;
namespace dc = esphome::lvgl_clock;

// --- controllable virtual clock (the component calls esphome::millis()) ------
static uint32_t g_millis = 0;
namespace esphome {
uint32_t millis() { return g_millis; }
}  // namespace esphome

// Expose the protected render entry points.
struct Harness : public LvglClock {
  void step() { this->loop(); }  // ticks animation + renders into the canvas
};

static void write_ppm(const char *path, lv_draw_buf_t *db, int w, int h) {
  FILE *f = fopen(path, "wb");
  if (!f) {
    fprintf(stderr, "cannot open %s\n", path);
    exit(1);
  }
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  const uint8_t *data = db->data;
  uint32_t stride = db->header.stride;
  lv_color_format_t cf = (lv_color_format_t) db->header.cf;
  std::string row;
  row.resize(w * 3);
  for (int y = 0; y < h; y++) {
    const uint8_t *line = data + (size_t) y * stride;
    for (int x = 0; x < w; x++) {
      uint8_t r, g, b;
      if (cf == LV_COLOR_FORMAT_RGB565) {
        uint16_t px = ((const uint16_t *) line)[x];
        uint8_t r5 = (px >> 11) & 0x1f, g6 = (px >> 5) & 0x3f, b5 = px & 0x1f;
        r = (r5 << 3) | (r5 >> 2);
        g = (g6 << 2) | (g6 >> 4);
        b = (b5 << 3) | (b5 >> 2);
      } else {  // ARGB8888 / XRGB8888: stored B,G,R,A
        const uint8_t *p = line + x * 4;
        b = p[0];
        g = p[1];
        r = p[2];
      }
      row[x * 3 + 0] = r;
      row[x * 3 + 1] = g;
      row[x * 3 + 2] = b;
    }
    fwrite(row.data(), 1, row.size(), f);
  }
  fclose(f);
}

int main(int argc, char **argv) {
  std::string style = argc > 1 ? argv[1] : "clockclock24";
  int W = argc > 2 ? atoi(argv[2]) : 480;
  int H = argc > 3 ? atoi(argv[3]) : 320;
  int frames = argc > 4 ? atoi(argv[4]) : 120;
  int dt = argc > 5 ? atoi(argv[5]) : 33;
  std::string outdir = argc > 6 ? argv[6] : "frames";

  lv_init();

  // A display is needed for LVGL's draw context; we never flush it (we read the
  // canvas buffer directly), so a tiny throwaway buffer + no-op flush is fine.
  lv_display_t *disp = lv_display_create(W, H);
  lv_display_set_flush_cb(disp, [](lv_display_t *d, const lv_area_t *, uint8_t *) {
    lv_display_flush_ready(d);
  });
  (void) disp;
  // No lv_display_set_buffers / lv_timer_handler: we never flush the display,
  // we render straight into the canvas's own draw buffer and read it back.

  // The clock's canvas, allocated exactly like the ESPHome codegen does.
  lv_obj_t *canvas = lv_canvas_create(lv_screen_active());
  lv_obj_set_size(canvas, W, H);

  bool transparent = false;
  lv_color_format_t cf = transparent ? LV_COLOR_FORMAT_ARGB8888 : LV_COLOR_FORMAT_NATIVE;
  uint32_t buf_size = LV_DRAW_BUF_SIZE(W, H, cf);
  static lv_draw_buf_t draw_buf;
  lv_draw_buf_init(&draw_buf, W, H, cf, 0, malloc(buf_size), buf_size);
  lv_canvas_set_draw_buf(canvas, &draw_buf);

  esphome::time::RealTimeClock rtc;
  rtc.now_val = {12, 34, 0, true};  // start time (clockclock24 sweeps 12:34 -> 12:39)

  Harness clock;
  clock.set_obj(canvas);
  clock.set_canvas_size(W, H);
  clock.set_time(&rtc);
  clock.set_foreground(Color(0xF5, 0xF5, 0xF5));
  clock.set_background(Color(0x10, 0x12, 0x18));
  clock.set_render_interval(0);  // render on every step()
  clock.set_transparent(transparent);

  // "<style>_12h" variants render 12-hour mode without seconds (AM/PM shown).
  bool is12h = false;
  std::string base = style;
  if (style == "digital_12h") {
    base = "digital";
    is12h = true;
  } else if (style == "flipclock_12h") {
    base = "flipclock";
    is12h = true;
  }

  if (base == "clockclock24") {
    clock.set_style(dc::STYLE_CLOCKCLOCK24);
    clock.set_movement(dc::CC_MOVE_LONG);
    clock.set_spacing(0.0f);
    clock.set_hand_width(3);
    clock.set_transition_length(1500);
    // little-clock faces (cc_faces = 12/12/14%), matching example_clockclock24
    clock.set_show_face(true);
    clock.set_face_fill_color(Color(30, 30, 36));
    clock.set_face_border_color(Color(30, 30, 36));
    // stays in the default time mode; the frame loop drives rtc (12:34 -> 12:40)
  } else if (base == "analog") {
    // mirrors example_analog.yaml (white hands, grey ticks, red line second)
    clock.set_style(dc::STYLE_ANALOG);
    clock.set_show_seconds(true);
    clock.set_foreground(Color(0xFF, 0xFF, 0xFF));
    clock.set_background(Color(0x00, 0x00, 0x00));
    clock.set_show_face(false);
    clock.set_minute_ticks(true);
    clock.set_minute_ticks_color(Color(102, 102, 102));   // cc_ticks_minor (40%)
    clock.set_minute_ticks_rounded(false);
    clock.set_minute_ticks_width(dc::TICK_SIZE_SMALL);
    clock.set_minute_ticks_length(dc::TICK_SIZE_SMALL);
    clock.set_hour_ticks(true);
    clock.set_hour_ticks_color(Color(153, 153, 153));      // cc_ticks_major (60%)
    clock.set_hour_ticks_rounded(false);
    clock.set_hour_ticks_width(dc::TICK_SIZE_LARGE);
    clock.set_hour_ticks_length(dc::TICK_SIZE_MEDIUM);
    clock.set_hour_hand_style(dc::HAND_STYLE_BATON);
    clock.set_minute_hand_style(dc::HAND_STYLE_BATON);
    clock.set_second_hand_style(dc::HAND_STYLE_LINE);
    clock.set_second_hand_color(Color(255, 51, 51));       // cc_second
    clock.set_second_extend(0.2f);
  } else if (base == "digital") {
    clock.set_style(dc::STYLE_DIGITAL);
    clock.set_show_seconds(true);
    clock.set_segment_style(dc::SEGMENT_STYLE_CLASSIC);
    clock.set_digital_blink(true);
    clock.set_foreground(Color(0xFF, 0x40, 0x30));      // red LED
    clock.set_digital_off_color(Color(0x66, 0x22, 0x1c));  // ghost segments (brighter, clearly visible)
  } else if (base == "flipclock") {
    clock.set_style(dc::STYLE_FLIPCLOCK);
    clock.set_show_seconds(true);
    clock.set_flip_font(&lv_font_montserrat_48);
    clock.set_am_pm_font(&lv_font_montserrat_24);
    clock.set_card_color(Color(0x24, 0x24, 0x28));
    clock.set_flip_duration(450);
  } else if (base == "seg_matrix") {
    clock.set_style(dc::STYLE_SEG_MATRIX);
    clock.set_segment_style(dc::SEGMENT_STYLE_CLASSIC);
    clock.set_foreground(Color(0xFF, 0x2a, 0x1a));         // red LED
    clock.set_digital_off_color(Color(0x2c, 0x0a, 0x08));  // ghost grid
    clock.set_background(Color(0x08, 0x06, 0x06));
  } else {
    fprintf(stderr, "style '%s' not wired in the harness yet\n", style.c_str());
    return 1;
  }

  if (is12h) {  // 12-hour, no seconds - the AM/PM markers appear
    clock.set_twenty_four_hour(false);
    clock.set_show_seconds(false);
  }

  clock.setup();

  // Two time drivers:
  //  - minute-sweep (clockclock24, and the 12h no-seconds variants): step the
  //    displayed minute across a short range, holding on the last for a beat.
  //    clockclock24 runs 12:34 -> 12:40; the 12h variants run 11:58 -> 12:02
  //    so the AM->PM (and hour) rollover at noon is on show.
  //  - second-tick (plain analog/digital/flipclock): advance real seconds so
  //    the second hand sweeps / seconds count (millis drives the sub-second).
  bool min_seq = (base == "clockclock24") || (base == "seg_matrix") || is12h;
  int seq_start_min = is12h ? (11 * 60 + 58) : (12 * 60 + 34);
  int seq_count = is12h ? 5 : 7;  // 11:58..12:02  |  12:34..12:40
  int start_sec = (base == "digital" || base == "flipclock") ? 5 : 0;
  const int SEQ_HOLD = 35;  // frames parked on the final minute before looping
  int seq_frames = frames - SEQ_HOLD;
  if (seq_frames < 1)
    seq_frames = frames;
  int fpm = seq_frames / seq_count;
  if (fpm < 1)
    fpm = 1;

  char path[512];
  for (int i = 0; i < frames; i++) {
    g_millis = (uint32_t) i * dt;
    lv_tick_inc(dt);
    if (min_seq) {
      int step = i / fpm;
      if (step > seq_count - 1)
        step = seq_count - 1;  // the tail holds on the last minute
      int tm = seq_start_min + step;
      rtc.now_val = {tm / 60, tm % 60, 0, true};
    } else {
      int total_s = start_sec + (int) (g_millis / 1000);
      rtc.now_val = {12, 34 + (total_s / 60), total_s % 60, true};
    }
    clock.step();
    lv_draw_buf_t *db = lv_canvas_get_draw_buf(canvas);
    snprintf(path, sizeof(path), "%s/frame_%04d.ppm", outdir.c_str(), i);
    write_ppm(path, db, W, H);
  }
  fprintf(stderr, "wrote %d frames to %s\n", frames, outdir.c_str());
  return 0;
}
