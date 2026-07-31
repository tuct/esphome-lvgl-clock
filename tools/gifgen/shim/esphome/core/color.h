#pragma once
#include <cstdint>
// Minimal stand-in for esphome::Color - only the r/g/b fields and the two
// constructors lvgl_clock uses.
namespace esphome {
struct Color {
  uint8_t r{0}, g{0}, b{0}, w{0};
  Color() = default;
  Color(uint8_t red, uint8_t green, uint8_t blue) : r(red), g(green), b(blue), w(0) {}
};
}  // namespace esphome
