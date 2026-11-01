# CrossMetal — third-party notices

## CrossPoint Reader (base firmware)

This project is derived from **crosspoint-reader**.

- Source: https://github.com/crosspoint-reader/crosspoint-reader
- Base revision: `f73d788` (branch `develop`)
- License: see `LICENSE` in the repository root.

The reader stack (EPUB engine, library, UI, filesystem helpers) originates there
and retains its original license.

## Metalio-E-INK4-Plus (board support layer)

The hardware bring-up layer under `components/metalio_bsp/` is ported from
**Metalio-E-INK4-Plus**.

- Source: https://github.com/CloudZao/Metalio-E-INK4-Plus
- License: **MIT**

```
MIT License

Copyright (c) 2025 Shenzhen Xinzhi Future Technology Co., Ltd.
Copyright (c) 2025 Project Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### Pinned upstream revisions referenced

The Metalio hardware repository was consulted at the revision below. Upstream's
`CMakeLists.txt` reports `PROJECT_VER` `2.0.66`.

| Upstream path | Ported to |
|:---|:---|
| `main/boards/metalio-e-ink4-plus/config.h` | `include/metalio_pinout.h` |
| `main/boards/metalio-e-ink4-plus/bus/metalio_i2c1.{c,h}` | `src/cm_i2c.c` |
| `main/boards/metalio-e-ink4-plus/bus/pca9555.{c,h}` | `src/cm_tca9555.c` |
| `main/boards/metalio-e-ink4-plus/bus/tps65185.{c,h}` | `src/cm_tps65185.c` |
| `main/boards/metalio-e-ink4-plus/display/epd_board_metalio_eink4_plus.{c,h}` | `src/cm_board.c` |
| `main/boards/common/power/bq27220_gauge.{cc,h}` | `src/cm_power.c` |
| `main/boards/common/power/cx25601n.{cc,h}` | `src/cm_power.c` |
| `main/boards/metalio-e-ink4-plus/input/metalio_keys.{cc,h}` | `src/cm_keys.c` |
| `main/boards/metalio-e-ink4-plus/periph/frontlight.{cc,h}` | `src/cm_frontlight.c` |
| `main/boards/metalio-e-ink4-plus/periph/metalio_sd.{cc,h}` | `src/cm_storage.c` |
| `main/boards/metalio-e-ink4-plus/periph/metalio_rtc.{cc,h}` | `src/cm_rtc.c` |
| `main/boards/metalio-e-ink4-plus/display/metalio_touch.c` | `src/cm_touch_ft6336u.c` |
| `partitions.csv` | `partitions_metalio.csv` |
| `sdkconfig.defaults.esp32s31` | `crossmetal_sdkconfig.defaults` |

### Display subsystem (`components/metalio_epd/`)

Vendored from `main/boards/metalio-e-ink4-plus/display/`, with deviations documented in each file header:

| Upstream path | Vendored to | Deviations |
|:---|:---|:---|
| `display/epdiy.{c,h}` | `src/epdiy.{c,h}` | none |
| `display/epd_board.{c,h}` | `src/epd_board.{c,h}` | none |
| `display/epd_board_common.{c,h}` | `src/epd_board_common.{c,h}` | none |
| `display/epd_display.h`, `epd_internals.h` | same names | none |
| `display/displays.c` | `src/displays.c` | none |
| `display/render.{c,h}`, `highlevel.c` | `src/` | none |
| `display/builtin_waveforms.c`, `board_specific.c` | `src/` | none |
| `display/epdiy_riscv_ve_stub.c` | `src/epdiy_riscv_ve_stub.c` | none |
| `display/output_lcd/*` | `src/output_lcd/*` | none |
| `display/output_common/*` | `src/output_common/*` | none |
| `display/waveforms/*` | `src/waveforms/*` | none |
| `display/epd_board_metalio_eink4_plus.{c,h}` | `src/` | VBORDER setting, delay after `epd_init()` |

`display/output_common/lut.S` is present in the vendor tree but **not built**: it
is S3 vector-extension assembly guarded by `#ifdef CONFIG_IDF_TARGET_ESP32S3` and
is empty on RISC-V.

CrossMetal-authored files in that component (not upstream):

| File | Purpose |
|:---|:---|
| `include/config.h` | shim: upstream pin names → `metalio_pinout.h` |
| `include/metalio_i2c1.h` | shim → `cm_i2c.h` |
| `include/pca9555.h` | shim → `cm_tca9555.h` |
| `include/tps65185.h` | shim → `cm_tps65185.h` |
| `include/metalio_sd.h` | shim → `cm_storage.h` |
| `include/cm_display.h`, `src/cm_display.c` | CrossMetal facade over the stack |

The waveform tables under `src/waveforms/` include `my_waveform*`, which
upstream authored for this panel; `waveforms/LICENSE.gray8` is carried alongside
them.

Every ported source file carries a header comment naming the upstream file it
came from. Behavioural deviations are called out individually in those headers
with the reason, so a future diff against upstream stays tractable.

### Other upstreams referenced but not vendored

- **EPDiy** — the parallel e-paper driver the Metalio display stack is built on
  (upstream carries it as `display/epdiy.{c,h}` and `display/output_lcd/`). Not
  yet ported; see `docs/CM_BOARD_PORT.md` §8.
- **esp-idf** — framework, Apache-2.0.
- **arduino-esp32 / pioarduino** — referenced only to document why they cannot
  build this target.

## Provenance note

Hardware values in this project were transcribed from the Metalio repository's
source and its own `README.md`, and cross-checked between the two. Where the two
disagree, the discrepancy is recorded rather than silently resolved — see
`docs/HARDWARE_SPEC_METALIO_EINK4_PLUS.md` §8.

The repository ships a `datasheet/DEPG0397BBS77AF11HP-2.1-GY.pdf` which is a
**3.97"** panel datasheet. It does **not** describe this device's 4.7" ED047TC2
panel; it belongs to the sibling product. It was not used as a source for any
value here.