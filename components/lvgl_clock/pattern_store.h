#pragma once

#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include <cstdint>
#include <cstddef>
#include <string>

namespace esphome {
namespace lvgl_clock {

// Motion patterns: 24 per-clock specs, authored in tools/clockclock24-sim and
// carried to every board over the sync bus.
//
// A pattern is a POSE plus a MOTION for each of the 24 clocks. Both hands get a
// direction (-1 left, 0 still, +1 right) and a speed, and every hand is drawn
// at `pose + dir * speed * PATTERN_MAX_RATE * t` - continuous by construction,
// so a pattern can never make a hand jump however it was authored.
//
// The editor also lets a speed be written as "the same as my neighbour, plus or
// minus a bit". That is an authoring convenience and NOT on the wire: the
// Python side resolves every chain down to a plain number before the firmware
// ever sees it. The wall only needs the final speeds, and resolving on-device
// would mean shipping a graph walker and a cycle breaker to eight boards for no
// gain.
static const int PATTERN_MAX_PER_NODE = 8;
// Speed 1.0 in degrees per second - the same constant the editor uses, so a
// pattern looks the same on the wall as it did in the browser.
static const float PATTERN_MAX_RATE = 90.0f;

struct PatternClock {
  // Home pose, degrees, 0..359.
  uint16_t h0;
  uint16_t h1;
  // -1 counter-clockwise, 0 still, +1 clockwise.
  int8_t dir0;
  int8_t dir1;
  // Speed as a percentage of PATTERN_MAX_RATE, 0..100. A byte is plenty: 1%
  // is 0.9 deg/s, well under what anyone can see on a 32 mm dial.
  uint8_t v0;
  uint8_t v1;
};

// 24 clocks. Kept as a plain array rather than referencing NUM_CLOCKS from
// lvgl_clock.h so this header stays independent of the widget.
static const int PATTERN_CLOCKS = 24;

struct Pattern {
  char name[16];
  PatternClock clocks[PATTERN_CLOCKS];
  // Bit per clock, so a half-received pattern is never drawn. A pattern is
  // usable only once all 24 have arrived.
  uint32_t got;
  bool complete() const { return this->got == (1u << PATTERN_CLOCKS) - 1u; }
};

// One store per node. The master fills it at boot from what was baked in at
// codegen; a slave fills it from the bus. Both then draw from the same place.
class PatternStore {
 public:
  int count() const { return this->count_; }
  const Pattern *get(int i) const {
    return (i >= 0 && i < this->count_ && this->slots_[i].complete()) ? &this->slots_[i] : nullptr;
  }

  // Master side: define a slot outright.
  void set_name(int slot, const char *name);
  void set_clock(int slot, int clock, const PatternClock &c);

  // Wire format, one line per clock plus a header, so a whole pattern never
  // has to fit in one RX buffer:
  //
  //     CCPN <slot> <name>\n
  //     CCPC <slot> <clock> <h0> <h1> <d0> <d1> <v0> <v1>\n   x24
  //
  // Line-per-clock also means a lost byte costs one clock rather than the whole
  // pattern, and the next repeat quietly fixes it.
  //
  // Returns false if the line was not ours or did not parse.
  bool parse_line(const char *line);
  // Formats clock `clock` of `slot` into `out`. Returns the length, or 0.
  int format_clock(int slot, int clock, char *out, size_t out_len) const;
  int format_name(int slot, char *out, size_t out_len) const;

  // ---- the text-entity form -------------------------------------------------
  //
  // A pattern as JSON is ~5 kB; a Home Assistant text entity holds 255
  // characters. So the entity form is PACKED, five bytes per clock:
  //
  //   0  h0, in 1.5 deg steps (0..239)
  //   1  h1
  //   2  (dir0+1) << 2 | (dir1+1)
  //   3  v0, 0..100
  //   4  v1
  //
  // 24 clocks = 120 bytes = 160 base64 characters, with room left for the
  // name. The 1.5 deg quantisation is ten times finer than the editor's 15 deg
  // snap, so nothing you can author is lost.
  //
  // "<name>:<base64>". Returns "" for an empty slot.
  std::string to_text(int slot) const;
  // Returns an empty string on success, or a reason.
  std::string from_text(int slot, const std::string &text);

  // Everything, for ESPPreferences. Fixed size so a saved blob stays readable
  // across builds as long as PATTERN_* do not change.
  static const size_t PACKED_BYTES = PATTERN_CLOCKS * 5;
  // "<name>:<160 base64 chars>" plus a NUL. 192 leaves headroom for the name.
  static const size_t TEXT_BYTES = 192;

  // Flash-backed, so the wall keeps its patterns with Home Assistant off.
  void save();
  void load();

 protected:
  struct PackedPrefs {
    char slots[PATTERN_MAX_PER_NODE][TEXT_BYTES];
  };

  Pattern slots_[PATTERN_MAX_PER_NODE]{};
  int count_{0};
  ESPPreferenceObject pref_;
};

// The node's store. A singleton because the widgets, the sync platform and the
// codegen-generated setup code all need the same one, and threading a pointer
// through three layers of ESPHome config would buy nothing.
PatternStore &pattern_store();

}  // namespace lvgl_clock
}  // namespace esphome
