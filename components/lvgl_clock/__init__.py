import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import time
from esphome.components.lvgl.defines import CONF_MAIN, literal
from esphome.components.lvgl.lv_validation import lv_font
from esphome.components.lvgl.lvcode import lv, lv_add, lv_expr
from esphome.components.lvgl.types import LvCompound, LvType
from esphome.components.lvgl.widgets import Widget, WidgetType
from esphome.const import (
    CONF_COLOR,
    CONF_FONT,
    CONF_HEIGHT,
    CONF_ID,
    CONF_MODE,
    CONF_TIME_ID,
    CONF_WIDTH,
)

CODEOWNERS = ["@tuct"]
# A bare marker: this key exists only so ESPHome imports this Python module
# (component modules are only imported when their domain appears as a
# top-level YAML key), which registers the `lvgl_clock` widget type below
# before `lvgl:` validates its `widgets:` list. It takes no options - the
# actual clock lives under `lvgl: widgets: - lvgl_clock: ...`.
DEPENDENCIES = ["lvgl"]

CONFIG_SCHEMA = cv.Schema({})


async def to_code(config):
    pass


lvgl_clock_ns = cg.esphome_ns.namespace("lvgl_clock")
LvglClock = lvgl_clock_ns.class_("LvglClock", cg.Component, LvCompound)
# Same fully-qualified type as `LvglClock` above (by construction, not just
# by name) so `cv.use_id(LvglClock)` in the mode actions below resolves
# against widget ids declared with this type - see MockObjClass.inherits_from,
# which compares these by str(). `cg.Component` must be listed here too:
# config.py only adds a declared id to CORE.component_ids (and therefore lets
# cg.register_component() accept it) when its declared type inherits_from
# Component - WidgetType.create_to_code() never registers compound widgets as
# components on its own, so without this LvglClock::setup()/loop() would
# simply never run.
lv_lvgl_clock_t = LvType(str(LvglClock), parents=(LvCompound, cg.Component))
lv_draw_buf_t = LvType("lv_draw_buf_t")
SetModeAction = lvgl_clock_ns.class_("SetModeAction", automation.Action)
ColorStruct = cg.esphome_ns.struct("Color")

ClockStyle = lvgl_clock_ns.enum("ClockStyle")
STYLES = {
    "clockclock24": ClockStyle.STYLE_CLOCKCLOCK24,
    "analog": ClockStyle.STYLE_ANALOG,
    "digital": ClockStyle.STYLE_DIGITAL,
    "flipclock": ClockStyle.STYLE_FLIPCLOCK,
    "seg_matrix": ClockStyle.STYLE_SEG_MATRIX,
}
HandStyle = lvgl_clock_ns.enum("HandStyle")
HAND_STYLES = {
    "baton": HandStyle.HAND_STYLE_BATON,
    "line": HandStyle.HAND_STYLE_LINE,
    "line_rounded": HandStyle.HAND_STYLE_LINE_ROUNDED,
    "lollipop": HandStyle.HAND_STYLE_LOLLIPOP,
    "sbb": HandStyle.HAND_STYLE_SBB,
}
CenterStyle = lvgl_clock_ns.enum("CenterStyle")
CENTER_STYLES = {
    "circle": CenterStyle.CENTER_STYLE_CIRCLE,
    "round": CenterStyle.CENTER_STYLE_ROUND,
    "none": CenterStyle.CENTER_STYLE_NONE,
}
SegmentStyle = lvgl_clock_ns.enum("SegmentStyle")
SEGMENT_STYLES = {
    "classic": SegmentStyle.SEGMENT_STYLE_CLASSIC,
    "rounded": SegmentStyle.SEGMENT_STYLE_ROUNDED,
}
TickSize = lvgl_clock_ns.enum("TickSize")
TICK_SIZES = {
    "s": TickSize.TICK_SIZE_SMALL,
    "m": TickSize.TICK_SIZE_MEDIUM,
    "l": TickSize.TICK_SIZE_LARGE,
}

ClockMode = lvgl_clock_ns.enum("ClockMode")
MODES = {
    "time": ClockMode.CC_MODE_TIME,
    "rotate_left": ClockMode.CC_MODE_ROTATE_LEFT,
    "flying_birds": ClockMode.CC_MODE_FLYING_BIRDS,
    "demo": ClockMode.CC_MODE_DEMO,
}
MovementMode = lvgl_clock_ns.enum("MovementMode")
MOVEMENTS = {
    "opposite": MovementMode.CC_MOVE_OPPOSITE,
    "clockwise": MovementMode.CC_MOVE_CLOCKWISE,
    "counter": MovementMode.CC_MOVE_COUNTER,
    "long": MovementMode.CC_MOVE_LONG,
}

# style sub-blocks
CONF_STYLE = "style"
CONF_CLOCKCLOCK24 = "clockclock24"
CONF_ANALOG = "analog"
CONF_DIGITAL = "digital"
CONF_FLIPCLOCK = "flipclock"
CONF_SEG_MATRIX = "seg_matrix"

# shared
CONF_TWENTY_FOUR_HOUR = "twenty_four_hour"
CONF_SHOW_FACE = "show_face"
CONF_FOREGROUND = "foreground"  # the "ink": hands / markers / digits
CONF_BACKGROUND = "background"  # behind everything
CONF_TRANSPARENT = "transparent"  # clear to transparent instead of background
CONF_SHOW_SECONDS = "show_seconds"
CONF_RENDER_INTERVAL = "render_interval"
# face colours (styles that draw a dial / clock faces)
CONF_FACE_COLOR = "face_color"  # dial / clock-face fill
CONF_BORDER_COLOR = "border_color"  # dial rim + tick marks
# clockclock24
CONF_HAND_WIDTH = "hand_width"
CONF_TRANSITION_LENGTH = "transition_length"
CONF_MOVEMENT = "movement"
CONF_MODE_SPEED = "mode_speed"
CONF_SPACING = "spacing"
CONF_DEMO_INTERVAL = "demo_interval"
# analog (classic)
CONF_MINUTE_TICKS = "minute_ticks"
CONF_HOUR_TICKS = "hour_ticks"
CONF_ENABLED = "enabled"
CONF_ROUNDED = "rounded"
CONF_LENGTH = "length"
CONF_HOUR_HAND = "hour_hand"
CONF_MINUTE_HAND = "minute_hand"
CONF_SECOND_HAND = "second_hand"
CONF_CENTER_STYLE = "center_style"
CONF_EXTEND = "extend"
# digital (seven-segment)
CONF_SEGMENT_STYLE = "segment_style"
CONF_BLINK = "blink"
CONF_BLANK_LEADING_ZERO = "blank_leading_zero"
CONF_OFF_COLOR = "off_color"
# flipclock
CONF_CARD_COLOR = "card_color"
CONF_FLIP_DURATION = "flip_duration"
CONF_SHOW_DOTS = "show_dots"
CONF_AM_PM_FONT = "am_pm_font"
# internal
CONF_DRAW_BUF_ID = "draw_buf_id"

# options shared by the styles that draw a face (analog + clockclock24)
FACE_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_SHOW_FACE, default=False): cv.boolean,
        cv.Optional(CONF_FACE_COLOR): cv.use_id(ColorStruct),
        cv.Optional(CONF_BORDER_COLOR): cv.use_id(ColorStruct),
    }
)

CLOCKCLOCK24_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_HAND_WIDTH, default=1): cv.int_range(min=1, max=12),
        cv.Optional(
            CONF_TRANSITION_LENGTH, default="2s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_MOVEMENT, default="opposite"): cv.enum(MOVEMENTS, lower=True),
        cv.Optional(CONF_MODE, default="time"): cv.enum(MODES, lower=True),
        cv.Optional(CONF_MODE_SPEED, default=1.0): cv.positive_float,
        cv.Optional(CONF_SPACING, default=0.0): cv.float_range(min=0.0, max=4.0),
        # testing aid for `mode: demo` - how often the fake minute advances
        cv.Optional(
            CONF_DEMO_INTERVAL, default="5s"
        ): cv.positive_time_period_milliseconds,
    }
).extend(FACE_SCHEMA)


def _hand_schema(default_style):
    return cv.Schema(
        {
            cv.Optional(CONF_STYLE, default=default_style): cv.enum(HAND_STYLES, lower=True),
            cv.Optional(CONF_COLOR): cv.use_id(ColorStruct),
            cv.Optional(CONF_CENTER_STYLE, default="circle"): cv.enum(
                CENTER_STYLES, lower=True
            ),
            # extends the hand a bit past the pivot on the opposite side, e.g.
            # the second hand's small counterweight tail.
            cv.Optional(CONF_EXTEND, default="0%"): cv.All(
                cv.percentage, cv.float_range(min=0.0, max=0.5)
            ),
        }
    )


def _tick_schema(default_size):
    return cv.Schema(
        {
            cv.Optional(CONF_ENABLED, default=True): cv.boolean,
            cv.Optional(CONF_COLOR): cv.use_id(ColorStruct),
            cv.Optional(CONF_ROUNDED, default=True): cv.boolean,
            cv.Optional(CONF_WIDTH, default=default_size): cv.enum(TICK_SIZES, lower=True),
            cv.Optional(CONF_LENGTH, default=default_size): cv.enum(TICK_SIZES, lower=True),
        }
    )


ANALOG_SCHEMA = cv.Schema(
    {
        # "s"/"m"/"l" mean the same absolute size on either ring - each just
        # defaults to its own historical look (small minute ticks, bold hour
        # ticks) unless overridden.
        cv.Optional(CONF_MINUTE_TICKS, default={}): _tick_schema("s"),
        cv.Optional(CONF_HOUR_TICKS, default={}): _tick_schema("l"),
        cv.Optional(CONF_HOUR_HAND, default={}): _hand_schema("baton"),
        cv.Optional(CONF_MINUTE_HAND, default={}): _hand_schema("baton"),
        cv.Optional(CONF_SECOND_HAND, default={}): _hand_schema("lollipop"),
    }
).extend(FACE_SCHEMA)

DIGITAL_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_SEGMENT_STYLE, default="classic"): cv.enum(
            SEGMENT_STYLES, lower=True
        ),
        cv.Optional(CONF_BLINK, default=False): cv.boolean,
        cv.Optional(CONF_BLANK_LEADING_ZERO, default=False): cv.boolean,
        # colour of unlit segments (the "ghost 8"); default is a dark grey
        cv.Optional(CONF_OFF_COLOR): cv.use_id(ColorStruct),
    }
)

# big HH:MM digits drawn on the reference 6x24 grid of small 7-segment displays
SEG_MATRIX_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_SEGMENT_STYLE, default="classic"): cv.enum(
            SEGMENT_STYLES, lower=True
        ),
        # colour of the unlit small segments (the "ghost" grid)
        cv.Optional(CONF_OFF_COLOR): cv.use_id(ColorStruct),
    }
)

FLIPCLOCK_SCHEMA = cv.Schema(
    {
        # a built-in LVGL font name (e.g. "montserrat_48") or an ESPHome
        # `font:` id - the glyph size drives the card size, so pick one that
        # fits the widget.
        cv.Required(CONF_FONT): lv_font,
        cv.Optional(CONF_CARD_COLOR): cv.use_id(ColorStruct),
        # how long one digit-flip takes; 0 disables the animation
        cv.Optional(
            CONF_FLIP_DURATION, default="450ms"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_BLINK, default=False): cv.boolean,
        cv.Optional(CONF_BLANK_LEADING_ZERO, default=False): cv.boolean,
        # false = no divider dots between HH/MM/SS - the groups are then only
        # separated by the normal inter-card gap
        cv.Optional(CONF_SHOW_DOTS, default=True): cv.boolean,
        # small AM/PM marker font, only drawn in 12h mode
        # (twenty_four_hour: false) in the hour card's lower-left corner
        cv.Optional(CONF_AM_PM_FONT, default="montserrat_14"): lv_font,
    }
)


def _resolve_style(config):
    sub_blocks = [
        k
        for k in (
            CONF_CLOCKCLOCK24,
            CONF_ANALOG,
            CONF_DIGITAL,
            CONF_FLIPCLOCK,
            CONF_SEG_MATRIX,
        )
        if k in config
    ]
    if len(sub_blocks) > 1:
        raise cv.Invalid(
            "Only one of clockclock24/analog/digital/flipclock/seg_matrix may be set, "
            f"got: {', '.join(sub_blocks)}"
        )

    style = config.get(CONF_STYLE)
    style = style.lower() if style else None
    if style is not None and style not in STYLES:
        raise cv.Invalid(f"style must be one of {list(STYLES)}", path=[CONF_STYLE])

    if style is None:
        style = sub_blocks[0] if sub_blocks else "clockclock24"
    elif sub_blocks and sub_blocks[0] != style:
        raise cv.Invalid(f"'{sub_blocks[0]}:' does not match style: {style}")

    if style == CONF_FLIPCLOCK and CONF_FLIPCLOCK not in config:
        raise cv.Invalid(
            "style: flipclock needs a `flipclock:` block (its `font:` is required)"
        )

    config[CONF_STYLE] = style
    return config


WIDGET_SCHEMA = (
    cv.Schema(
        {
            cv.Required(CONF_WIDTH): cv.int_range(min=1),
            cv.Required(CONF_HEIGHT): cv.int_range(min=1),
            cv.Required(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
            cv.GenerateID(CONF_DRAW_BUF_ID): cv.declare_id(lv_draw_buf_t),
            cv.Optional(CONF_STYLE): cv.string_strict,
            cv.Optional(CONF_TWENTY_FOUR_HOUR, default=True): cv.boolean,
            cv.Optional(CONF_SHOW_SECONDS, default=False): cv.boolean,
            cv.Optional(
                CONF_RENDER_INTERVAL, default="16ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_FOREGROUND): cv.use_id(ColorStruct),
            cv.Optional(CONF_BACKGROUND): cv.use_id(ColorStruct),
            # transparent background - allocates an ARGB8888 canvas (4 bytes/px
            # vs 2) so widgets behind the clock show through; `background` is
            # then unused.
            cv.Optional(CONF_TRANSPARENT, default=False): cv.boolean,
            cv.Optional(CONF_CLOCKCLOCK24): CLOCKCLOCK24_SCHEMA,
            cv.Optional(CONF_ANALOG): ANALOG_SCHEMA,
            cv.Optional(CONF_DIGITAL): DIGITAL_SCHEMA,
            cv.Optional(CONF_FLIPCLOCK): FLIPCLOCK_SCHEMA,
            cv.Optional(CONF_SEG_MATRIX): SEG_MATRIX_SCHEMA,
        }
    ).add_extra(_resolve_style)
)


async def _set_color(var, config, key, setter):
    if key in config:
        lv_add(setter(await cg.get_variable(config[key])))


async def _apply_face(var, c):
    lv_add(var.set_show_face(c[CONF_SHOW_FACE]))
    await _set_color(var, c, CONF_FACE_COLOR, var.set_face_fill_color)
    await _set_color(var, c, CONF_BORDER_COLOR, var.set_face_border_color)


class LvglClockWidgetType(WidgetType):
    def __init__(self):
        super().__init__(
            "lvgl_clock",
            lv_lvgl_clock_t,
            (CONF_MAIN,),
            schema=WIDGET_SCHEMA,
            modify_schema={},
            lv_name="canvas",
        )

    def get_uses(self):
        # lv_canvas_t structurally embeds an lv_image_dsc_t, and LVGL's image
        # widget in turn hard-requires the label widget to be compiled in -
        # both needed even though we never create `image:`/`label:` widgets
        # ourselves.
        return ("canvas", "image", "label")

    async def to_code(self, w: Widget, config):
        # WidgetType.create_to_code() only constructs compound widgets - it
        # doesn't register them as ESPHome components, so without this
        # LvglClock::setup()/loop() (and therefore render_()) never run.
        await cg.register_component(w.var, config)

        width = config[CONF_WIDTH]
        height = config[CONF_HEIGHT]
        # A transparent clock needs an alpha channel, so use ARGB8888 (4 bytes/
        # px) instead of the native RGB565 (2 bytes/px) - the C++ side then
        # clears each frame to transparent instead of the background colour.
        transparent = config[CONF_TRANSPARENT]
        cf = "LV_COLOR_FORMAT_ARGB8888" if transparent else "LV_COLOR_FORMAT_NATIVE"
        draw_buf = cg.new_Pvariable(config[CONF_DRAW_BUF_ID])
        buf_size = literal(f"LV_DRAW_BUF_SIZE({width}, {height}, {cf})")
        lv.draw_buf_init(
            draw_buf,
            width,
            height,
            literal(cf),
            0,
            lv_expr.malloc_core(buf_size),
            literal(buf_size),
        )
        lv.draw_buf_set_flag(draw_buf, literal("LV_IMAGE_FLAGS_MODIFIABLE"))
        lv.canvas_set_draw_buf(w.obj, draw_buf)
        lv_add(w.var.set_canvas_size(width, height))
        lv_add(w.var.set_transparent(transparent))

        lv_add(w.var.set_time(await cg.get_variable(config[CONF_TIME_ID])))
        style = config[CONF_STYLE]
        lv_add(w.var.set_style(STYLES[style]))
        lv_add(w.var.set_twenty_four_hour(config[CONF_TWENTY_FOUR_HOUR]))
        lv_add(w.var.set_show_seconds(config[CONF_SHOW_SECONDS]))
        lv_add(w.var.set_render_interval(config[CONF_RENDER_INTERVAL].total_milliseconds))
        await _set_color(w.var, config, CONF_FOREGROUND, w.var.set_foreground)
        await _set_color(w.var, config, CONF_BACKGROUND, w.var.set_background)

        if CONF_CLOCKCLOCK24 in config:
            c = config[CONF_CLOCKCLOCK24]
            lv_add(w.var.set_hand_width(c[CONF_HAND_WIDTH]))
            lv_add(w.var.set_transition_length(c[CONF_TRANSITION_LENGTH].total_milliseconds))
            lv_add(w.var.set_movement(c[CONF_MOVEMENT]))
            lv_add(w.var.set_mode_speed(c[CONF_MODE_SPEED]))
            lv_add(w.var.set_spacing(c[CONF_SPACING]))
            lv_add(w.var.set_mode(c[CONF_MODE]))
            lv_add(w.var.set_demo_interval(c[CONF_DEMO_INTERVAL].total_milliseconds))
            await _apply_face(w.var, c)
        elif CONF_ANALOG in config:
            c = config[CONF_ANALOG]
            mt = c[CONF_MINUTE_TICKS]
            ht = c[CONF_HOUR_TICKS]
            lv_add(w.var.set_minute_ticks(mt[CONF_ENABLED]))
            lv_add(w.var.set_hour_ticks(ht[CONF_ENABLED]))
            lv_add(w.var.set_hour_ticks_rounded(ht[CONF_ROUNDED]))
            lv_add(w.var.set_minute_ticks_rounded(mt[CONF_ROUNDED]))
            lv_add(w.var.set_hour_ticks_width(ht[CONF_WIDTH]))
            lv_add(w.var.set_minute_ticks_width(mt[CONF_WIDTH]))
            lv_add(w.var.set_hour_ticks_length(ht[CONF_LENGTH]))
            lv_add(w.var.set_minute_ticks_length(mt[CONF_LENGTH]))
            await _set_color(w.var, ht, CONF_COLOR, w.var.set_hour_ticks_color)
            await _set_color(w.var, mt, CONF_COLOR, w.var.set_minute_ticks_color)
            lv_add(w.var.set_hour_hand_style(c[CONF_HOUR_HAND][CONF_STYLE]))
            lv_add(w.var.set_minute_hand_style(c[CONF_MINUTE_HAND][CONF_STYLE]))
            lv_add(w.var.set_second_hand_style(c[CONF_SECOND_HAND][CONF_STYLE]))
            lv_add(w.var.set_hour_center_style(c[CONF_HOUR_HAND][CONF_CENTER_STYLE]))
            lv_add(w.var.set_minute_center_style(c[CONF_MINUTE_HAND][CONF_CENTER_STYLE]))
            lv_add(w.var.set_second_center_style(c[CONF_SECOND_HAND][CONF_CENTER_STYLE]))
            lv_add(w.var.set_hour_extend(c[CONF_HOUR_HAND][CONF_EXTEND]))
            lv_add(w.var.set_minute_extend(c[CONF_MINUTE_HAND][CONF_EXTEND]))
            lv_add(w.var.set_second_extend(c[CONF_SECOND_HAND][CONF_EXTEND]))
            await _set_color(w.var, c[CONF_HOUR_HAND], CONF_COLOR, w.var.set_hour_hand_color)
            await _set_color(w.var, c[CONF_MINUTE_HAND], CONF_COLOR, w.var.set_minute_hand_color)
            await _set_color(w.var, c[CONF_SECOND_HAND], CONF_COLOR, w.var.set_second_hand_color)
            await _apply_face(w.var, c)
        elif CONF_DIGITAL in config:
            c = config[CONF_DIGITAL]
            lv_add(w.var.set_segment_style(c[CONF_SEGMENT_STYLE]))
            lv_add(w.var.set_digital_blink(c[CONF_BLINK]))
            lv_add(w.var.set_digital_blank_leading(c[CONF_BLANK_LEADING_ZERO]))
            await _set_color(w.var, c, CONF_OFF_COLOR, w.var.set_digital_off_color)
        elif CONF_FLIPCLOCK in config:
            c = config[CONF_FLIPCLOCK]
            # resolves to `&lv_font_<name>` for built-ins or a `font::Font*`
            # for ESPHome fonts - set_flip_font has an overload for each
            lv_add(w.var.set_flip_font(await lv_font.process(c[CONF_FONT])))
            await _set_color(w.var, c, CONF_CARD_COLOR, w.var.set_card_color)
            lv_add(w.var.set_flip_duration(c[CONF_FLIP_DURATION].total_milliseconds))
            lv_add(w.var.set_flip_show_dots(c[CONF_SHOW_DOTS]))
            lv_add(w.var.set_am_pm_font(await lv_font.process(c[CONF_AM_PM_FONT])))
            # the divider dots + leading zero reuse the digital-style plumbing
            lv_add(w.var.set_digital_blink(c[CONF_BLINK]))
            lv_add(w.var.set_digital_blank_leading(c[CONF_BLANK_LEADING_ZERO]))
        elif CONF_SEG_MATRIX in config:
            c = config[CONF_SEG_MATRIX]
            lv_add(w.var.set_segment_style(c[CONF_SEGMENT_STYLE]))
            await _set_color(w.var, c, CONF_OFF_COLOR, w.var.set_digital_off_color)


LvglClockWidgetType()


# --- Actions (clockclock24 idle modes) --------------------------------------
_ACTION_SCHEMA = automation.maybe_simple_id({cv.GenerateID(): cv.use_id(LvglClock)})


def _register_mode_action(action, mode):
    @automation.register_action(action, SetModeAction, _ACTION_SCHEMA, synchronous=True)
    async def _to_code(config, action_id, template_arg, args):
        parent = await cg.get_variable(config[CONF_ID])
        return cg.new_Pvariable(action_id, template_arg, parent, mode)

    return _to_code


_show_time = _register_mode_action("lvgl_clock.show_time", MODES["time"])
_rotate_left = _register_mode_action("lvgl_clock.rotate_left", MODES["rotate_left"])
_flying_birds = _register_mode_action("lvgl_clock.flying_birds", MODES["flying_birds"])
_demo = _register_mode_action("lvgl_clock.demo", MODES["demo"])
