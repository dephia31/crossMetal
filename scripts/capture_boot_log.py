#!/usr/bin/env python3
"""Capture the boot log from a Metalio E-Ink4-Plus over serial.

Writes to stdout and to a timestamped file at the same time, so the log can be
read live and attached to a bug report afterwards.

Why not just `idf.py monitor | tee`: the monitor is an interactive pager, and
piping it loses the first lines (which are the ones that matter for a boot
failure) while also mangling terminal control sequences.

Usage:
    ./scripts/capture_boot_log.py /dev/cu.usbmodem1101     # macOS
    ./scripts/capture_boot_log.py /dev/ttyACM0             # Linux
    ./scripts/capture_boot_log.py /dev/cu.usbmodem1101 --seconds 30 -o mylog.txt
    ./scripts/capture_boot_log.py /dev/cu.usbmodem1101 --reset  # try DTR/RTS reset

Run it, then press the board's reset button (or power-cycle it) so the capture
starts before the first line. Press Ctrl+C to stop early; the file is kept.
"""

import argparse
import datetime
import os
import sys
import time

try:
    import serial
except ImportError:
    sys.stderr.write(
        "error: pyserial not found.\n"
        "Source the ESP-IDF environment first:  . \"$IDF_PATH/export.sh\"\n"
    )
    sys.exit(1)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", help="serial device, e.g. /dev/cu.usbmodem1101 (macOS) "
                                 "or /dev/ttyACM0 (Linux)")
    ap.add_argument("-b", "--baud", type=int, default=115200,
                    help="baud rate (default 115200)")
    ap.add_argument("-o", "--output", default=None,
                    help="output file (default: bootlog-<timestamp>.log)")
    ap.add_argument("--seconds", type=int, default=45,
                    help="how long to capture (default 45; Ctrl+C stops early)")
    ap.add_argument("--reset", action="store_true",
                    help="pulse DTR/RTS to reset the board before capturing")
    ap.add_argument("--wait", type=int, default=0,
                    help="seconds to wait for the port to appear")
    args = ap.parse_args()

    deadline = time.time() + args.wait
    while not os.path.exists(args.port):
        if time.time() > deadline:
            sys.stderr.write(
                f"error: {args.port} does not exist.\n\n"
                "Check the cable is a data cable (not charge-only), then list ports:\n"
                "  macOS : ls /dev/cu.*\n"
                "          system_profiler SPUSBDataType | grep -i -A6 303a\n"
                "  Linux : ls /dev/ttyACM* /dev/ttyUSB*\n"
                "          lsusb | grep -i 303a\n"
                "The port name changes with the machine and the USB port, so copy\n"
                "the one you actually see rather than reusing the example.\n"
            )
            return 1
        time.sleep(0.2)

    out_path = args.output or (
        "bootlog-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + ".log"
    )

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.1)
    except serial.SerialException as exc:
        sys.stderr.write(f"error: cannot open {args.port}: {exc}\n")
        if sys.platform == "darwin":
            sys.stderr.write(
                "On macOS this is usually another program holding the port\n"
                "(close `idf.py monitor`) or a driver with a stale claim on it.\n"
            )
        else:
            sys.stderr.write(
                "If this is a permission error, check you are in the 'dialout' group:\n"
                "  id -nG | grep dialout\n"
            )
        return 1

    if args.reset:
        # Standard ESP auto-reset circuit. Boards using native USB Serial/JTAG may
        # ignore this; the message says so rather than failing silently.
        ser.dtr = False
        ser.rts = True
        time.sleep(0.1)
        ser.rts = False
        print("# pulsed DTR/RTS reset (ignored on some native-USB boards)",
              file=sys.stderr)

    banner = (
        f"# CrossMetal boot log capture\n"
        f"# port   : {args.port}\n"
        f"# baud   : {args.baud}\n"
        f"# started: {datetime.datetime.now().isoformat(timespec='seconds')}\n"
        f"# ---\n"
    )
    sys.stdout.write(banner)
    sys.stdout.flush()

    end = time.time() + args.seconds
    written = 0

    try:
        with open(out_path, "w", encoding="utf-8", errors="replace") as fh:
            fh.write(banner)
            fh.flush()

            while time.time() < end:
                chunk = ser.read(4096)
                if not chunk:
                    continue
                text = chunk.decode("utf-8", errors="replace")
                sys.stdout.write(text)
                sys.stdout.flush()
                fh.write(text)
                fh.flush()
                written += len(chunk)
    except KeyboardInterrupt:
        print("\n# stopped by user", file=sys.stderr)
    finally:
        ser.close()

    footer = (
        f"# ---\n"
        f"# finished: {datetime.datetime.now().isoformat(timespec='seconds')}\n"
        f"# bytes captured: {written}\n"
    )
    sys.stdout.write(footer)
    with open(out_path, "a", encoding="utf-8") as fh:
        fh.write(footer)

    print(f"\n# saved to {out_path}", file=sys.stderr)

    if written == 0:
        sys.stderr.write(
            "\nNothing was received. Likely causes, in order:\n"
            "  1. The board is not resetting — press its reset button, or\n"
            "     unplug and replug USB while this script is running.\n"
            "  2. Wrong port: list them again (`ls /dev/cu.*` on macOS,\n"
            "     `ls /dev/ttyACM* /dev/ttyUSB*` on Linux) and try the other one.\n"
            "  3. Wrong baud: the firmware uses 115200.\n"
            "  4. The firmware crashed before initialising the console.\n"
        )
        return 2

    return 0


if __name__ == "__main__":
    sys.exit(main())