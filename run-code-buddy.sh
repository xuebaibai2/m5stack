#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CODE_BUDDY_DIR="$SCRIPT_DIR/external/CodeBuddy"
VENV_DIR="$CODE_BUDDY_DIR/.venv"
PYTHON="$VENV_DIR/bin/python"
CODE_BUDDY="$VENV_DIR/bin/code-buddy"
PACKAGE_SRC="$CODE_BUDDY_DIR/src/codex_buddy"
COMPAT_RUNNER="$SCRIPT_DIR/scripts/run_code_buddy_compat.py"

if [[ ! -d "$CODE_BUDDY_DIR" ]]; then
  echo "CodeBuddy source not found: $CODE_BUDDY_DIR" >&2
  exit 1
fi

if [[ ! -x "$PYTHON" ]]; then
  echo "CodeBuddy venv not found: $VENV_DIR" >&2
  echo "Create it with:" >&2
  echo "  cd \"$CODE_BUDDY_DIR\"" >&2
  echo "  python3 -m venv .venv" >&2
  echo "  .venv/bin/pip install -e '.[dev]'" >&2
  exit 1
fi

if [[ ! -x "$CODE_BUDDY" ]]; then
  echo "CodeBuddy CLI not found: $CODE_BUDDY" >&2
  echo "Install it with:" >&2
  echo "  cd \"$CODE_BUDDY_DIR\"" >&2
  echo "  .venv/bin/pip install -e '.[dev]'" >&2
  exit 1
fi

SITE_PACKAGES="$("$PYTHON" - <<'PY'
import site
print(site.getsitepackages()[0])
PY
)"

PACKAGE_LINK="$SITE_PACKAGES/codex_buddy"
if ! "$PYTHON" - <<'PY' >/dev/null 2>&1
import codex_buddy
PY
then
  if [[ ! -d "$PACKAGE_SRC" ]]; then
    echo "CodeBuddy package source not found: $PACKAGE_SRC" >&2
    exit 1
  fi
  rm -rf "$PACKAGE_LINK"
  ln -s "$PACKAGE_SRC" "$PACKAGE_LINK"
fi

cd "$CODE_BUDDY_DIR"
exec "$PYTHON" "$COMPAT_RUNNER" "$@"
