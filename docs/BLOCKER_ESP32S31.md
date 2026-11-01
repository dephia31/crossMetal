# Blocker analysis: ESP32-S31 vs ESP32-S3 vs CrossPoint's stack

**Status: the brief's premise needs one correction before any code is written.**

## 1. The correction

The brief states two things that are in tension:

- §"YÊU CẦU CỐT LÕI" item 2: *"Thiết bị sử dụng **ESP32-S3**."*
- §GIAI ĐOẠN 2: *"Cấu hình chuẩn `board = esp32-s3-devkitc-1`"*

The hardware repository says otherwise, unambiguously and in three independent places:

| Evidence | File | Value |
|:---|:---|:---|
| Kconfig symbol | `sdkconfig` | `CONFIG_IDF_TARGET="esp32s31"` |
| CPU architecture | `sdkconfig` | `CONFIG_IDF_TARGET_ARCH="riscv"` |
| Vendor README | `README.md` §1 | "**MCU**: ESP32-S31 (RISC-V; Flash 16 MB, Octal PSRAM @ 200 MHz)" |
| Vendor README | `README.md` §1 note | "Plus moves to **ESP32-S31** … **Do not reuse the 397 SPI pin map**" |

Corroborated structurally: `config.h` uses **GPIO 54–61**, which do not exist on
ESP32-S3 (max GPIO 48, with 22–25 absent).

**ESP32-S3 is Xtensa LX7. ESP32-S31 is RISC-V.** They are different ISAs with
different toolchains. A binary built for one will not boot on the other.

## 2. The name collision

CrossPoint's `platformio.ini` already contains:

```ini
[env:metalio_eink4]
board = esp32-s3-devkitc1-n16r8
-D FREEINK_DEVICE_METALIO_EINK4=1
```

and `freeink-sdk` defines `METALIO_EINK4` as:

```cpp
// BoardConfig.h:1723
constexpr BoardProfile METALIO_EINK4 = {
    Board::MetalioEInk4, "metalio_eink4", InputStyle::DigitalButtons,
    DisplayController::SSD1677, 800, 480, ...   // ← SPI, SSD1677, 800x480
```

Compare against **Metalio E-Ink 4** (the *sibling* repo, `CloudZao/Metalio-E-INK4`):

```c
// main/boards/metalio-e-ink-4/config.h
#define DISPLAY_WIDTH  800
#define DISPLAY_HEIGHT 480                 // GDEM0397T81 3.97" 800x480 SSD1677
#define EPD_PIN_MOSI   GPIO_NUM_8
#define EPD_PIN_CS     GPIO_NUM_45
#define SDMMC_D0_PIN   GPIO_NUM_39
```

`CONFIG_IDF_TARGET="esp32s3"` and the pin map match **exactly**.

**Conclusion.** CrossPoint's `metalio_eink4` profile targets the **3.97" ESP32-S3
sibling**, not the 4.7" ESP32-S31 Plus. The two devices share a brand name and
almost nothing else.

## 3. What breaks, specifically

| Layer | ESP32-S3 (CrossPoint today) | ESP32-S31 (the Plus) | Port cost |
|:---|:---|:---|:---|
| ISA / toolchain | Xtensa LX7, `toolchain-xtensa-esp-elf` | **RISC-V**, `toolchain-riscv32-esp` | toolchain swap |
| HAL | Arduino-ESP32 3.3.11 (pioarduino 55.03.311) | **ESP-IDF 6.1.0 native** | see §4 |
| GPIO range | 0–48 | includes **54–61** | pin map invalid |
| Display bus | SPI + controller IC (SSD1677/UC8179) | **parallel 8-bit, no IC** (EPDiy + TPS65185) | new driver class |
| Panel geometry | 800×480 | **1216×684** | framebuffer sizing |
| Touch | CST816S @ 0x15 | **FT6336U @ 0x38** | new driver |
| Buttons | direct GPIO | **TCA9555 expander** | new input layer |
| Battery | ADC / AXP2101 | **BQ27220 I²C gauge** | new power path |
| Frontlight | none in the S3 profile | **GPIO 6/7 PWM** | new subsystem |
| RTC | PCF8563/PCF85063 @ 0x51 | PCF8563 @ 0x51, INT via expander | mostly reusable |
| SD | SDMMC 1-bit | SDMMC **4-bit** | config change |

### 3.1 PlatformIO cannot build ESP32-S31 as installed

Verified on this machine:

```
$ grep -rl "esp32s31" ~/.platformio/platforms/espressif32/
(no output)
$ ls ~/.platformio/platforms/espressif32/boards/ | grep -i s31
(no output)
```

pioarduino `espressif32` **55.03.311** (Arduino core 3.3.11) has:
- 299 boards, **zero** `esp32s31`
- no `esp32s31` string anywhere in the platform

So `board = esp32-s3-devkitc-1` plus a custom `board_build.mcu = esp32s31` will
**not** produce a bootable image. The `board_build.mcu` value must correspond to
a target the platform's framework and toolchain actually export.

### 3.2 The sleep path is genuinely hard

`main/boards/metalio-e-ink4-plus/README.md` documents an entire module,
`common/power_policy/s31_sleep/`, whose only job is making light-sleep work on
S31 under IDF 6.1:

- MPLL calibration restore (DREG/PVT → `12/12`) before every wake calibration
- PSRAM clock handoff: switch to XTAL before MPLL off, back after MPLL on
- Tracks the upstream Espressif fix `9726078b` (`improve mpll calibration flow
  on esp32s31`) via a **build-time SDK patch** (`s31_sleep_sdk.cmake`)
- Requires re-verification on every IDF upgrade

This is not portable logic — it is workaround code coupled to a specific IDF
revision. Reproducing it outside the vendor tree is a research task, not a port.

## 4. Recommended paths

### Path A — "CrossMetal" for the **Plus** (what was asked)

Build **native ESP-IDF 6.1**, not PlatformIO-Arduino.

1. `platform = espressif32` pinned to a release that exports `esp32s31`, **or**
   `framework = espidf` against the local `~/esp/v6.1` via a custom board JSON.
2. Port the Metalio board layer (see the file plan) into CrossPoint's HAL.
3. Seed `crossmetal_sdkconfig.defaults` from Metalio's `sdkconfig.defaults.esp32s31` —
   especially the MPLL/PSRAM sleep block — or the device will not resume from
   light sleep.
4. Replace CrossPoint's SPI `EpdBus` with a parallel EPDiy backend.

**Effort:** large. Display + input + power layers are all new. The Arduino HAL
must be replaced or shimmed. Estimate: weeks, with hardware iteration.

### Path B — "CrossMetal" for the **3.97" sibling**, today

CrossPoint already supports it (`pio run -e metalio_eink4`). This satisfies
Giai đoạn 2–4 immediately and is a genuine "Metalio" target, just not the Plus.

### Path C — **Reuse the Plus's own firmware, add CrossPoint's reader**

Metalio is ESP-IDF-native and already has a working S31 bring-up, EPDiy driver,
TPS65185, FT6336U, frontlight, keys, power and sleep. Instead of lifting all of
that into CrossPoint's Arduino stack, lift **CrossPoint's reader** (EPUB engine,
library, UI) into the Metalio tree. This inverts the integration direction and is
likely **less total work** than Path A.

## 5. What is and is not in scope

- The repo does **not** contain `main/boards/<board id>/config*.h`-verified
  schematic PDFs, so `config.h` + `epd_board_metalio_eink4_plus.*` are the
  ceiling of provable hardware truth. They are internally consistent and
  cross-checked against the vendor README.
- PSRAM **capacity** is not declared anywhere. Do not state a number.
- The `DEPG0397*` datasheet belongs to the sibling device (see hardware sheet §8).