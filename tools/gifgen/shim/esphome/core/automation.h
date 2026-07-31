#pragma once
// Minimal Action base so the SetModeAction template in lvgl_clock.h
// compiles. The harness never instantiates actions.
namespace esphome {
template<typename... Ts> class Action {
 public:
  virtual ~Action() = default;
  virtual void play(Ts... x) = 0;
};
}  // namespace esphome
