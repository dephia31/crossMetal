# CrossMetal — build, flash and port notes

CrossMetal is a firmware project for the **Metalio E-Ink4-Plus**. It is a port of
the [Metalio-E-INK4-Plus](https://github.com/CloudZao/Metalio-E-INK4-Plus)
board layer (MIT) onto the
[crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader)
reader stack, renamed and restructured as an independent fork.

---

## 1. Read this first: the target is not an ESP32-S3

The device runs an **ESP32-S31**, not an ESP32-S3. This is stated in the hardware
repository in three independent places and it changes the toolchain entirely:

| Evidence | Source |
|:---|:---|
| `CONFIG_IDF_TARGET="esp32s31"` | `sdkconfig` |
| `CONFIG_IDF_TARGET_ARCH="riscv"` | `sdkconfig` |
| "MCU: **ESP32-S31** (RISC-V; Flash 16 MB, Octal PSRAM @ 200 MHz)" | `README.md` §1 |
| "Plus moves to **ESP32-S31** … Do not reuse the 397 SPI pin map" | `README.md` §1 |

Corroborated structurally: the pin map uses **GPIO 54–61**, which do not exist on
ESP32-S3 at all (it tops out at GPIO 48, with 22–25 absent).

NOTE — a name collision to be aware of: CrossPoint already ships an
`[env:metalio_eink4]` PlatformIO environment. That targets the **3.97"
ESP32-S3 sibling** (`CloudZao/Metalio-E-INK4`, SSD1677, 800x480 SPI, CST816S
touch), **not** the 4.7" ESP32-S31 Plus this project is for. The two share a
brand name and almost nothing else.

### 1.1 Why PlatformIO cannot build this

Verified on the development machine, not assumed:

```
$ grep -rl "esp32s31" ~/.platformio/platforms/espressif32/
(no output)
$ ls ~/.platformio/platforms/espressif32/boards/ | grep -i s31
(no output)
```

pioarduino `espressif32` **55.03.311** ships 299 boards and **zero** `esp32s31`
support. Its `builder/frameworks/espidf.py` hardcodes the known MCU list
(`esp32`, `esp32s2`, `esp32s3`). An attempted build fails with:

```
Unknown MCU: esp32s31
Error: Missing toolchain directory 'None'
```

The Arduino framework is also out: `esp32-core-3.3.11` has **no** mention of
`s31` anywhere in its `boards.txt`, and neither does `arduino-esp32` master. The
installed PlatformIO `framework-espidf` package (5.5.5) also has no
`components/soc/esp32s31/`.

**Conclusion: build with native ESP-IDF.** This is a hard constraint, not a
preference.

---

## 2. Prerequisites

- **ESP-IDF v6.1** — the only line with ESP32-S31 support. The PlatformIO
  `framework-espidf` package (5.5.5) does **not** have
  `components/soc/esp32s31/` and cannot be used.
- ESP32-S31 is flagged **preview** in IDF 6.1. Every `idf.py` invocation that
  touches the target needs `--preview`:

  ```
  esp32s31 is still in preview. You have to append '--preview' option after idf.py
  ```

  Because the target is preview, treat pin/peripheral config as subject to
  change across IDF point releases and re-verify after any upgrade.

Confirm your IDF has the SoC support (with the environment exported, so
`$IDF_PATH` is set):

```bash
ls "$IDF_PATH/components/soc/esp32s31"
```

---

## 3. Build

```bash
. ~/esp/v6.1/export.sh

idf.py --preview set-target esp32s31
idf.py --preview build
```

The `set-target` step is required once per checkout; it regenerates `sdkconfig`
from `crossmetal_sdkconfig.defaults`. Only the defaults file is tracked;
`sdkconfig` is generated and is ignored.

The defaults file is deliberately **not** called `sdkconfig.defaults`.
PlatformIO's cache hook rewrites the first line of `sdkconfig.defaults` in the
project root on every cached build (`scripts/patch_pioarduino_cache.py`), so a
file with that name belongs to PlatformIO in this repo. `CMakeLists.txt` selects
the CrossMetal file explicitly with `set(SDKCONFIG_DEFAULTS ...)`.

A `build/` directory carried over from another machine is not reusable: its
`CMakeCache.txt` pins the absolute source path of the checkout that produced it.
Build into a fresh directory instead (`idf.py -B build-mac --preview build`), or
delete the stale one.

### 3.1 Verified output

This configuration builds clean for `esp32s31` (no warnings), producing:

```
Successfully created ESP32-S31 image.
crossmetal.bin binary size 0x644c0 bytes. Smallest app partition is 0x500000 bytes.
Project build complete.
```

and the image header confirms the target:

```
ESP32-S31 Image Header
Image size: 410576 bytes
Flash size: 16MB
Flash freq: 80m
Flash mode: DIO
Chip ID: 32 (ESP32-S31)
```

These figures were re-measured on macOS with ESP-IDF v6.1 and reproduce the
same sizes; an earlier revision of this document still carried the previous
`0x539b0` / `342448 bytes` values. The image is not bit-identical between
builds because `main.c` embeds `__DATE__` and `__TIME__`.

---

## 4. Flash

### 4.1 Via idf.py

```bash
idf.py --preview -p /dev/cu.usbmodem1101 flash
idf.py --preview -p /dev/cu.usbmodem1101 monitor
```

`/dev/cu.usbmodem1101` is the macOS form; on Linux it is `/dev/ttyACM0`. Find the
real name with `ls /dev/cu.*` (macOS) or `ls /dev/ttyACM* /dev/ttyUSB*` (Linux);
it changes with the machine and the USB port.

### 4.2 Offsets and manual flash

The images and their offsets. These are read from the generated
`<build-dir>/flash_args`, not assumed — in particular the application offset is
**`0x80000`**, not the ESP-IDF default `0x10000`, because this partition table
places `ota_0` at `0x80000`:

| Image | Offset |
|:---|:---|
| `bootloader/bootloader.bin` | `0x2000` |
| `partition_table/partition-table.bin` | `0x8000` |
| `ota_data_initial.bin` | `0xd000` |
| `crossmetal.bin` | **`0x80000`** |

Flashing by hand — note `--chip esp32s31`, which older esptool releases do not
know:

```bash
python -m esptool --chip esp32s31 -b 460800 \
  --before default-reset --after hard-reset --no-stub write-flash \
  --flash-mode dio --flash-size 16MB --flash-freq 80m \
  0x2000  build-mac/bootloader/bootloader.bin \
  0x8000  build-mac/partition_table/partition-table.bin \
  0xd000  build-mac/ota_data_initial.bin \
  0x80000 build-mac/crossmetal.bin
```

Or let IDF assemble the argument list, which is the safer option because it
reads the offsets from the build rather than from this document:

```bash
cd build-mac && python -m esptool --chip esp32s31 write-flash "@flash_args"
```

### 4.3 Partition table

`partitions_metalio.csv` reproduces the upstream 16 MB layout exactly, so NVS
and both OTA slots keep their positions on an already-provisioned device:

| Name | Type/Subtype | Offset | Size |
|:---|:---|:---|:---|
| nvs | data/nvs | `0x9000` | 16 K |
| otadata | data/ota | `0xD000` | 8 K |
| phy_init | data/phy | `0xF000` | 4 K |
| model | data/spiffs | `0x10000` | 400 K |
| ota_0 | app/ota_0 | `0x80000` | 5 M |
| ota_1 | app/ota_1 | `0x580000` | 5 M |
| resources | data/spiffs | `0xA80000` | 400 K |
| font_data | data/`0x40` | `0xAE4000` | 5 M |
| coredump | data/coredump | `0xFE4000` | 64 K |

The partition table itself goes at **`0x8000`**, one sector below the first
entry. If you move it to `0x9000` the generator stops with
`Partitions overlap` — the table occupies the whole sector it is placed in.

---

## 5. Hardware configuration

`crossmetal_sdkconfig.defaults` is transcribed from upstream's `sdkconfig` and
`sdkconfig.defaults.esp32s31`. Getting these wrong is the fastest route to a
boot loop. Verified identical to upstream except where noted:

| Setting | Value |
|:---|:---|
| Target | `esp32s31`, RISC-V |
| Flash | 16 MB, **DIO** (not QIO), 80 MHz, STR |
| PSRAM | **Octal**, 200 MHz, dedicated 1.8 V LDO |
| PSRAM init | on, with boot memtest |
| Internal reserve | 64 KB |
| CPU | **320 MHz** |

> Note on PSRAM capacity: upstream's `sdkconfig` contains **no**
> `CONFIG_SPIRAM_SIZE_*` symbol. Capacity is fixed by the SoC/bootloader and is
> not declared in the repository. `main/main.c` therefore reports
> `esp_psram_get_size()` at boot rather than asserting a figure.

---

## 6. What is ported, and where it lives

```
components/metalio_bsp/       board bring-up (ported, adapted)
  include/metalio_pinout.h   every pin, transcribed from upstream config.h
  src/cm_i2c.c               shared I2C master (with bus-clear bit-bang)
  src/cm_tca9555.c           TCA9555 16-bit expander
  src/cm_tps65185.c          panel PMIC (rails + VCOM)
  src/cm_board.c             cross-chip sequencing, rails, sleep masks
  src/cm_power.c             BQ27220 gauge + CX25601N charger
  src/cm_keys.c              BOOT + expander keys
  src/cm_frontlight.c        dual-channel PWM frontlight
  src/cm_storage.c           SDMMC 4-bit microSD
  src/cm_rtc.c               PCF8563 RTC
  src/cm_touch_ft6336u.c     FT6336U touch + cover-glass virtual keys

components/metalio_epd/       parallel EPDiy display (vendored, see §8)
  include/                   shim headers mapping upstream names onto the BSP
  include/cm_display.h       CrossMetal facade: init, framebuffer, flush
  src/cm_display.c           facade implementation
  src/epdiy.c, render.c, ... vendored upstream, unmodified
  src/output_lcd/            LCD/CAM peripheral parallel bus driver
  src/output_common/         LUT construction and line queues
  src/waveforms/             panel waveform tables

main/main.c                  bring-up sequence, self-report, test pattern
```

---

## 7. Things that will bite you

Collected from the upstream code and comments, because each one cost someone a
debugging session:

1. **Deep-sleep `gpio_hold` survives a reset.** A pin latched low last session
   stays low, and the PMIC boost then fails to start. `cm_board_boot_release_holds()`
   runs first in `cm_board_init()` for this reason.
2. **The panel bus and the SD pins must be parked low before sleep** and released
   at boot, or they leak through the sleep and break the next mount.
3. **TCA P1.2 is shared** between the touch reset, the camera, and the 4G module.
   The 4G needs it **high** to operate; pulling it low kills that rail. All access
   goes through `cm_board_shared_reset_acquire()/release()`.
4. **The expander keys are active low with pull-ups**, and their pins are also
   used as outputs. Driving them low *while the scanner still runs* is read as a
   key press, and the following release emits a spurious page turn. Hence
   `cm_keys_mask_for_sleep()` stops scanning before it changes directions.
5. **The pin map changed between hardware revisions.** `XSTL` moved from GPIO19
   to GPIO4 and `MODE` from GPIO36 to GPIO19 (V1.2 → V1.3/V1.4); GPIO36 became
   `ANA_SW`. `metalio_pinout.h` carries the superseded names as `_V12` so a
   rev-1.2 board can be diffed.
6. **SDMMC pins are IOMUX pads, not package pins.** Upstream says so explicitly:
   schematic package pin 32 is GPIO24, not GPIO32. Do not map package numbers.
7. **SDIO pads on S31 default to the CNNT block**, not GPIO, so releasing them
   requires writing `CNNT_PAD_CTRL` — see `sdio_cnnt_release_to_gpio()`.
8. **The touch INT is a hint, not the truth.** A missed edge must degrade latency,
   not disable input, so the sampling task also polls on a timeout.
9. **Light sleep on S31 needs MPLL/DREG workarounds.** Upstream ships a whole
   `s31_sleep/` module that patches the SDK at build time to follow Espressif fix
   `9726078b`. None of that is ported yet — see §8.

---

## 8. Display driver — parallel EPDiy ported

`components/metalio_epd/` is the ED047TC2 parallel-panel stack, **vendored** from
the Metalio repository (MIT) rather than rewritten.

### 8.1 Why vendored, not reimplemented

The panel has **no discrete controller IC**. It is a raw 8-bit parallel panel
driven by timing signals, so there is no register set to write a clean driver
against — the behaviour lives in EPDiy plus the panel's waveform tables. The
upstream code is the only tested source for that timing, so it is carried over
verbatim and adapted only at the seams.

Upstream spelling is preserved on purpose: it keeps `diff` against the vendor
tree meaningful, which is the reason to vendor rather than reimplement.

### 8.2 The seams

Five shim headers in `include/` map upstream names onto the CrossMetal BSP:

| Shim | Maps to | Why |
|:---|:---|:---|
| `config.h` | `metalio_pinout.h` | upstream pin names → CrossMetal names |
| `metalio_i2c1.h` | `cm_i2c.h` | upstream bus helper → the one shared bus |
| `pca9555.h` | `cm_tca9555.h` | expander accessors |
| `tps65185.h` | `cm_tps65185.h` | PMIC accessors (calibration helpers dropped) |
| `metalio_sd.h` | `cm_storage.h` | only `MetalioSd_ReleaseHold()` is used |

The I²C shim matters most. Upstream ships its *own* bus implementation; binding
the vendored driver to that would create a second `i2c_new_master_bus()` on
`I2C_NUM_1`, which would contend for the same pins and peripheral. Everything is
routed through `cm_i2c` instead, so there is exactly one bus owner.

### 8.3 RISC-V specifics already handled upstream

Two things that look like blockers are already solved in the vendor tree:

- **`lut.S` is Xtensa-only.** It is S3 vector-extension assembly behind
  `#ifdef CONFIG_IDF_TARGET_ESP32S3`, i.e. empty on RISC-V. It is not built here.
  `epdiy_riscv_ve_stub.c` supplies equivalent C implementations.
- **Byte order differs.** `lut.c` has a dedicated
  `#if defined(CONFIG_IDF_TARGET_ESP32S31)` path in `lookup_pixels_in_VE_LUT`,
  because the panel eats the high 2 bits of each byte first and the little-endian
  pack order differs from Xtensa. Getting this wrong produces vertical ghosting
  inside every 4-pixel group.

`render_method.h` also selects `RENDER_METHOD_LCD` for `ESP32S31`, so the
LCD/CAM peripheral path is the one compiled.

### 8.5 Two runtime defects found by auditing the seam

Both would have compiled and linked cleanly, then failed on hardware.

**Two shadows of one expander register.** The vendored display glue keeps its own
copy of both TCA9555 output ports, and so did `cm_board`. Whichever layer wrote
last won, and the other's stale copy then clobbered it. Concretely: after
`epd_poweroff()` the board's image still believed the panel was powered, so a
later motor or camera-rail write would **re-energise the panel**.

Fixed by funnelling every output write through `cm_board_ioexp_write_port()`,
which merges into one authoritative image under a mutex. The vendored file stays
byte-for-byte unmodified: the routing happens in `include/pca9555.h`, scoped by
`EPDIO_PCA9555_BOARD_OWNED`. Both layers' direction masks were checked and agree
(P0 = `0x3F`, P1 = `0x1C`).

**The display driver steals the expander interrupt.** `epd_init()` attaches its
own handler to GPIO2, and the GPIO ISR service keeps one handler per pin, so ours
was silently replaced. Key scanning would have degraded to a 50 ms poll. Fixed by
`cm_board_ioexp_irq_reclaim()`, called once after display init.

The power-on path is unaffected by that reclaim: it polls PWR_GOOD through the
expander rather than waiting on the interrupt.

**The framebuffer is 1.59 MB, not 0.8 MB.** `epd_hl_init()` allocates front, back
and a *difference* buffer at one byte per pixel:

```
fb_size       = 1216/2 * 684 = 415,872 B
front + back  = 831,744 B
difference    = 2 * fb_size = 831,744 B
total         = 1,663,488 B ≈ 1.59 MB
```

An earlier budget check in `main.c` used 831 KB and would have under-estimated by
half. Worse, `epd_hl_init()` validates with `assert()`, and this build has
assertions at level 2 (abort), so a shortfall is a **boot loop**, not a message.
`check_framebuffer_budget()` now computes the real figure from the allocation
pattern, refuses to proceed if it does not fit, and additionally proves a
contiguous block of that size can actually be allocated.

### 8.6 Memory

The build reports DIRAM 22.95% used (388 KB free of 503 KB) and 329 KB of flash.
The framebuffers come from PSRAM, not internal RAM.

---

## 9. Not yet done

Scope so far is hardware bring-up plus the display driver. Still outstanding, in
the order they would be tackled:

1. ~~**Display driver.**~~ Done — see §8. `cm_display_init()` brings the panel up
   and `main.c` pushes a geometry test pattern on boot.
2. **Sleep and power policy.** The S31 light-sleep workarounds (MPLL calibration,
   PSRAM clock handoff, the SDK patch) are a research task in their own right.
3. **Reader stack integration.** Wiring CrossPoint's EPUB library and UI onto the
   framebuffer through `cm_display.h`. Note the UI is portrait 684x1216 while the
   panel is native landscape 1216x684; the transform is the same transpose the
   touch driver applies (`x_p = y_l`, `y_p = 683 - x_l`).
4. **On-device validation.** Everything compiles and links; nothing has run on
   hardware yet.

### 9.1 What the bring-up firmware now does

`main.c` brings up the board in the vendor's order, then:

1. Logs chip, PSRAM size and heap.
2. Verifies a 1,663,488-byte display budget (front, back and 2x difference
   buffers) can be allocated from PSRAM.
3. Initialises EPDiy, sets VCOM, and powers on the panel (leaving it powered, as
   every draw needs the rails up). Confirms the PMIC reports PWR_GOOD.
4. Draws a **geometry test pattern**: white field, 4 px black border on all four
   edges, both diagonals, and a centred cross. Then a full `GC16` flush.

Read the panel after flashing. A correct pattern has the border flush with every
edge and both diagonals meeting at the corners. Rotated, mirrored or sheared
output means the logical-to-physical transform is wrong. A blank or half-drawn
panel points at the bus timing or the TPS rails, not the geometry.

---

## 10. Attribution

The board layer is ported from **Metalio-E-INK4-Plus**, MIT licensed:

```
Copyright (c) 2025 Shenzhen Xinzhi Future Technology Co., Ltd.
Copyright (c) 2025 Project Contributors
```

See `NOTICE.md`. File-level provenance is recorded in each ported source's header
comment. CrossPoint's own license governs the reader stack.