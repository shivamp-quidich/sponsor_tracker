# Sponsor Tracker (Standalone)

Minimal standalone application extracted from StiQy 2.0's `SponsorGridTracker` module. It reads a video file, runs sponsor grid tracking, and exports an overlay video plus per-frame quad CSV.

## Prerequisites

- CUDA 13 + OpenCV 4.14 (with CUDA modules) — must match the StiQy build environment
- GCC 12 (`g++-12`) for compatibility with nvcc
- OpenGL, GLFW, GLEW, spdlog, yaml-cpp
- Python 3.12 dev headers (matches StiQy; used by optional LoFTR fallback)
- StiQy 2.0 source at `../StiQy_2.0` plus a **Release build** at `../StiQy_2.0/build-release`
- Python sidecar venv at `/opt/stiqy_sidecar_venv` for ALIKED+LightGlue reinit (optional)

Build StiQy once if `build-release/` is missing:

```bash
cd ../StiQy_2.0 && mkdir -p build-release && cd build-release
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/g++-12
cmake --build . -j
```

## Build

```bash
./scripts/build.sh
```

Binary output: `bin/sponsor_tracker`

The standalone app **recompiles** `SponsorGridTracker.cpp` against your local OpenCV and links prebuilt StiQy common libs (`common_image`, `opengl_context`, `reinit_sidecar`, `logger`).

## Run

```bash
./scripts/run.sh --video /path/to/match.mp4 \
  --boundary assets/sponsor_refs/cam_1/reference_boundary.txt \
  --auto-align --auto-place
```

Put sponsor PNG/JPG files in `data/` and select one from the preview side panel.

### CLI options

| Flag | Description |
|------|-------------|
| `--video` | Input video (required) |
| `--config` | YAML config (default: `config/config.yaml`) |
| `--boundary` | 4-point boundary file (TL TR BR BL) |
| `--camera-id` | Camera id for `assets/sponsor_refs/cam_<id>/` |
| `--output` | Export directory (default: `output/`) |
| `--headless` | No preview window |
| `--auto-align` | Align grid from `--boundary` on first frame |
| `--auto-place` | Place graphic at boundary center |
| `--max-frames` | Limit processed frames |

### Interactive keys (preview mode)

- `a` — start alignment
- `Enter` — apply alignment (start tracking)
- `p` — arm graphic placement (then right-click on video)
- `r` — request manual reinit
- `q` / `Esc` — quit

## Outputs

- `output/sponsor_export_<timestamp>.mp4` — video with grid/quad overlay
- `output/sponsor_export_<timestamp>.csv` — per-frame quad corners (TL, TR, BR, BL)

Visualize offline with StiQy's `tools/visualize_sponsor_roi.py`.

## Configuration

`config/config.yaml` mirrors StiQy's `sponsor_grid_tracker` section. The reinit sidecar listens on port **5557** by default.

## Project layout

```
sponsor/
├── app/                  # Standalone main + helpers
├── cmake/                # StiQy module build wiring
├── config/
├── assets/sponsor_refs/  # Reinit reference bank
├── scripts/build.sh
└── scripts/run.sh
```

`SponsorGridTracker.cpp` is compiled from `../StiQy_2.0` at build time; CUDA image kernels and other common libs are reused from `build-release/`.

## Headless / CI

`--headless` still needs an OpenGL context. On a machine with a display server, a hidden GLFW window is used. Without `DISPLAY`, use `xvfb-run -a ./scripts/run.sh ...`.
