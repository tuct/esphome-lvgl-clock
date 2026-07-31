#pragma once
// Minimal ESPTime / RealTimeClock stand-in. The harness sets `now_val` on a
// RealTimeClock instance to feed the clock a controllable time.
namespace esphome {

struct ESPTime {
  int hour{0};
  int minute{0};
  int second{0};
  bool valid{true};
  bool is_valid() const { return this->valid; }
};

namespace time {
class RealTimeClock {
 public:
  ESPTime now() { return this->now_val; }
  ESPTime now_val{};
};
}  // namespace time
}  // namespace esphome
