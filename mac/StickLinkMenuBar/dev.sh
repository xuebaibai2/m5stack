#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

APP_NAME="StickLinkMenuBar"
PID=""

cleanup() {
  if [[ -n "${PID}" ]] && kill -0 "${PID}" 2>/dev/null; then
    kill "${PID}" 2>/dev/null || true
    wait "${PID}" 2>/dev/null || true
  fi
}

restart_app() {
  cleanup
  echo "Starting ${APP_NAME}..."
  swift run "${APP_NAME}" &
  PID="$!"
}

snapshot() {
  find Package.swift Sources config -type f \
    \( -name '*.swift' -o -name '*.json' -o -name 'Package.swift' \) \
    -print0 |
    sort -z |
    xargs -0 stat -f '%N %m %z' 2>/dev/null || true
}

trap cleanup EXIT INT TERM

restart_app
LAST_SNAPSHOT="$(snapshot)"

echo "Watching for changes. Press Ctrl-C to stop."

while true; do
  sleep 1
  CURRENT_SNAPSHOT="$(snapshot)"
  if [[ "${CURRENT_SNAPSHOT}" != "${LAST_SNAPSHOT}" ]]; then
    LAST_SNAPSHOT="${CURRENT_SNAPSHOT}"
    echo "Change detected. Restarting ${APP_NAME}..."
    restart_app
  fi
done
