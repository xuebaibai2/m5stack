#!/usr/bin/env bash
set -euo pipefail

BAUD="${BAUD:-115200}"
PORT="${1:-${PORT:-}}"

if [[ -z "$PORT" ]]; then
  for candidate in /dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.SLAB_USBtoUART*; do
    if [[ -e "$candidate" ]]; then
      PORT="$candidate"
      break
    fi
  done
fi

if [[ -z "$PORT" ]]; then
  echo "No serial port found." >&2
  echo "Available ports:" >&2
  ls /dev/cu.* >&2
  echo >&2
  echo "Usage: ./monitor.sh /dev/cu.usbmodem101" >&2
  echo "Or:    PORT=/dev/cu.usbmodem101 ./monitor.sh" >&2
  exit 1
fi

echo "Opening serial monitor on $PORT at $BAUD baud"
echo "Press Ctrl+] to exit."
exec pio device monitor -p "$PORT" -b "$BAUD"
