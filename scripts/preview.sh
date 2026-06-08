#!/usr/bin/env bash
# Run sponsor_tracker with a live OpenCV preview window.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

ARGS=()
for arg in "$@"; do
  if [[ "$arg" == "--headless" ]]; then
    echo "preview.sh: ignoring --headless (preview is always on). Use ./scripts/run.sh --headless for export-only." >&2
  else
    ARGS+=("$arg")
  fi
done

exec "${ROOT}/scripts/run.sh" --preview "${ARGS[@]}"
