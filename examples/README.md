# Examples

Ready-to-flash single-board configs, one per clock style, plus the shared
packages they include. Each example is small on purpose — the board and the
panel come from packages, so the file itself is almost entirely the widget.

```bash
esphome run example_clockclock24.yaml
```

Copy [`secrets.yaml.example`](./secrets.yaml.example) to `secrets.yaml` first.
Every option used here is documented in the
[component README](../components/lvgl_clock/README.md).

## The examples

| File | Style | Panel | What it shows |
| --- | --- | --- | --- |
| [`example_clockclock24.yaml`](./example_clockclock24.yaml) | clockclock24 | full screen | The full boot sequence: `rotate_left` while Wi-Fi connects, `flying_birds` while waiting for NTP, then the time — driven from an `interval:` with the mode actions |
| [`example_clockclock24_demo.yaml`](./example_clockclock24_demo.yaml) | clockclock24 | full screen | Boots straight into `mode: demo`, so the digit-flip repeats every 5 s instead of once a minute. Handy for trying the `movement:` options |
| [`example_clockclock24_round_dsp.yaml`](./example_clockclock24_round_dsp.yaml) | clockclock24 | 1.28″ round | All 24 mini-clocks squeezed onto one small round panel |
| [`example_clockclock24_direct.yaml`](./example_clockclock24_direct.yaml) | clockclock24 | full screen | **Experimental** `direct_draw:` benchmark — renders into LVGL's buffer instead of owning a canvas, dropping both the canvas RAM and the per-frame copy |
| [`example_analog.yaml`](./example_analog.yaml) | analog | square 320×320 | Every analog option at once, plus `transparent: true` with a plain LVGL `label:` **behind** the face showing the date through the gaps |
| [`example_analog_sbb.yaml`](./example_analog_sbb.yaml) | analog | square 320×320 | The Mondaine/SBB Swiss railway look: black-on-white, `sbb` hands, `lollipop` second hand |
| [`example_analog_round.yaml`](./example_analog_round.yaml) | analog | 1.28″ round | The same dial on a round panel, where a round face actually fits the glass |
| [`example_digital.yaml`](./example_digital.yaml) | digital | full screen | 24 h `HH:MM` 7-segment with a blinking colon and the ghost 8 |
| [`example_digital_12h.yaml`](./example_digital_12h.yaml) | digital | full screen | Same in 12 h mode — adds the vector-drawn AM/PM marker column |
| [`example_flipclock.yaml`](./example_flipclock.yaml) | flipclock | full screen | Split-flap cards with a Google-font TTF (`font:` at `size: 100`) — the recommended way to get large crisp digits |
| [`example_flipclock_12h.yaml`](./example_flipclock_12h.yaml) | flipclock | full screen | Same in 12 h mode — adds the dedicated AM/PM flap card |
| [`example_seg_matrix.yaml`](./example_seg_matrix.yaml) | seg_matrix | full screen | The 6×24 grid of small 7-segment displays with a dark red ghost grid |

## The shared packages

Every example is `packages:` = one **base** + one **display**, then its own
`lvgl: widgets:` block. Swapping hardware is a one-line edit, because pins and
sizes are substitutions rather than literals.

```yaml
packages:
  base: !include common_base_esp32_s3_xiao.yaml
  display: !include common_tft_1_28_round_xiao_seeed_GC9A01A_240_240.yaml
```

### Bases — the board

| File | Board |
| --- | --- |
| [`common_base_esp32.yaml`](./common_base_esp32.yaml) | Generic ESP32 |
| [`common_base_esp32_c3_xiao.yaml`](./common_base_esp32_c3_xiao.yaml) | Seeed XIAO ESP32-C3 |
| [`common_base_esp32_s3_devkit.yaml`](./common_base_esp32_s3_devkit.yaml) | ESP32-S3-DevKitC-1 (N16R8) — PSRAM, for large canvases |
| [`common_base_esp32_s3_xiao.yaml`](./common_base_esp32_s3_xiao.yaml) | Seeed XIAO ESP32-S3 — PSRAM, and what the multi-board builds use |

A base provides everything that is not the panel: board and framework,
`psram:`, `wifi:` / `api:` / `ota:` / `logger:`, the `external_components:`
pointer at `../components`, the `time: sntp` source (`id: clock_time`), the
bare `lvgl_clock:` marker key, the shared `cc_hands` / `cc_bg` / `cc_faces`
colours, and the **pin** substitutions.

### Displays — the panel

| File | Panel |
| --- | --- |
| [`common_tft_1_28_round_xiao_seeed_GC9A01A_240_240.yaml`](./common_tft_1_28_round_xiao_seeed_GC9A01A_240_240.yaml) | 1.28″ round GC9A01A, 240×240 — the panel the 24-screen wall is built from |
| [`common_tft_1_69_spi_st7789v2_240_280.yaml`](./common_tft_1_69_spi_st7789v2_240_280.yaml) | 1.69″ ST7789V2, 240×280 |
| [`common_tft_1_8_spi_st7735_128_160.yaml`](./common_tft_1_8_spi_st7735_128_160.yaml) | 1.8″ ST7735, 128×160 |
| [`common_tft_4_0_spi_st7796_320_480.yaml`](./common_tft_4_0_spi_st7796_320_480.yaml) | 4.0″ ST7796, 320×480 |

A display provides the `spi:` bus, the `display:` component (always
`id: my_display`), the `lvgl:` binding for it, and the **size**
substitutions — `clock_width` / `clock_height`. Pins come from the base, so a
panel file has no hard-coded GPIOs, and swapping the `display:` line resizes
the clock with it.

`packages:` merges dicts key by key, so the display file's `lvgl:` keys and the
example's own `lvgl: widgets:` combine into one `lvgl:` block.

To rewire the bus, override in the example — the outer file wins:

```yaml
substitutions:
  mosi_pin: "GPIO11"
  clk_pin: "GPIO12"
```

## Multi-board builds

These examples are all one board driving one panel. Spreading a single
24-clock choreography across many boards is a different thing, and each has its
own directory and README:

- [**`digital_clock_clock_24_24_round_screens/`**](../digital_clock_clock_24_24_round_screens)
  — 24 round panels on 8 boards, one mini-clock each. Built and running.
- [**`digital_clock_clock_24_4_screens/`**](../digital_clock_clock_24_4_screens)
  — 4 rectangular panels on 2 boards, one digit each.
