# Hardware Specification Sheet — Metalio E-Ink4-Plus

> **Provenance.** Every value below is transcribed from the `Metalio-E-INK4-Plus`
> repository (commit at clone time, `CMakeLists.txt` PROJECT_VER `2.0.66`), and
> cross-checked against that repo's own `README.md` §5/§7. Files cited inline are
> the authority. Nothing here is inferred from the panel datasheet or from the
> sibling Metalio E-Ink 4 board unless explicitly marked as a cross-reference.
>
> Primary truth file: `main/boards/metalio-e-ink4-plus/config.h`
> Board glue:          `main/boards/metalio-e-ink4-plus/display/epd_board_metalio_eink4_plus.c`
> SDK config:          `sdkconfig` / `sdkconfig.defaults.esp32s31`

---

## 1. SoC, memory and boot configuration

**This is the single most important section — it is where a naive port fails.**

| Item | Value | Source |
|:---|:---|:---|
| Target SoC | **ESP32-S31** | `sdkconfig`: `CONFIG_IDF_TARGET="esp32s31"` |
| Architecture | **RISC-V** | `CONFIG_IDF_TARGET_ARCH="riscv"` |
| Firmware chip ID | `0x0020` | `CONFIG_IDF_FIRMWARE_CHIP_ID=0x0020` |
| ESP-IDF version | **6.1.0** | `CONFIG_IDF_INIT_VERSION="6.1.0"` |
| Flash size | **16 MB** | `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y` |
| Flash mode | **DIO** (not QIO) | `CONFIG_ESPTOOLPY_FLASHMODE="dio"` |
| Flash freq | **80 MHz** | `CONFIG_ESPTOOLPY_FLASHFREQ="80m"` |
| Flash sample mode | STR | `CONFIG_ESPTOOLPY_FLASH_SAMPLE_MODE_STR=y` |
| PSRAM | **Octal (OPI)** | `CONFIG_SPIRAM_MODE_OCT=y` |
| PSRAM speed | **200 MHz** | `CONFIG_SPIRAM_SPEED_200M=y` |
| PSRAM integration | malloc | `CONFIG_SPIRAM_USE_MALLOC=y` |
| PSRAM boot init | on | `CONFIG_SPIRAM_BOOT_INIT=y`, `CONFIG_SPIRAM_BOOT_HW_INIT=y` |
| PSRAM dedicated LDO | **yes — 1.8 V** | `CONFIG_SOC_PSRAM_HAS_DEDICATED_LDO=y`, `CONFIG_ESP_LDO_VOLTAGE_PSRAM_1800_MV=y` |
| PSRAM memtest at boot | enabled | `CONFIG_SPIRAM_MEMTEST=y` |
| Internal reserve | 64 KB | `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=65536` |
| `malloc` always-internal threshold | 16 B | `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16` |

> **Note on PSRAM capacity.** `sdkconfig` contains **no** `CONFIG_SPIRAM_SIZE_*`
> symbol. Capacity is not declared anywhere in the repo; it is fixed by the SoC /
> bootloader. The `README.md` §1 states "Octal PSRAM @ 200 MHz" without a size.
> **Do not invent a size** — read it at runtime (`esp_psram_get_size()`) or from
> the boot ROM log.

### 1.1 Why this blocks a straight port

ESP32-S31 is **not** ESP32-S3. They differ in ISA (RISC-V vs Xtensa LX7), in
GPIO range, in USB personality, and in clock/sleep architecture:

- `config.h` legally references **GPIO 54, 55, 56, 57, 58, 59, 60, 61**
  (camera D0/PCLK/MCLK/VSYNC/HREF, NT26 modem UART + MRDY, BOOT button).
  ESP32-S3 only has GPIO 0–48, with 22–25 absent. **This pin map cannot exist on
  an S3.**
- A working `sdkconfig` needs `CONFIG_IDF_TARGET=esp32s31`, which requires ESP-IDF
  ≥ 6.1 (this machine has `~/esp/v6.1` with `components/soc/esp32s31/` present).
- The pioarduino PlatformIO platform installed here (**espressif32 55.03.311**,
  Arduino core 3.3.11) contains **no `esp32s31` board and no `esp32s31` support
  anywhere** — verified by `grep -rl esp32s31 ~/.platformio/platforms/espressif32/`
  returning nothing. It cannot build this target as shipped.

---

## 2. Display subsystem — parallel EPDiy (NOT SPI)

| Item | Value | Source |
|:---|:---|:---|
| Panel | **ED047TC2**, 4.7" | `config.h:6`; `builtin_waveforms.c:7` |
| Board-specific panel ID | `E0470A14_AF`, 34-pin | `metalio_epd.c:31,37` |
| Native resolution | **1216 × 684** (landscape) | `config.h:6` |
| Logical UI resolution | **684 × 1216** (portrait) | `DISPLAY_WIDTH=684`, `DISPLAY_HEIGHT=1216` |
| UI content insets | 2 px on all four sides | `DISPLAY_CONTENT_*_INSET 2` |
| Logical content size | 680 × 1212 | derived from `DISPLAY_CONTENT_W/H` macros |
| White border pixels | 0 | `DISPLAY_WHITE_BORDER_PIXELS 0` |
| **Bus type** | **Parallel 8-bit (i80-style)** | `lcd_bus_config_t` in `epd_board_metalio_eink4_plus.c:112` |
| Controller IC | **No discrete controller** — raw parallel panel via EPDiy | `epdiy.c`, `epdiy.h` |
| Panel PMIC | **TPS65185** (boost + VCOM) | `bus/tps65185.c` |
| VCOM | **1200 mV** | `config.h:22` `EPD_VCOM_MV 1200` |
| LUT / waveform | `epdiy_ED047TC2` + `my_waveform*.h` (LUT table) | `waveforms/`; `EPD_LUT_1K` in `metalio_epd.c:37` |
| Dummy clocks after data | 10 (E0470A14-AF §6.3.1) | `epd_board_metalio_eink4_plus.c:388` |
| Refresh modes | full / partial; Bayer dither gray | `docs/epd-bayer-dither-gray.md` |

### 2.1 Parallel bus pins (`config.h:44-62`, hardware rev V1.3/V1.4)

| Signal | GPIO | Note |
|:---|:---:|:---|
| D0–D7 | 8, 9, 10, 11, 12, 13, 14, 15 | parallel data |
| XCL (CKH) | 16 | clock |
| XLE (LEH) | 17 | latch enable |
| XOE | 18 | output enable |
| XSTL (STH) | **4** | start pulse — **V1.3/1.4 relocated** (V1.2 used 19) |
| MODE | **19** | **V1.4** (V1.2 used 36) |
| CKV | 40 | |
| SPV (STV) | 35 | |
| BORDER | 37 | |

> Two commented-out blocks in `config.h:25-40` record the **V1.2** assignments.
> The active block is V1.3/V1.4. Pin conflicts to watch: **19** and **36** were
> reused between revisions, and **GPIO 36** is now `ANA_SW`.

### 2.2 TPS65185 control via TCA9555

`config.h:64-69` — the TPS65185's control lines are **not** on ESP GPIOs; they go
through the TCA9555 I²C expander.

| Function | TCA pin | Direction |
|:---|:---|:---|
| VCOM_CTRL | P0.0 | OUT |
| PWRUP | P0.1 | OUT |
| WAKEUP | P0.2 | OUT |
| PWR_GOOD | P0.7 | IN |
| (INT) | P1.7 — note: **now used as the power-key input** | IN |

---

## 3. I²C bus and device map

| Item | Value | Source |
|:---|:---|:---|
| SDA | **GPIO 0** | `config.h:82` |
| SCL | **GPIO 1** | `config.h:83` |
| Expander IRQ | **GPIO 2** | `config.h:84` `IO_EXPANDER_INT_GPIO` |
| Bus instance | `I2C_NUM_1` | `epd_board_metalio_eink4_plus.c:33` |

All devices below share this one bus (`config.h:85` comment):

| Device | 7-bit addr | Role |
|:---|:---:|:---|
| **TCA9555** | `0x20` | 16-bit I/O expander |
| **FT6336U** | `0x38` | Capacitive touch |
| **PCF8563** | `0x51` | RTC |
| **BQ27220** | `0x55` | Fuel gauge |
| **CX25601N** | `0x6B` | Charger |
| **SC7A20H** | `0x19` (alt `0x18`) | Accelerometer |
| TPS65185 | — | panel PMIC (control via TCA) |

> `config.h:85-86` annotates the expander comment as "同 I2C1" (same as I²C1).
> The repo's `README.md` §7.3 lists the same addresses; `TCA9555 @ 0x20` is
> corroborated by `datasheet/tca9555.pdf` being shipped in-repo.

---

## 4. Input

### 4.1 Physical buttons

| Button | Location | GPIO / expander pin | Source |
|:---|:---|:---|:---|
| BOOT | ESP GPIO | **GPIO 61** | `config.h:112` |
| POWER | expander | **P1.7** | `config.h:113` `EPD_IO_POWER 15` |
| VOL+ | expander | **P1.0** | `config.h:114` `EPD_IO_VOL_UP 8` |
| VOL− | expander | **P1.1** | `config.h:115` `EPD_IO_VOL_DOWN 9` |
| PWR_PULSE (out) | expander | **P1.3** | `config.h:116` |
| RTC_INT (in) | expander | **P1.6** | `config.h:87` |

Long-press timings (`config.h:117-119`):
- `POWER_LONG_PRESS_MS` = 3000
- `PWR_PULSE_INTERVAL_MS` = 100 (high and low each)
- `PWR_PULSE_COUNT` = 15

> **GPIO 61** is one of the pins impossible on ESP32-S3 — a hard confirmation of
> the S31 target.

### 4.2 Touch — FT6336U

| Item | Value | Source |
|:---|:---|:---|
| Controller | FT6336U | `config.h:94` |
| I²C address | `0x38` | `config.h:95` `TOUCH_I2C_ADDR` |
| INT | **GPIO 5** | `config.h:96` |
| RST | TCA **P1.2** (`EPD_IO_TP_RST 10`) | `config.h:97` |
| RST polarity | **active high** (low = powered off) | `config.h:97` comment |
| Firmware upgrade | yes — `ft6336u_v03_app.bin` shipped | `display/firmware/` |

**Cover-glass virtual keys** (`config.h:103-109`) — reported by the FT6336U at a
Y coordinate *outside* the visible panel:

| Key | X | Y | Notes |
|:---|:---:|:---:|:---|
| HOME | 100 | 1600 | |
| PREV (back / page up) | 200 | 1600 | |
| NEXT (page down) | 300 | 1600 | |

`TOUCH_VK_TOL` = 30 (hit tolerance), `TOUCH_VK_LONG_MS` = 500.

> **Shared-reset hazard.** TCA P1.2 is shared between the touch controller, the
> GC2145 camera RST, and the 4G module. `epd_board_metalio_eink4_plus.h:32-36`
> documents a lease/mutex (`..._camera_acquire` / `..._camera_release`) because
> pulling it low kills the 4G rail. The 4G must see it **high** to work.

---

## 5. Power, battery and charging

| Item | Value | Source |
|:---|:---|:---|
| Fuel gauge | **BQ27220** @ `0x55` | `periph/metalio_power.cc`; `common/power/bq27220_gauge.*` |
| Charger | **CX25601N** @ `0x6B`, default 1000 mA | `common/power/cx25601n.*`; README §2.5 |
| SOC model | **voltage-linear**, not coulomb-count | `docs/battery-soc-and-charge.md` |
| Panel PMIC | **TPS65185** (VCOM/PWRUP/WAKEUP via TCA) | `bus/tps65185.c` |
| Power key | TCA **P1.7** | `config.h:113` |
| Soft power-off | pulse TCA **P1.3**, 15 pulses × 100 ms | `config.h:116-119` |
| Camera/screen rail | TCA **P0.5** | `config.h:72` `EPD_IO_CAM_SCR_EN 5` |
| BT/PA rail | TCA **P1.4** | `config.h:100` `EPD_IO_BT_PA_PWR 12` |
| Analog switch (USB MSC path) | **GPIO 36** | `config.h:101` `ANA_SW_GPIO` |

Idle policy (README §2.5, `PowerPolicy`): idle → light-sleep standby after
**3 min** (10/30 optional); standby → power-off after a further **3 min**;
AppIdle CPU 80/160/240 MHz (default 240).

---

## 6. Storage

| Item | Value | Source |
|:---|:---|:---|
| microSD | **SDMMC 4-bit** | `config.h:125-139`; README §5 |
| CLK | **GPIO 24** | `SDMMC_CLK_PIN` |
| CMD | **GPIO 25** | `SDMMC_CMD_PIN` |
| D0 | **GPIO 20** | `SDMMC_D0_PIN` |
| D1 | **GPIO 21** | `SDMMC_D1_PIN` |
| D2 | **GPIO 22** | `SDMMC_D2_PIN` |
| D3 | **GPIO 23** | `SDMMC_D3_PIN` |

> `config.h:126-133` is explicit that these are the **IOMUX slot-0** SDIO pins:
> *"原理图 U5 专用 SDIO（封装脚≠GPIO 号）"* — the schematic package-pin numbers
> are **not** the GPIO numbers. Do not map package pin 32→GPIO32.

### 6.1 Flash partition table (`partitions.csv`, 16 MB)

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

---

## 7. Frontlight, haptics, audio and other peripherals

| Subsystem | Detail | Source |
|:---|:---|:---|
| Frontlight cool | **GPIO 6** (YX6016N EN/PWM) | `config.h:122` `FL_COOL_PIN` |
| Frontlight warm | **GPIO 7** | `config.h:123` `FL_WARM_PIN` |
| Vibration motor | TCA **P0.3**, active high | `config.h:70` `EPD_IO_MOTOR 3` |
| Motor default pulse | 50 ms | `config.h:80` `VIBRATION_MOTOR_PULSE_MS` |
| Audio PA enable | TCA **P0.4** (NS4150 CTRL) | `config.h:71` `EPD_IO_PA_EN 4` |
| I²S BCLK | **GPIO 42** | `config.h:148` |
| I²S WS | **GPIO 43** | `config.h:149` |
| I²S DOUT | **GPIO 44** | `config.h:150` |
| I²S DIN | **GPIO 45** | `config.h:151` |
| I²S role | **ESP is slave** ← external BT codec clock | `config.h:143` comment |
| Audio sample rate | 16 kHz in / 16 kHz out | `config.h:146-147` |
| BT audio chip | BH1098G, UART AT | `config.h:142` |
| BT UART TX | **GPIO 39** | `config.h:153` |
| BT UART RX | **GPIO 38** | `config.h:154` |
| RTC | PCF8563 @ `0x51`, INT → TCA P1.6 | `config.h:87-89` |
| RTC INT ESP pin | **none** (`GPIO_NUM_NC`) | `config.h:88` — rev 1.4 routes it via TCA |
| Accelerometer | SC7A20H, INT wired-OR to expander INT → GPIO 2 | `config.h:90-92` |

### 7.1 Cellular / camera (present in pin map, outside reader scope)

| Function | Pin | Source |
|:---|:---|:---|
| NT26 TX | **GPIO 58** | `config.h:157` |
| NT26 RX | **GPIO 59** | `config.h:158` |
| NT26 MRDY | **GPIO 60** | `config.h:159` |
| NT26 SRDY | **GPIO 3** | `config.h:160` |
| Camera SCCB addr | `0x3C` | `config.h:73` |
| Camera D0–D7 | **GPIO 46** | `config.h:74` |
| Camera PCLK | **GPIO 54** | `config.h:75` |
| Camera MCLK | **GPIO 55** (20 MHz) | `config.h:76,79` |
| Camera VSYNC | **GPIO 56** | `config.h:77` |
| Camera HREF | **GPIO 57** | `config.h:78` |
| Camera RST | shared with TCA P1.2 | `config.h:99` |

---

## 8. Repo hygiene notes (discrepancies found — flagged, not resolved)

These are things a port must **not** silently assume:

1. **`datasheet/DEPG0397BBS77AF11HP-2.1-GY.pdf`** is a **3.97"** panel datasheet.
   The E-Ink4-**Plus** uses the **4.7" ED047TC2 / E0470A14-AF**. The sibling
   product *Metalio E-Ink 4* is the 3.97" device (`GDEM0397T81`, 800×480,
   SSD1677). This datasheet therefore is consistent with the **sibling**, not
   with the Plus; treat it as stale/shared-inherited. **The authoritative source
   for the Plus panel is the code, not this PDF.**
2. **`datasheet/tca9555.pdf` is genuinely relevant** — the Plus uses a TCA9555.
3. `config.h:16-18`/`19-21` contain a **contradictory comment** ("负 Y=上移")
   against both offsets being `0`. Cosmetic, but shows the file is hand-edited.
4. The repo contains **no schematic PDF**. README §8 defers: *"If schematics are
   published elsewhere, follow the hardware team docs."* `config.h` and
   `epd_board_metalio_eink4_plus.*` are declared to be **"firmware pin truth"**.
5. Two hardware revisions share one `config.h`; the active defines are V1.3/V1.4.
6. Firmware lineage is **xiaozhi-esp32** (README §9), a China-market AI-assistant
   stack — architecturally unrelated to CrossPoint (see the porting analysis).

---

## 9. Cross-reference: the two Metalio devices

| | **Metalio E-Ink 4** (sibling) | **Metalio E-Ink 4 Plus** (this repo) |
|:---|:---|:---|
| SoC | ESP32-S3 | **ESP32-S31** |
| Panel | 3.97" **800×480 SPI**, SSD1677 | 4.7" **1216×684 parallel**, ED047TC2 |
| Panel interface | SPI (CLK/MOSI/CS/DC/RST/BUSY) | **parallel 8-bit**, EPDiy |
| Touch | CST816S @ `0x15` | **FT6336U @ `0x38`** |
| Frontlight | none | **cool GPIO6 / warm GPIO7** |
| SD | SDMMC 1-bit | **SDMMC 4-bit** |
| I²C | GPIO 41/42 | **GPIO 0/1** |
| Buttons | BOOT/POWER on GPIO | BOOT GPIO61 + **TCA9555 expander** |
| CrossPoint env | `metalio_eink4` (**exists**) | **does not exist** |

> Metalio's own README is emphatic: *"Do not reuse the 397 SPI pin map."*