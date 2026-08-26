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

std::string SyncTime::set_pattern_text(int slot, const std::string &text) {
  std::string err = pattern_store().from_text(slot, text);
  if (!err.empty()) {
    ESP_LOGW(TAG, "Pattern %d rejected: %s", slot, err.c_str());
    return err;
  }
  pattern_store().save();
  // Start the push again from the top rather than waiting out pattern_repeat.
  // An edit you cannot see for five minutes is an edit you will assume failed.
  this->pattern_next_ms_ = millis();
  this->pattern_tx_slot_ = -1;
  return "";
}

void SyncTime::reload_patterns_from_firmware() {
  for (int slot = 0; slot < PATTERN_MAX_PER_NODE; slot++)
    pattern_store().from_text(slot, this->firmware_text_[slot]);
  pattern_store().save();
  this->pattern_next_ms_ = millis();
  this->pattern_tx_slot_ = -1;
  ESP_LOGI(TAG, "Patterns reloaded from firmware");
}

// Bumped whenever WallPrefs changes shape - an old blob read as a new struct
// is worse than no blob at all.
static const uint32_t WALL_PREF_HASH = 0x1C24C01Au;
// NVS has a finite erase count and a colour picker fires while you drag it, so
// a change is held before it is written.
static const uint32_t WALL_PREF_SETTLE_MS = 10000;

WallPrefs SyncTime::current_wall_prefs_() {
  WallPrefs p{};
  LvglClock *c = this->primary_clock_();
  if (c == nullptr)
    return p;
  p.movement = (uint8_t) c->get_movement();
  p.trans_ms = (uint16_t) c->get_transition_length();
  p.speed100 = (uint16_t) lroundf(c->get_mode_speed() * 100.0f);
  p.fg_rgb = c->get_foreground_rgb();
  p.bg_rgb = c->get_background_rgb();
  p.cycle_interval_s = c->get_cycle_interval();
  std::string modes = c->get_cycle_modes_text();
  strncpy(p.cycle_modes, modes.c_str(), sizeof(p.cycle_modes) - 1);
  return p;
}

void SyncTime::apply_wall_prefs_(const WallPrefs &p) {
  LvglClock *c = this->primary_clock_();
  if (c == nullptr)
    return;
  // Range-checked, because a blob written by a different firmware version is
  // just bytes. Anything out of range keeps the compiled-in value.
  if (p.movement <= (uint8_t) CC_MOVE_LONG)
    c->set_movement((MovementMode) p.movement);
  if (p.trans_ms > 0 && p.trans_ms <= 60000)
    c->set_transition_length(p.trans_ms);
  if (p.speed100 >= 10 && p.speed100 <= 500)
    c->set_mode_speed(p.speed100 / 100.0f);
  if (p.fg_rgb <= 0xFFFFFFu)
    c->set_foreground_rgb(p.fg_rgb);
  if (p.bg_rgb <= 0xFFFFFFu)
    c->set_background_rgb(p.bg_rgb);
  if (p.cycle_interval_s <= 24u * 3600u)
    c->set_cycle_interval(p.cycle_interval_s);
  if (p.cycle_modes[0] != '\0') {
    std::string modes(p.cycle_modes, strnlen(p.cycle_modes, sizeof(p.cycle_modes)));
    c->set_cycle_modes_text(modes);
  }
}

void SyncTime::load_wall_prefs_() {
  // Capture what the YAML compiled in BEFORE flash overwrites it.
  this->defaults_ = this->current_wall_prefs_();
  this->wall_pref_ = global_preferences->make_preference<WallPrefs>(WALL_PREF_HASH);
  WallPrefs blob{};
  if (this->wall_pref_.load(&blob)) {
    this->apply_wall_prefs_(blob);
    ESP_LOGCONFIG(TAG, "Look restored from flash: movement %s, %u ms, x%.2f, #%06x on #%06x",
                  movement_name((MovementMode) blob.movement), (unsigned) blob.trans_ms,
                  blob.speed100 / 100.0f, (unsigned) blob.fg_rgb, (unsigned) blob.bg_rgb);
  }
  this->saved_ = this->current_wall_prefs_();
  this->wall_prefs_ready_ = true;
}

void SyncTime::maybe_save_wall_prefs_() {
  if (!this->wall_prefs_ready_)
    return;
  WallPrefs now = this->current_wall_prefs_();
  if (memcmp(&now, &this->saved_, sizeof(WallPrefs)) == 0) {
    this->wall_prefs_dirty_ms_ = 0;
    return;
  }
  uint32_t ms = millis();
  if (this->wall_prefs_dirty_ms_ == 0) {
    this->wall_prefs_dirty_ms_ = ms;
    return;                                   // start the settle timer
  }
  if (ms - this->wall_prefs_dirty_ms_ < WALL_PREF_SETTLE_MS)
    return;
  this->wall_prefs_dirty_ms_ = 0;
  this->saved_ = now;
  if (this->wall_pref_.save(&now))
    ESP_LOGI(TAG, "Look saved: movement %s, %u ms, x%.2f, #%06x on #%06x",
             movement_name((MovementMode) now.movement), (unsigned) now.trans_ms,
             now.speed100 / 100.0f, (unsigned) now.fg_rgb, (unsigned) now.bg_rgb);
  else
    ESP_LOGW(TAG, "Could not save the wall's look to flash");
}

void SyncTime::reset_wall_prefs_to_firmware() {
  this->apply_wall_prefs_(this->defaults_);
  this->saved_ = this->current_wall_prefs_();
  this->wall_pref_.save(&this->saved_);
  ESP_LOGI(TAG, "Look reset to the compiled-in defaults");
}

void SyncTime::setup() {
  ESP_LOGCONFIG(TAG, "Setting up %s time sync...", this->broadcast_ ? "master" : "slave");
  // Flash wins over the compiled-in folder: a pattern edited from Home
  // Assistant should survive a reboot, and the folder is the starting point
  // rather than the authority. `reload` is how you get back to it.
  if (this->broadcast_) {
    pattern_store().load();
    // After the widgets exist, so `defaults_` is what codegen actually set.
    this->load_wall_prefs_();
  }
  if (this->broadcast_ && pattern_store().count() > 0) {
    this->pattern_next_ms_ = millis() + this->pattern_delay_ms_;
    ESP_LOGCONFIG(TAG, "%d pattern(s) loaded, first push in %.0f s, repeating every %.0f s",
                  pattern_store().count(), this->pattern_delay_ms_ / 1000.0f,
                  this->pattern_repeat_ms_ / 1000.0f);
  }
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

  // Which pattern the wall is playing. The PICKER owns this now - it has to,
  // because a cycle list can name a pattern (`fan,shear`) and only the picker
  // knows which entry the current window is on. This just reports it.
  if (primary != nullptr)
    this->pattern_slot_ = primary->get_pattern_slot();
  for (size_t i = 1; i < this->clocks_.size(); i++)
    this->clocks_[i]->set_pattern_slot(this->pattern_slot_);

  // Mirror the picker onto this board's other panels, exactly as a slave does
  // with what arrives on the wire - so all three agree even mid-choreography.
  for (size_t i = 1; i < this->clocks_.size(); i++) {
    this->clocks_[i]->set_mode((ClockMode) mode);
    if (mode == (int) CC_MODE_DEMO && demo_min >= 0)
      this->clocks_[i]->adopt_demo_min(demo_min);
  }
  // How the wall moves: the routing rule for a sweep, how long a sweep takes,
  // and the choreography speed multiplier. The picker owns all three, exactly
  // as it owns the mode, and the same values go to this board's other panels
  // and out on the wire. Every board has to agree: mode_speed in particular
  // scales the time base, so two boards on different values drift apart rather
  // than merely look different.
  int movement = primary != nullptr ? (int) primary->get_movement() : 0;
  int trans_ms = primary != nullptr ? (int) primary->get_transition_length() : 0;
  int speed100 = primary != nullptr ? (int) lroundf(primary->get_mode_speed() * 100.0f) : 100;
  // And what it is drawn in. Colours are on the wire so one automation can
  // warm the whole wall at sunset instead of eight.
  int fg_rgb = primary != nullptr ? (int) primary->get_foreground_rgb() : 0xFFFFFF;
  int bg_rgb = primary != nullptr ? (int) primary->get_background_rgb() : 0x000000;
  // Once a second, which is often enough for a settle timer and rare enough to
  // cost nothing.
  this->maybe_save_wall_prefs_();
  for (size_t i = 1; i < this->clocks_.size(); i++) {
    this->clocks_[i]->set_movement((MovementMode) movement);
    this->clocks_[i]->set_transition_length((uint32_t) trans_ms);
    this->clocks_[i]->set_mode_speed(speed100 / 100.0f);
    this->clocks_[i]->set_foreground_rgb((uint32_t) fg_rgb);
    this->clocks_[i]->set_background_rgb((uint32_t) bg_rgb);
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

  // Nine fields now. Sized for the worst case with room to spare, and the
  // truncation check matters: snprintf returns what it WOULD have written, so
  // a short buffer silently ships a line with the last field cut in half.
  char out[96];
  int n = snprintf(out, sizeof(out), "CC24 %u %u %d %d %d %d %d %d %d %d %d\n", (unsigned) epoch,
                   (unsigned) ms, mode, demo_min, temp, this->pattern_slot_, movement, trans_ms,
                   speed100, fg_rgb, bg_rgb);
  if (n <= 0 || n >= (int) sizeof(out))
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
    int n2 = snprintf(out, sizeof(out), "CC24 %u %u %d %d %d %d %d %d %d %d %d\n",
                      (unsigned) adj_epoch, (unsigned) adj_ms, mode, demo_min, temp,
                      this->pattern_slot_, movement, trans_ms, speed100, fg_rgb, bg_rgb);
    if (n2 > 0 && n2 < (int) sizeof(out))
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

// Two lines per call. loop() runs every few ms, so a 25-line pattern is out in
// well under a second while never holding the bus long enough to delay a time
// packet.
static const int PATTERN_LINES_PER_LOOP = 2;

void SyncTime::push_patterns_() {
  if (pattern_store().count() == 0)
    return;
  uint32_t now = millis();

  if (this->pattern_tx_slot_ < 0) {
    if (this->pattern_next_ms_ == 0 || (int32_t) (now - this->pattern_next_ms_) < 0)
      return;
    this->pattern_tx_slot_ = 0;
    this->pattern_tx_clock_ = -1;
    ESP_LOGI(TAG, "TX patterns: pushing %d", pattern_store().count());
  }

  char out[64];
  for (int i = 0; i < PATTERN_LINES_PER_LOOP && this->pattern_tx_slot_ >= 0; i++) {
    int n = this->pattern_tx_clock_ < 0
                ? pattern_store().format_name(this->pattern_tx_slot_, out, sizeof(out))
                : pattern_store().format_clock(this->pattern_tx_slot_, this->pattern_tx_clock_, out,
                                               sizeof(out));
    if (n > 0)
      this->write_array((const uint8_t *) out, (size_t) n);

    if (++this->pattern_tx_clock_ >= PATTERN_CLOCKS) {
      this->pattern_tx_clock_ = -1;
      if (++this->pattern_tx_slot_ >= pattern_store().count()) {
        // Done. Repeat later so a board that rebooted - or was plugged in
        // after the wall was already running - picks the patterns up without
        // anyone having to reset the master.
        this->pattern_tx_slot_ = -1;
        this->pattern_next_ms_ = now + this->pattern_repeat_ms_;
        ESP_LOGI(TAG, "TX patterns: done, next in %.0f s", this->pattern_repeat_ms_ / 1000.0f);
      }
    }
  }
}

void SyncTime::loop() {
  if (this->broadcast_) {
    this->push_patterns_();
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
  // Pattern definitions share the bus with the time. They are their own line
  // types so a node that does not understand them - an older slave - simply
  // logs an unknown prefix and carries on telling the time.
  if (pattern_store().parse_line(this->rx_buf_)) {
    this->packets_++;
    this->last_rx_ms_ = millis();
    return;
  }
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
  p = end;
  // Optional 6th field: which pattern slot `mode: pattern` should draw. Absent
  // from an older master's packets, which reads as slot 0.
  int pattern_slot = (int) strtol(p, &end, 10);
  if (end == p)
    pattern_slot = 0;
  p = end;
  // Optional 7th, 8th and 9th fields: how a sweep routes, how long it takes,
  // and the choreography speed in hundredths. All absent from an older
  // master's packets, which reads as "keep whatever this board was compiled
  // with" - the same rule every optional field before them follows.
  int movement = (int) strtol(p, &end, 10);
  const bool have_movement = end != p;
  p = end;
  int trans_ms = (int) strtol(p, &end, 10);
  const bool have_trans = end != p;
  p = end;
  int speed100 = (int) strtol(p, &end, 10);
  const bool have_speed = end != p;
  p = end;
  // Optional 10th and 11th: what the wall is drawn in, packed 0xRRGGBB.
  int fg_rgb = (int) strtol(p, &end, 10);
  const bool have_fg = end != p;
  p = end;
  int bg_rgb = (int) strtol(p, &end, 10);
  const bool have_bg = end != p;

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

  // How the wall moves. Range-checked rather than trusted: these arrive from a
  // master that may be running newer firmware than this board, and an unknown
  // movement or a nonsense speed should be ignored, not applied. The setters
  // are all no-ops when the value is unchanged, so this is safe every packet.
  if (have_movement && movement >= (int) CC_MOVE_OPPOSITE && movement <= (int) CC_MOVE_LONG) {
    if (movement != this->last_rx_movement_) {
      this->last_rx_movement_ = movement;
      ESP_LOGI(TAG, "RX movement -> %s", movement_name((MovementMode) movement));
    }
    for (auto *clock : this->clocks_)
      clock->set_movement((MovementMode) movement);
  }
  if (have_trans && trans_ms >= 0 && trans_ms <= 60000) {
    if (trans_ms != this->last_rx_trans_ms_) {
      this->last_rx_trans_ms_ = trans_ms;
      ESP_LOGI(TAG, "RX transition length -> %d ms", trans_ms);
    }
    for (auto *clock : this->clocks_)
      clock->set_transition_length((uint32_t) trans_ms);
  }
  if (have_fg && fg_rgb >= 0 && fg_rgb <= 0xFFFFFF) {
    if (fg_rgb != this->last_rx_fg_) {
      this->last_rx_fg_ = fg_rgb;
      ESP_LOGI(TAG, "RX hand colour -> #%06x", (unsigned) fg_rgb);
    }
    for (auto *clock : this->clocks_)
      clock->set_foreground_rgb((uint32_t) fg_rgb);
  }
  if (have_bg && bg_rgb >= 0 && bg_rgb <= 0xFFFFFF) {
    if (bg_rgb != this->last_rx_bg_) {
      this->last_rx_bg_ = bg_rgb;
      ESP_LOGI(TAG, "RX background -> #%06x", (unsigned) bg_rgb);
    }
    for (auto *clock : this->clocks_)
      clock->set_background_rgb((uint32_t) bg_rgb);
  }
  if (have_speed && speed100 >= 10 && speed100 <= 500) {
    if (speed100 != this->last_rx_speed100_) {
      this->last_rx_speed100_ = speed100;
      ESP_LOGI(TAG, "RX mode speed -> x%.2f", speed100 / 100.0f);
    }
    for (auto *clock : this->clocks_)
      clock->set_mode_speed(speed100 / 100.0f);
  }

  // set_mode() is a no-op when the mode is unchanged, so this is safe to call
  // on every packet. Every widget on this node gets it: on a multi-panel board
  // the bus is the only thing that moves them.
  if (mode >= 0 && mode <= (int) CC_MODE_LAST) {
    if (mode != this->last_rx_mode_) {
      this->last_rx_mode_ = mode;
      ESP_LOGI(TAG, "RX mode -> %s", clock_mode_name((ClockMode) mode));
    }
    for (auto *clock : this->clocks_) {
      // Slot before mode: entering `pattern` should draw the right one on its
      // first frame, not the previous slot for a frame and then swap.
      clock->set_pattern_slot(pattern_slot);
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
