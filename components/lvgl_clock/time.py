"""UART time distribution for a multi-node ClockClock 24.

    time:
      - platform: lvgl_clock
        id: clock_time
        uart_id: sync_bus
        lvgl_clock_id: dc          # optional: the mode travels with the time
        broadcast_interval: 1s     # present => master, absent => slave

`lvgl_clock_id` also takes a list, for a node driving several panels:

    lvgl_clock_id: [dc_a, dc_b, dc_c]

`temperature_sensor_id` goes on the MASTER only. Its reading travels in the
sync packet, so `mode: temp` works on every board without a sensor of its own.

See digital_clock_clock_24/ for the whole build.
"""

import json
import os

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import time as time_
from esphome.components import sensor, uart
from esphome.const import CONF_ID

from . import LvglClock, lvgl_clock_ns

DEPENDENCIES = ["uart"]

SyncTime = lvgl_clock_ns.class_("SyncTime", time_.RealTimeClock, uart.UARTDevice)

CONF_BROADCAST_INTERVAL = "broadcast_interval"
CONF_LVGL_CLOCK_ID = "lvgl_clock_id"
CONF_TEMPERATURE_SENSOR_ID = "temperature_sensor_id"
CONF_PATTERNS = "patterns"
CONF_PATTERN_DELAY = "pattern_delay"
CONF_PATTERN_REPEAT = "pattern_repeat"

# Mirrors PATTERN_MAX_PER_NODE / PATTERN_CLOCKS in pattern_store.h.
MAX_PATTERNS = 8
PATTERN_CLOCKS = 24
NEIGHBOURS = ("left", "right", "up", "down")


def _wall_pos(i):
    """Clock index -> (col, row) on the 8x3 wall. See lvgl_clock.h."""
    digit, cell = divmod(i, 6)
    return digit * 2 + (cell % 2), cell // 2


def _neighbour(i, direction):
    col, row = _wall_pos(i)
    if direction == "left":
        col -= 1
    elif direction == "right":
        col += 1
    elif direction == "up":
        row -= 1
    else:
        row += 1
    if not (0 <= col < 8 and 0 <= row < 3):
        return None
    return (col >> 1) * 6 + row * 2 + (col & 1)


def _resolve_speed(clocks, i, hand, seen):
    """Follow a `same as my neighbour` chain down to a plain number.

    The editor lets a speed be relative so a gradient can be authored by
    setting one clock and letting the wall derive itself. That is an authoring
    convenience: it is resolved HERE, at codegen, and the firmware only ever
    sees fixed speeds. Walking a graph and breaking its cycles is not work to
    ship to eight microcontrollers for no gain.
    """
    spd = clocks[i].get("spdA" if hand == 0 else "spdB") or {}
    if spd.get("mode") != "rel":
        return max(0.0, min(1.0, float(spd.get("v", 0.0))))
    key = (i, hand)
    if key in seen:
        # A -> B -> A. A still clock is a visible, harmless answer; refusing to
        # build over it would be a worse one.
        return 0.0
    seen.add(key)
    j = _neighbour(i, spd.get("from", "left"))
    if j is None:
        return 0.0
    return max(0.0, min(1.0, _resolve_speed(clocks, j, hand, seen) + float(spd.get("d", 0.0))))


def _load_pattern(path):
    """Read one exported pattern and flatten it for the firmware."""
    with open(path, "r", encoding="utf-8") as fh:
        try:
            raw = json.load(fh)
        except ValueError as err:
            raise cv.Invalid(f"{os.path.basename(path)}: not valid JSON - {err}")
    clocks = raw.get("clocks") if isinstance(raw, dict) else raw
    if not isinstance(clocks, list) or len(clocks) != PATTERN_CLOCKS:
        raise cv.Invalid(
            f"{os.path.basename(path)}: expected {PATTERN_CLOCKS} clocks, "
            f"found {len(clocks) if isinstance(clocks, list) else 'none'}"
        )
    out = []
    for i, c in enumerate(clocks):
        if not isinstance(c, dict):
            raise cv.Invalid(f"{os.path.basename(path)}: clock {i} is not an object")
        out.append(
            (
                int(round(float(c.get("h0", 0)))) % 360,
                int(round(float(c.get("h1", 180)))) % 360,
                max(-1, min(1, int(c.get("dirA", 0)))),
                max(-1, min(1, int(c.get("dirB", 0)))),
                int(round(_resolve_speed(clocks, i, 0, set()) * 100)),
                int(round(_resolve_speed(clocks, i, 1, set()) * 100)),
            )
        )
    # The name is what shows in the logs, so it is worth being the filename
    # rather than an index nobody can map back to a file.
    name = os.path.splitext(os.path.basename(path))[0][:15]
    return name, out


def _patterns_dir(value):
    """A folder of exported patterns, relative to the YAML."""
    path = cv.directory(value)
    files = sorted(f for f in os.listdir(path) if f.lower().endswith(".json"))
    if not files:
        raise cv.Invalid(f"no .json patterns in {value}")
    if len(files) > MAX_PATTERNS:
        raise cv.Invalid(
            f"{len(files)} patterns in {value}, but a node holds at most {MAX_PATTERNS}"
        )
    # Parsed at validation time so a broken file is a config error with a
    # filename, not a compile error in generated code.
    return [_load_pattern(os.path.join(path, f)) for f in files]

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
            # Master only: the temperature that rides along with the time, so
            # `mode: temp` shows one reading across the whole wall. Put the
            # sensor on the master and leave the slaves without one - eight
            # sensors would just be eight opinions about the same room.
            cv.Optional(CONF_TEMPERATURE_SENSOR_ID): cv.use_id(sensor.Sensor),
            # Master only: a folder of patterns exported from
            # tools/clockclock24-sim. They are baked into the master's firmware
            # and pushed to every slave over the bus, so the slaves need no
            # filesystem and no separate flash step.
            cv.Optional(CONF_PATTERNS): _patterns_dir,
            # How long after boot the first push goes out. The master is the
            # only board with Wi-Fi, so it is up and talking while the slaves
            # are still bringing up PSRAM, three panels and LVGL - anything
            # sent into that window is simply not heard.
            cv.Optional(
                CONF_PATTERN_DELAY, default="30s"
            ): cv.positive_time_period_milliseconds,
            # And how often it repeats, so a board that reboots later picks the
            # patterns up without anyone touching the master.
            cv.Optional(
                CONF_PATTERN_REPEAT, default="5min"
            ): cv.positive_time_period_milliseconds,
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

    if (tid := config.get(CONF_TEMPERATURE_SENSOR_ID)) is not None:
        cg.add(var.set_temperature_sensor(await cg.get_variable(tid)))

    cg.add(var.set_pattern_delay(config[CONF_PATTERN_DELAY].total_milliseconds))
    cg.add(var.set_pattern_repeat(config[CONF_PATTERN_REPEAT].total_milliseconds))
    # One call per clock rather than a generated array: it keeps the wire
    # format and the in-memory format the only two representations, and 24
    # calls per pattern is nothing at codegen.
    for slot, (name, clocks) in enumerate(config.get(CONF_PATTERNS, [])):
        cg.add(var.add_pattern_name(slot, name))
        for i, (h0, h1, d0, d1, v0, v1) in enumerate(clocks):
            cg.add(var.add_pattern_clock(slot, i, h0, h1, d0, d1, v0, v1))
