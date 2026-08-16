"""UART time distribution for a multi-node ClockClock 24.

    time:
      - platform: lvgl_clock
        id: clock_time
        uart_id: sync_bus
        lvgl_clock_id: dc          # optional: the mode travels with the time
        broadcast_interval: 1s     # present => master, absent => slave

`lvgl_clock_id` also takes a list, for a node driving several panels:

    lvgl_clock_id: [dc_a, dc_b, dc_c]

See digital_clock_clock_24/ for the whole build.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import time as time_
from esphome.components import uart
from esphome.const import CONF_ID

from . import LvglClock, lvgl_clock_ns

DEPENDENCIES = ["uart"]

SyncTime = lvgl_clock_ns.class_("SyncTime", time_.RealTimeClock, uart.UARTDevice)

CONF_BROADCAST_INTERVAL = "broadcast_interval"
CONF_LVGL_CLOCK_ID = "lvgl_clock_id"

# PollingComponent's "never" - a slave has nothing to send.
NEVER = 4294967295

CONFIG_SCHEMA = (
    time_.TIME_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(SyncTime),
            # Master role: how often the time line goes out. Omit for a slave.
            cv.Optional(CONF_BROADCAST_INTERVAL): cv.positive_time_period_milliseconds,
            # Optional link to the widget(s) so the clockclock24 idle mode
            # (rotate_left / flying_birds / demo) travels with the time and the
            # whole wall animates together. Takes one id or a list: a node
            # driving several panels must name all of them, since on a slave
            # the bus is the only thing that moves them.
            cv.Optional(CONF_LVGL_CLOCK_ID): cv.ensure_list(cv.use_id(LvglClock)),
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    # sync_time.h/.cpp are compiled into every config that uses this external
    # component, so they are guarded on this define rather than on `uart:`
    # being present (ESPHome has no USE_UART).
    cg.add_define("USE_LVGL_CLOCK_SYNC")

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    await time_.register_time(var, config)

    if (interval := config.get(CONF_BROADCAST_INTERVAL)) is not None:
        cg.add(var.set_broadcast(True))
        cg.add(var.set_update_interval(interval.total_milliseconds))
    else:
        cg.add(var.set_broadcast(False))
        cg.add(var.set_update_interval(NEVER))

    for clock_id in config.get(CONF_LVGL_CLOCK_ID, []):
        cg.add(var.add_clock(await cg.get_variable(clock_id)))
