# CrossMetal — ESP-IDF project for the Metalio E-Ink4-Plus.
#
# The target is ESP32-S31. PlatformIO's pioarduino espressif32 platform does not
# support this SoC (no board, no MCU mapping, no Arduino variant), so this
# project builds with native ESP-IDF 6.1 and must be driven by `idf.py`.
# See docs/CM_BOARD_PORT.md for the full rationale.
#
#   . $IDF_PATH/export.sh
#   idf.py --preview set-target esp32s31
#   idf.py --preview build

cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.1.0")

# A distinct defaults filename, because PlatformIO's cache hook overwrites the
# first line of sdkconfig.defaults on every cached build (scripts/patch_pioarduino_cache.py)
# and this repo still builds other boards with PlatformIO.
set(SDKCONFIG_DEFAULTS "${CMAKE_SOURCE_DIR}/crossmetal_sdkconfig.defaults")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(crossmetal)