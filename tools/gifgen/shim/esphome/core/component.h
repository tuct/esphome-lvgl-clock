#pragma once
#include <cstdint>
// Minimal stand-ins for the esphome runtime bits lvgl_clock needs: the
// Component base class, the setup_priority constant, and millis(). millis() is
// defined by the harness (main.cpp) as a controllable virtual clock.
namespace esphome {

namespace setup_priority {
static const float DATA = 0.6f;
}  // namespace setup_priority

class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0.0f; }
};

uint32_t millis();

}  // namespace esphome
