#!/usr/bin/env bash
# Back up the entire 16 MB flash of a Metalio E-Ink4-Plus before flashing
# CrossMetal, so the stock firmware can be restored.
#
# Why a full-image backup rather than per-partition: the device's calibration and
# settings live in NVS (0x9000) and the model/resources partitions. Reflashing a
# stock *release* binary would not restore those. A whole-chip dump does.
#
# Usage:
#   ./scripts/backup_flash.sh /dev/cu.usbmodem1101      # macOS
#   ./scripts/backup_flash.sh /dev/ttyACM0              # Linux
#   ./scripts/backup_flash.sh /dev/cu.usbmodem1101 metalio-stock-16mb.bin
#
# Restore:
#   ./scripts/backup_flash.sh --restore /dev/cu.usbmodem1101 metalio-stock-16mb.bin

set -euo pipefail

FLASH_SIZE_BYTES=$((16 * 1024 * 1024))

usage() {
    cat <<'EOF'
Usage:
  backup_flash.sh <port> [output.bin]          back up the whole 16 MB flash
  backup_flash.sh --restore <port> <image.bin> write a backup back

Examples:
  ./scripts/backup_flash.sh /dev/cu.usbmodem1101       # macOS
  ./scripts/backup_flash.sh /dev/ttyACM0               # Linux
  ./scripts/backup_flash.sh --restore /dev/cu.usbmodem1101 metalio-stock-16mb.bin

Find the port first: `ls /dev/cu.*` on macOS, `ls /dev/ttyACM* /dev/ttyUSB*`
on Linux.

Notes:
  - Reading the full 16 MB takes a few minutes.
  - The backup is a complete chip image, including NVS and calibration.
  - Requires ESP-IDF's esptool on PATH (`idf.py` env) and `--chip esp32s31`.
EOF
}

# File size in bytes. BSD stat (macOS) spells it -f %z, GNU stat spells it -c %s.
file_size() {
    stat -f %z "$1" 2>/dev/null || stat -c %s "$1"
}

require_esptool() {
    if ! command -v python >/dev/null 2>&1; then
        echo "error: python not found" >&2
        exit 1
    fi
    if ! python -m esptool version >/dev/null 2>&1; then
        echo "error: esptool not importable. Source the IDF environment first:" >&2
        echo "       . \"\$IDF_PATH/export.sh\"" >&2
        exit 1
    fi
}

do_backup() {
    local port="$1"
    local out="${2:-metalio-stock-$(date +%Y%m%d-%H%M%S).bin}"

    if [ -e "$out" ]; then
        echo "error: '$out' already exists; refusing to overwrite a backup" >&2
        exit 1
    fi

    echo "Reading 16 MB from $port -> $out"
    echo "(this takes a few minutes; do not unplug)"

    python -m esptool --chip esp32s31 -p "$port" -b 460800 \
        --before default-reset --after hard-reset \
        read-flash 0 "$FLASH_SIZE_BYTES" "$out"

    local size
    size=$(file_size "$out")
    if [ "$size" -ne "$FLASH_SIZE_BYTES" ]; then
        echo "error: backup is $size bytes, expected $FLASH_SIZE_BYTES" >&2
        exit 1
    fi

    echo
    echo "Backup complete: $out ($size bytes)"
    echo "Keep this file. It is the only way back to the stock firmware with its"
    echo "NVS and calibration intact."
}

do_restore() {
    local port="$1"
    local image="$2"

    if [ ! -f "$image" ]; then
        echo "error: '$image' not found" >&2
        exit 1
    fi
    local size
    size=$(file_size "$image")
    if [ "$size" -ne "$FLASH_SIZE_BYTES" ]; then
        echo "error: '$image' is $size bytes, expected a full $FLASH_SIZE_BYTES-byte image" >&2
        exit 1
    fi

    echo "This will overwrite the ENTIRE flash on $port with $image."
    read -r -p "Type 'restore' to continue: " confirm
    [ "$confirm" = "restore" ] || { echo "aborted"; exit 1; }

    python -m esptool --chip esp32s31 -p "$port" -b 460800 \
        --before default-reset --after hard-reset \
        write-flash 0 "$image"

    echo "Restore complete."
}

main() {
    case "${1:-}" in
        -h|--help|"")
            usage
            exit 0
            ;;
    esac

    require_esptool

    case "${1:-}" in
        --restore)
            [ $# -eq 3 ] || { usage; exit 1; }
            do_restore "$2" "$3"
            ;;
        *)
            do_backup "$@"
            ;;
    esac
}

main "$@"