#include "sync_time.h"

#ifdef USE_LVGL_CLOCK_SYNC

#include "esphome/core/log.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/time.h>
#include <cmath>

namespace esphome {
namespace lvgl_clock {

static const char *const TAG = "lvgl_clock.sync";

// Anything before 2019 is a device that has never had the time set.
static const uint32_t MIN_VALID_EPOCH = 1546300800;
// How often the throttled bus-health line is logged, on either role.
static const uint32_t STATUS_INTERVAL_MS = 10000;
// How long without a time-carrying packet before a listener calls itself out
// of sync and puts the dots back up. The master broadcasts every second by
// default, so this is ten missed packets - long enough not to flicker on a
// single dropped line, short enough to notice before the clock visibly drifts.
static const uint32_t SYNC_TIMEOUT_MS = 10000;

void SyncTime::setup() {
  ESP_LOGCONFIG(TAG, "Setting up %s time sync...", this->broadcast_ ? "master" : "slave");
  // On a multi-panel master the same rule applies within the board: widget 0 is
  // the picker and the rest are told, so the three panels cannot disagree about
  // which choreography is running either. update() pushes the mode across.
  for (size_t i = 1; i < this->clocks_.size(); i++)
    this->clocks_[i]->set_mode_follower(true);

  if (!this->broadcast_) {
    // A listener starts out of sync, so its panels show the sync dot until a
    // packet carrying real time arrives. The master leaves its widgets alone -
    // it *is* the time source, so it never shows a dot. That is also why the
    // widget defaults to synced: anything without a bus stays dark.
    this->set_widgets_synced_(false);
    // ...and it never picks its own choreography. `cycle_modes:` can sit in
    // the shared config; only the master acts on it, and its choice reaches
    // this node in the mode field of the sync packet like any other mode
    // change. Two independent pickers is how a wall ends up showing eight
    // different animations at once.
    for (auto *clock : this->clocks_)
      clock->set_mode_follower(true);
  }
}

void SyncTime::set_widgets_synced_(bool synced) {
  if (synced == this->widgets_synced_)
    return;
  this->widgets_synced_ = synced;
  for (auto *clock : this->clocks_)
    clock->set_synced(synced);
}

void SyncTime::update() {
  if (!this->broadcast_)
    return;
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  // Before SNTP lands there is no time worth sending - the slaves are better
  // off on their own than being handed 1970 - but the *mode* still has to go
  // out, or the wall would not share its boot animation: the master would spin
  // while the slaves sat in whatever `clock_mode` they were compiled with.
  // So send epoch 0, which the slave reads as "mode only, keep your clock".
  // This is also what makes `mode: demo` work on the bench with no network at
  // all: demo_min rides the same mode-only packet.
  const bool time_valid = (uint32_t) tv.tv_sec >= MIN_VALID_EPOCH;
  const uint32_t epoch = time_valid ? (uint32_t) tv.tv_sec : 0;
  const uint32_t ms = time_valid ? (uint32_t) (tv.tv_usec / 1000) : 0;
  if (!time_valid)
    this->suppressed_++;

  LvglClock *primary = this->primary_clock_();
  int mode = primary != nullptr ? (int) primary->get_mode() : 0;
  // In demo mode the displayed time is a fake minute counter, not the clock,
  // so send that too - otherwise every node counts its own and the wall shows
  // 24 different times.
  int demo_min = (primary != nullptr && mode == (int) CC_MODE_DEMO) ? primary->get_demo_min() : -1;

  // Mirror the picker onto this board's other panels, exactly as a slave does
  // with what arrives on the wire - so all three agree even mid-choreography.
  for (size_t i = 1; i < this->clocks_.size(); i++) {
    this->clocks_[i]->set_mode((ClockMode) mode);
    if (mode == (int) CC_MODE_DEMO && demo_min >= 0)
      this->clocks_[i]->adopt_demo_min(demo_min);
  }
  // The wall's temperature, from the master's sensor. TEMP_NONE when there is
  // no sensor or it has not published - the slaves then know to skip `temp`
  // rather than draw a stale or empty face.
  int temp = TEMP_NONE;
#ifdef USE_SENSOR
  if (this->temp_sensor_ != nullptr && this->temp_sensor_->has_state() &&
      !std::isnan(this->temp_sensor_->state))
    temp = (int) lroundf(this->temp_sensor_->state);
#endif
  // The master's own panels read it from here too, so master and slaves are
  // fed by exactly the same path.
  for (auto *clock : this->clocks_)
    clock->adopt_temperature(temp);
  this->last_temp_ = temp;

  char out[48];
  int n = snprintf(out, sizeof(out), "CC24 %u %u %d %d %d\n", (unsigned) epoch, (unsigned) ms, mode,
                   demo_min, temp);
  if (n <= 0)
    return;

  // Send the time we expect it to be when the packet LANDS, not when we
  // sampled it. The slave applies the timestamp on the newline - the last byte
  // of the line - so everything we are about to shift out is pure latency, and
  // uncompensated it makes every listener run exactly that far behind the
  // master. At 115200 8N1 a 25-byte line is ~2.2 ms; small, but it is a fixed
  // bias in one direction, which is precisely the kind that accumulates into a
  // visible offset across a wall.
  //
  // 10 bits on the wire per byte: 8 data + start + stop.
  uint32_t baud = this->parent_ != nullptr ? this->parent_->get_baud_rate() : 0;
  uint32_t tx_us = baud != 0 ? (uint32_t) ((uint64_t) n * 10ull * 1000000ull / baud) : 0;
  if (time_valid && tx_us != 0) {
    uint64_t total_us = (uint64_t) ms * 1000ull + tx_us;
    uint32_t adj_epoch = epoch + (uint32_t) (total_us / 1000000ull);
    uint32_t adj_ms = (uint32_t) ((total_us % 1000000ull) / 1000ull);
    // Re-format with the corrected stamp. The length can shift by a digit,
    // which moves the wire time by <0.1 ms - far below the jitter this is
    // correcting for, so one pass is enough.
    int n2 = snprintf(out, sizeof(out), "CC24 %u %u %d %d %d\n", (unsigned) adj_epoch,
                      (unsigned) adj_ms, mode, demo_min, temp);
    if (n2 > 0)
      n = n2;
  }
  this->write_array((const uint8_t *) out, (size_t) n);
  this->tx_count_++;
  // Whether this was the scheduled tick or a mode change firing early, record
  // what went out so loop() only triggers on an actual change.
  this->last_tx_mode_ = mode;
  // Per-packet at VERBOSE (one a second is a lot), plus a throttled summary at
  // DEBUG so `logger: level: DEBUG` is enough to see the bus is alive.
  ESP_LOGV(TAG, "TX: %.*s", n - 1, out);
  if (this->last_status_ms_ == 0 || millis() - this->last_status_ms_ >= STATUS_INTERVAL_MS) {
    this->last_status_ms_ = millis();
    if (time_valid) {
      ESP_LOGD(TAG, "TX ok (%u sent), mode %s: %.*s", (unsigned) this->tx_count_,
               clock_mode_name((ClockMode) mode), n - 1, out);
    } else {
      ESP_LOGW(TAG, "TX mode-only (%u sent, no system time yet), mode %s - waiting for SNTP: %.*s",
               (unsigned) this->tx_count_, clock_mode_name((ClockMode) mode), n - 1, out);
    }
  }
}

void SyncTime::loop() {
  if (this->broadcast_) {
    // Send the moment the mode changes, instead of waiting for the next tick.
    //
    // This is the big one for "the master starts early". The master switches
    // choreography the instant the window opens; a slave that only hears about
    // it on the next scheduled broadcast starts up to a whole
    // broadcast_interval later - 1000 ms by default, against ~2 ms of wire
    // time. No amount of timestamp correction touches that, because the packet
    // simply has not been sent yet.
    LvglClock *primary = this->primary_clock_();
    if (primary != nullptr && (int) primary->get_mode() != this->last_tx_mode_) {
      ESP_LOGI(TAG, "TX mode -> %s", clock_mode_name(primary->get_mode()));
      this->update();
    }
    return;
  }
  while (this->available()) {
    uint8_t c;
    if (!this->read_byte(&c))
      break;
    this->rx_bytes_++;
    if (c == '\n' || c == '\r') {
      if (this->rx_len_ > 0) {
        this->rx_buf_[this->rx_len_] = '\0';
        ESP_LOGV(TAG, "RX: %s", this->rx_buf_);
        this->handle_line_();
        this->rx_len_ = 0;
      }
      continue;
    }
    if (this->rx_len_ + 1 >= sizeof(this->rx_buf_)) {
      // Overlong line: drop it rather than wrapping into a half-parsed packet.
      // Usually means a baud mismatch or a floating RX pin, so it is worth a
      // (throttled) complaint rather than a silent reset.
      this->rx_len_ = 0;
      this->bad_lines_++;
      ESP_LOGW(TAG, "RX overlong line, dropped (%u bad)", (unsigned) this->bad_lines_);
      continue;
    }
    this->rx_buf_[this->rx_len_++] = (char) c;
  }

  uint32_t now = millis();

  // Fall back out of sync if the master goes quiet: the clock keeps running on
  // the local oscillator and will drift, so the panels put their dots back up
  // rather than quietly showing a time nobody is correcting any more.
  if (this->widgets_synced_ && now - this->last_time_rx_ms_ >= SYNC_TIMEOUT_MS) {
    this->set_widgets_synced_(false);
    ESP_LOGW(TAG, "Out of sync: no time packet for %u ms", (unsigned) (now - this->last_time_rx_ms_));
  }

  // Heartbeat: distinguishes "nothing on the wire" from "bytes arriving but
  // unusable" - the same symptom on the display, very different wiring fault.
  if (this->last_status_ms_ == 0 || now - this->last_status_ms_ >= STATUS_INTERVAL_MS) {
    this->last_status_ms_ = now;
    if (this->packets_ == 0) {
      ESP_LOGW(TAG, "RX no valid packets yet (%u bytes, %u bad lines) - check TX->RX wiring, "
                    "common ground and that the master has SNTP time",
               (unsigned) this->rx_bytes_, (unsigned) this->bad_lines_);
    } else if (now - this->last_rx_ms_ >= STATUS_INTERVAL_MS) {
      ESP_LOGW(TAG, "RX stalled: last packet %u ms ago (%u total, %u bad lines)",
               (unsigned) (now - this->last_rx_ms_), (unsigned) this->packets_,
               (unsigned) this->bad_lines_);
    } else {
      ESP_LOGD(TAG, "RX ok (%u packets, %u bytes, %u bad lines), mode %s",
               (unsigned) this->packets_, (unsigned) this->rx_bytes_, (unsigned) this->bad_lines_,
               this->last_rx_mode_ >= 0 ? clock_mode_name((ClockMode) this->last_rx_mode_)
                                        : "none yet");
    }
  }
}

void SyncTime::handle_line_() {
  if (strncmp(this->rx_buf_, "CC24 ", 5) != 0) {
    // Garbled framing is the usual signature of a baud mismatch or a shared
    // pin, so show the offending bytes instead of dropping them quietly.
    this->bad_lines_++;
    ESP_LOGW(TAG, "RX bad prefix, ignored: '%s'", this->rx_buf_);
    return;
  }
  char *p = this->rx_buf_ + 5;
  char *end = nullptr;
  uint32_t epoch = (uint32_t) strtoul(p, &end, 10);
  if (end == p) {
    this->bad_lines_++;
    ESP_LOGW(TAG, "RX no epoch field: '%s'", this->rx_buf_);
    return;
  }
  p = end;
  uint32_t ms = (uint32_t) strtoul(p, &end, 10);
  if (end == p) {
    this->bad_lines_++;
    ESP_LOGW(TAG, "RX no ms field: '%s'", this->rx_buf_);
    return;
  }
  p = end;
  int mode = (int) strtol(p, &end, 10);
  if (end == p)
    mode = -1;
  p = end;
  // Optional 4th field: the master's demo-mode minute counter (-1 = not in
  // demo mode). Older/shorter packets simply lack it.
  int demo_min = (int) strtol(p, &end, 10);
  if (end == p)
    demo_min = -1;
  p = end;
  // Optional 5th field: the master's temperature in whole degrees. Absent from
  // an older master's packets, which simply reads as "no reading".
  int temp = (int) strtol(p, &end, 10);
  if (end == p)
    temp = TEMP_NONE;

  // epoch 0 is the master saying "I have no time yet, but here is the mode" -
  // that is how the boot animation reaches the wall before SNTP lands, and how
  // demo mode works with no network at all. Anything else below the threshold
  // is garbage rather than a deliberate sentinel.
  const bool has_time = epoch >= MIN_VALID_EPOCH;
  if ((epoch != 0 && !has_time) || ms > 999) {
    this->bad_lines_++;
    ESP_LOGW(TAG, "RX implausible time, ignored: epoch=%u ms=%u", (unsigned) epoch, (unsigned) ms);
    return;
  }

  if (has_time) {
    // Set the clock ourselves rather than going through synchronize_epoch_():
    // that helper ignores corrections under +-1s and only ever sets whole
    // seconds, which would leave neighbouring nodes up to a second out of phase
    // - visible as one clock in the wall flipping its digit late.
    struct timeval tv;
    tv.tv_sec = (time_t) epoch;
    tv.tv_usec = (suseconds_t) (ms * 1000);
    settimeofday(&tv, nullptr);
  }

  this->packets_++;
  this->last_rx_ms_ = millis();
  if (has_time) {
    // Only a packet carrying real time counts as sync - a mode-only packet
    // means the master itself is still waiting on SNTP, so the wall is
    // animating together but nobody knows the time yet.
    this->last_time_rx_ms_ = millis();
    this->set_widgets_synced_(true);
    if (!this->synced_) {
      this->synced_ = true;
      ESP_LOGI(TAG, "Time synced from master: %u.%03u", (unsigned) epoch, (unsigned) ms);
      this->time_sync_callback_.call();
    }
  }

  if (temp != this->last_temp_) {
    this->last_temp_ = temp;
    ESP_LOGD(TAG, "RX temperature -> %d C", temp);
  }
  for (auto *clock : this->clocks_)
    clock->adopt_temperature(temp);

  // set_mode() is a no-op when the mode is unchanged, so this is safe to call
  // on every packet. Every widget on this node gets it: on a multi-panel board
  // the bus is the only thing that moves them.
  if (mode >= 0 && mode <= (int) CC_MODE_LAST) {
    if (mode != this->last_rx_mode_) {
      this->last_rx_mode_ = mode;
      ESP_LOGI(TAG, "RX mode -> %s", clock_mode_name((ClockMode) mode));
    }
    for (auto *clock : this->clocks_) {
      clock->set_mode((ClockMode) mode);
      if (mode == (int) CC_MODE_DEMO && demo_min >= 0)
        clock->adopt_demo_min(demo_min);
    }
  }
}

void SyncTime::dump_config() {
  ESP_LOGCONFIG(TAG, "LvglClock UART time sync:");
  ESP_LOGCONFIG(TAG, "  Role: %s", this->broadcast_ ? "master (broadcasting)" : "slave (listening)");
  if (this->broadcast_) {
    ESP_LOGCONFIG(TAG, "  Sent: %u (%u mode-only, before SNTP)", (unsigned) this->tx_count_,
                  (unsigned) this->suppressed_);
    uint32_t baud = this->parent_ != nullptr ? this->parent_->get_baud_rate() : 0;
    if (baud != 0) {
      ESP_LOGCONFIG(TAG, "  Wire-time compensation: ~%u us at %u baud",
                    (unsigned) (25u * 10u * 1000000u / baud), (unsigned) baud);
    }
  } else {
    ESP_LOGCONFIG(TAG, "  Synced: %s (%u packets)", YESNO(this->synced_), (unsigned) this->packets_);
    ESP_LOGCONFIG(TAG, "  Received: %u bytes, %u bad lines", (unsigned) this->rx_bytes_,
                  (unsigned) this->bad_lines_);
  }
  ESP_LOGCONFIG(TAG, "  Mode mirroring: %s (%u widget(s))", YESNO(!this->clocks_.empty()),
                (unsigned) this->clocks_.size());
  LvglClock *primary = this->primary_clock_();
  if (primary != nullptr)
    ESP_LOGCONFIG(TAG, "  Mode now: %s", clock_mode_name(primary->get_mode()));
  time::RealTimeClock::dump_config();
}

}  // namespace lvgl_clock
}  // namespace esphome

#endif  // USE_LVGL_CLOCK_SYNC
