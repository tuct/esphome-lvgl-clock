#pragma once
// Pulls in the real LVGL and provides the tiny LvCompound mixin lvgl_clock
// derives from (just an owned lv_obj_t*). No ESPHome LVGL machinery.
#include "lvgl.h"

namespace esphome {
namespace lvgl {
class LvCompound {
 public:
  virtual ~LvCompound() = default;
  virtual void set_obj(lv_obj_t *obj) { this->obj = obj; }
  lv_obj_t *obj{nullptr};
};
}  // namespace lvgl
}  // namespace esphome
