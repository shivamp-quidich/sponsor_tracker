#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STIQY_ROOT="${ROOT}/../StiQy_2.0"
cd "${ROOT}"

export STIQY_SPONSOR_REINIT_REF_DIR="${STIQY_SPONSOR_REINIT_REF_DIR:-assets/sponsor_refs}"
export STIQY_SPONSOR_REINIT_MANIFEST="${STIQY_SPONSOR_REINIT_MANIFEST:-assets/sponsor_refs/bank_manifest.json}"
export STIQY_REINIT_SIDECAR_WORKER="${STIQY_REINIT_SIDECAR_WORKER:-${STIQY_ROOT}/tools/reinit_sidecar_poc/python_worker/matcher_worker_cuda_ipc.py}"

SIDECAR_PID=""
cleanup() {
  if [[ -n "${SIDECAR_PID}" ]] && kill -0 "${SIDECAR_PID}" 2>/dev/null; then
    kill "${SIDECAR_PID}" 2>/dev/null || true
    wait "${SIDECAR_PID}" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

mkdir -p assets/sponsor_refs logs output

# SponsorGridTracker embeds Python and imports loftr_reinit from a path relative to
# StiQy (modules/PerspectiveTracker/src/python). Mirror that layout here so the
# standalone app can find the module when cwd is sponsor/.
LOFTR_PY_DIR="${ROOT}/modules/PerspectiveTracker/src/python"
LOFTR_PY_SRC="${STIQY_ROOT}/modules/PerspectiveTracker/src/python/loftr_reinit.py"
if [[ -f "${LOFTR_PY_SRC}" ]]; then
  mkdir -p "${LOFTR_PY_DIR}"
  if [[ ! -e "${LOFTR_PY_DIR}/loftr_reinit.py" ]]; then
    ln -sf "${LOFTR_PY_SRC}" "${LOFTR_PY_DIR}/loftr_reinit.py"
  fi
fi

# pybind11 embed also honors PYTHONPATH; include StiQy python helpers and LightGlue.
STIQY_PYTHONPATH="${STIQY_ROOT}/modules/PerspectiveTracker/src/python"
STIQY_PYTHONPATH="${STIQY_PYTHONPATH}:${STIQY_ROOT}/modules/sponsor_grid_tracker/src/python"
if [[ -d "${STIQY_ROOT}/LightGlue" ]]; then
  STIQY_PYTHONPATH="${STIQY_PYTHONPATH}:${STIQY_ROOT}/LightGlue"
fi
if [[ -d "${STIQY_ROOT}/tools/reinit_sidecar_poc/python_worker/cuda_ipc_ext" ]]; then
  STIQY_PYTHONPATH="${STIQY_PYTHONPATH}:${STIQY_ROOT}/tools/reinit_sidecar_poc/python_worker/cuda_ipc_ext"
fi
export PYTHONPATH="${STIQY_PYTHONPATH}${PYTHONPATH:+:${PYTHONPATH}}"

# Reset runtime manifest (session-scoped refs)
python3 - <<'PY' 2>/dev/null || true
import json, os, datetime
manifest = os.environ.get("STIQY_SPONSOR_REINIT_MANIFEST", "assets/sponsor_refs/bank_manifest.json")
os.makedirs(os.path.dirname(manifest), exist_ok=True)
session = datetime.datetime.now().strftime("%Y%m%d_%H%M%S_%f")[:18]
with open(manifest, "w") as f:
    json.dump({"refs": [], "session_id": session}, f, indent=2)
PY

sidecar_python_ready() {
  local py="$1"
  "$py" -c "import cv2, torch; from lightglue import ALIKED, LightGlue" 2>/dev/null
}

if [[ "${STIQY_SKIP_SIDECAR:-0}" == "1" ]]; then
  echo "Sidecar skipped (STIQY_SKIP_SIDECAR=1). Tracking works; reinit disabled."
elif [[ -f "${STIQY_REINIT_SIDECAR_WORKER}" ]]; then
  PYTHON_BIN=""
  SIDECAR_CANDIDATES=()
  [[ -n "${STIQY_SIDECAR_VENV:-}" ]] && SIDECAR_CANDIDATES+=("${STIQY_SIDECAR_VENV}")
  [[ -n "${STIQY_SIDECAR_VENV_PATH:-}" ]] && SIDECAR_CANDIDATES+=("${STIQY_SIDECAR_VENV_PATH}")
  SIDECAR_CANDIDATES+=("/opt/stiqy_sidecar_venv")

  for venv in "${SIDECAR_CANDIDATES[@]}"; do
    candidate="${venv}/bin/python3"
    if [[ -x "${candidate}" ]] && sidecar_python_ready "${candidate}"; then
      PYTHON_BIN="${candidate}"
      break
    fi
  done

  if [[ -z "${PYTHON_BIN}" ]]; then
    echo "Warning: no sidecar Python with lightglue found."
    echo "  Set STIQY_SIDECAR_VENV_PATH=/path/to/venv (needs: torch, cv2, lightglue)"
    echo "  Or set enable_reinit_sidecar: false in config/config.yaml"
    echo "  Or STIQY_SKIP_SIDECAR=1 to silence this and run without sidecar reinit."
    echo "  (Built-in feature-match reinit still works when sidecar/LoFTR are unavailable.)"
  else
    echo "Starting reinit sidecar on :5557 (${PYTHON_BIN})"
    PYTHONPATH="${PYTHONPATH}" "${PYTHON_BIN}" "${STIQY_REINIT_SIDECAR_WORKER}" \
      --host 127.0.0.1 \
      --port 5557 \
      --ref-dir "${STIQY_SPONSOR_REINIT_REF_DIR}" \
      --manifest "${STIQY_SPONSOR_REINIT_MANIFEST}" &
    SIDECAR_PID=$!
    sleep 2
  fi
else
  echo "Warning: sidecar worker not found at ${STIQY_REINIT_SIDECAR_WORKER}"
fi

BIN="${ROOT}/bin/sponsor_tracker"
if [[ ! -x "${BIN}" ]]; then
  echo "Binary not found. Run scripts/build.sh first."
  exit 1
fi

exec "${BIN}" "$@"
