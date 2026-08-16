#pragma once

#include "esphome/core/defines.h"
// External components ship every file in the folder, so this one is compiled
// even in configs that have no `uart:` at all - which would fail on the
// missing uart header. time.py defines this only when the platform is used.
// (There is no stock USE_UART define to key off.)
#ifdef USE_LVGL_CLOCK_SYNC

#include "esphome/core/component.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/uart/uart.h"
#include "lvgl_clock.h"

#include <vector>

namespace esphome {
namespace lvgl_clock {

// One-wire UART time distribution, for building a physical ClockClock 24 out
// of 24 separate displays (see digital_clock_clock_24/). One node has Wi-Fi
// and SNTP and broadcasts; the rest listen and set their clocks from it.
//
// Master (`broadcast_interval:` set) sends, every interval:
//
//     CC24 <epoch> <ms> <mode> <demo_min>\n
//
// Slave (no `broadcast_interval:`) parses that and sets its system clock.
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
  // Optional: the widgets whose idle animation mode travels with the time, so
  // the whole wall spins/flies/shows time together. A node driving more than
  // one panel adds one per widget - a slave has no other way to move them,
  // since the mode only ever arrives over the bus. On a master the first one
  // is the mode that gets broadcast; they are always in the same mode anyway.
  void add_clock(LvglClock *clock) { this->clocks_.push_back(clock); }

 protected:
  // Parses one complete line out of rx_buf_ and applies it.
  void handle_line_();
  // Pushes the sync state onto every mirrored widget, so their sync dots come
  // up while this node is adrift and go dark once the bus is feeding it.
  void set_widgets_synced_(bool synced);

  // The widget the master reads its mode from / a slave writes it back to.
  LvglClock *primary_clock_() const { return this->clocks_.empty() ? nullptr : this->clocks_[0]; }

  bool broadcast_{false};
  std::vector<LvglClock *> clocks_;
  // "CC24 4294967295 999 3 59\n" is 25 bytes; leave room for slop and always
  // NUL-terminate before parsing.
  char rx_buf_[48];
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
  // Slave only: last mode taken off the wire, so a change is logged once
  // rather than on every packet. -1 = nothing received yet.
  int last_rx_mode_{-1};
};

}  // namespace lvgl_clock
}  // namespace esphome

#endif  // USE_LVGL_CLOCK_SYNC
