#pragma once

#include "esphome/core/defines.h"
// External components ship every file in the folder, so this one is compiled
// even in configs that have no `uart:` at all - which would fail on the
// missing uart header. time.py defines this only when the platform is used.
// (There is no stock USE_UART define to key off.)
#ifdef USE_LVGL_CLOCK_SYNC

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/uart/uart.h"
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#include "lvgl_clock.h"
#include "pattern_store.h"

#include <string>
#include <vector>

namespace esphome {
namespace lvgl_clock {

// One-wire UART time distribution, for building a physical ClockClock 24 out
// of 24 separate displays (see digital_clock_clock_24/). One node has Wi-Fi
// and SNTP and broadcasts; the rest listen and set their clocks from it.
//
// Master (`broadcast_interval:` set) sends, every interval:
//
//     CC24 <epoch> <ms> <mode> <demo_min> <temp_c>\n
//
// Slave (no `broadcast_interval:`) parses that and sets its system clock.
//
// `<temp_c>` is the master's temperature in whole degrees, or -1000 for "no
// reading". Only the master has a sensor - the whole wall shows one number, so
// putting a sensor on every board would be 8 sensors disagreeing about the same
// room. A slave with no sensor of its own draws whatever arrives here.
//
// `<epoch> == 0` means "no time yet, but here is the mode": the master keeps
// broadcasting from boot, so the whole wall shares its spin/birds animation
// before SNTP has landed, and `mode: demo` works with no network at all. A
// slave applies the mode and leaves its clock alone.
//
// Why not just RealTimeClock::synchronize_epoch_()? It deliberately ignores
// corrections smaller than +-1s and only ever sets whole seconds. That is
// right for NTP but wrong here: the nodes must agree on the *phase* of the
// minute, not just the second, or one clock flips its digit up to a second
// after its neighbours - which on a wall of 24 reads as a fault, not drift.
// So this sets the clock itself, with the millisecond field.
// What the master remembers across a reboot.
//
// Patterns already survive one (pattern_store), and losing the rest on a power
// cut is the same annoyance: the wall comes back at compile-time white-on-black
// at x1.0 and every automation that ever set it has already run.
//
// Fixed-size POD, packed, with its own hash - NVS stores it as one blob, so a
// field added later must be APPENDED and the hash bumped, exactly like the
// wire format.
struct WallPrefs {
  uint8_t movement;
  uint16_t trans_ms;
  uint16_t speed100;
  uint32_t fg_rgb;
  uint32_t bg_rgb;
  uint32_t cycle_interval_s;
  char cycle_modes[128];
} __attribute__((packed));

class SyncTime : public time::RealTimeClock, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  // PollingComponent: fires at `broadcast_interval` on the master, never on a
  // slave (the Python side sets the interval to "never" there).
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_broadcast(bool on) { this->broadcast_ = on; }
  // Master only. How long after boot the first pattern push goes out, and how
  // often it repeats. The delay exists because the master is the only board
  // with Wi-Fi: it is up and broadcasting while the slaves are still bringing
  // up PSRAM, three SPI panels and LVGL. Patterns sent into that window are
  // simply not heard, and unlike the time - which repeats every second - a
  // pattern that is missed stays missed until the next repeat.
  void set_pattern_delay(uint32_t ms) { this->pattern_delay_ms_ = ms; }
  void set_pattern_repeat(uint32_t ms) { this->pattern_repeat_ms_ = ms; }
  // ---- runtime pattern editing ----------------------------------------------
  //
  // Read and written by template `text:` entities on the master - see
  // digital_clock_clock_24_24_round_screens/board_d.yaml. Kept here rather than
  // as a custom ESPHome platform so the entities live in YAML, where they can
  // be renamed or dropped without touching the component.
  std::string get_pattern_text(int slot) const { return pattern_store().to_text(slot); }
  // Returns "" on success or a reason. A successful write saves to flash and
  // re-pushes the whole set down the bus straight away, so the wall picks up an
  // edit within a second rather than at the next repeat.
  std::string set_pattern_text(int slot, const std::string &text);
  // Throw away runtime edits and go back to what was compiled in.
  void reload_patterns_from_firmware();

  // Called from codegen, once per clock, to bake the folder's patterns in.
  void add_pattern_name(int slot, const char *name) { pattern_store().set_name(slot, name); }
  void add_pattern_clock(int slot, int clock, uint16_t h0, uint16_t h1, int8_t d0, int8_t d1,
                         uint8_t v0, uint8_t v1) {
    PatternClock c{h0, h1, d0, d1, v0, v1};
    pattern_store().set_clock(slot, clock, c);
    // Remember the compiled-in set, so `reload` has something to go back to
    // after flash-restored or HA-written patterns have replaced it.
    if (slot < PATTERN_MAX_PER_NODE && clock == PATTERN_CLOCKS - 1)
      this->firmware_text_[slot] = pattern_store().to_text(slot);
  }
#ifdef USE_SENSOR
  // Master only: the temperature that goes out with the time, so `mode: temp`
  // shows the same reading on every board without any of them having a sensor.
  void set_temperature_sensor(sensor::Sensor *s) { this->temp_sensor_ = s; }
#endif
  // Optional: the widgets whose idle animation mode travels with the time, so
  // the whole wall spins/flies/shows time together. A node driving more than
  // one panel adds one per widget - a slave has no other way to move them,
  // since the mode only ever arrives over the bus. On a master the first one
  // is the mode that gets broadcast; they are always in the same mode anyway.
  void add_clock(LvglClock *clock) { this->clocks_.push_back(clock); }

 protected:
  // Master only: dribble the pattern definitions onto the bus, a couple of
  // lines per loop(). Sent in one burst they would be ~800 bytes per pattern,
  // which at 115200 is 70 ms of wire time that a time packet would have to
  // queue behind - and the whole point of this bus is that the time is not
  // delayed.
  void push_patterns_();

  // Parses one complete line out of rx_buf_ and applies it.
  void handle_line_();
  // Pushes the sync state onto every mirrored widget, so their sync dots come
  // up while this node is adrift and go dark once the bus is feeding it.
  void set_widgets_synced_(bool synced);

  // The widget the master reads its mode from / a slave writes it back to.
  LvglClock *primary_clock_() const { return this->clocks_.empty() ? nullptr : this->clocks_[0]; }

  bool broadcast_{false};
  std::vector<LvglClock *> clocks_;
  // "CC24 4294967295 999 3 59 -1000 7\n" is 33 bytes and a pattern line
  // "CCPC 7 23 359 359 -1 -1 100 100\n" is 32; 64 leaves room for both plus
  // slop, and there is always a NUL before parsing.
  // Matches the TX buffer in sync_time.cpp. The wire format only ever GROWS -
  // every field is appended and older nodes ignore the tail - so an RX buffer
  // smaller than what a master can send is a future in which listeners start
  // dropping whole packets as "overlong". A full 9-field line is 47 bytes; the
  // headroom is the point.
  char rx_buf_[96];
  uint8_t rx_len_{0};
  bool synced_{false};
  // Mirrors what the widgets were last told. Starts true so setup()'s
  // set_widgets_synced_(false) on a listener is not swallowed as a no-op.
  bool widgets_synced_{true};
  uint32_t last_rx_ms_{0};
  // Last packet that actually carried a time - mode-only packets don't count.
  uint32_t last_time_rx_ms_{0};
  uint32_t packets_{0};
  // Diagnostics. The two ways this bus dies silently are "master never got
  // NTP so it sends nothing" and "slave gets bytes but they are garbage", so
  // both are counted and reported by a once-a-10s status line.
  uint32_t tx_count_{0};
  uint32_t rx_bytes_{0};
  uint32_t bad_lines_{0};
  uint32_t suppressed_{0};
  uint32_t last_status_ms_{0};
  // Master only: last mode actually put on the wire. -1 so the first loop
  // broadcasts immediately rather than leaving the wall dark until the first
  // scheduled tick.
  int last_tx_mode_{-1};
  // Watched alongside the mode: picking a different pattern changes what the
  // wall draws just as much as picking a different mode does.
  int last_tx_slot_{-1};
  // Slave only: last mode taken off the wire, so a change is logged once
  // rather than on every packet. -1 = nothing received yet.
  int last_rx_mode_{-1};
  // Last movement/transition/speed seen on the wire, so the log says something
  // only when the wall actually changes rather than once a second forever.
  int last_rx_movement_{-1};
  int last_rx_trans_ms_{-1};
  int last_rx_speed100_{-1};
  // Saved look and feel. `defaults_` is what the YAML compiled in, captured
  // before flash is read, so there is a way back - the same role `Reload
  // patterns from firmware` plays for patterns.
  ESPPreferenceObject wall_pref_;
  WallPrefs saved_{};
  WallPrefs defaults_{};
  bool wall_prefs_ready_{false};
  uint32_t wall_prefs_dirty_ms_{0};
 public:
  // Throw away the saved look and go back to what the YAML compiled in. The
  // counterpart of `Reload patterns from firmware`, and needed for the same
  // reason: once flash wins, editing panel.yaml and reflashing does nothing
  // visible, which is a deeply confusing way to lose an afternoon.
  void reset_wall_prefs_to_firmware();

 protected:
  void load_wall_prefs_();
  void apply_wall_prefs_(const WallPrefs &p);
  WallPrefs current_wall_prefs_();
  void maybe_save_wall_prefs_();

  int last_rx_fg_{-1};
  int last_rx_bg_{-1};
  // Pattern push state. `pattern_tx_slot_` < 0 means "nothing to send".
  uint32_t pattern_delay_ms_{30000};
  uint32_t pattern_repeat_ms_{300000};
  uint32_t pattern_next_ms_{0};
  int pattern_tx_slot_{-1};
  // -1 = the header line is still to go, then 0..23 are the clocks.
  int pattern_tx_clock_{-1};
  // The slot the wall is currently playing, broadcast with the mode.
  int pattern_slot_{0};
  // What codegen baked in, kept so `reload` can undo a runtime edit.
  std::string firmware_text_[PATTERN_MAX_PER_NODE];
#ifdef USE_SENSOR
  sensor::Sensor *temp_sensor_{nullptr};
#endif
  // Last temperature put on / taken off the wire, for the mode-change trigger
  // and to avoid re-pushing an unchanged value.
  int last_temp_{TEMP_NONE};
};

}  // namespace lvgl_clock
}  // namespace esphome

#endif  // USE_LVGL_CLOCK_SYNC
