# Motion capture tooling (Phase 18)

Turning reference video into clips our rig can play. **Not a parallel animation system** — the
destination is Phase 17's `.mgeanim` through the same importer, the same rig-hash refusal and
the same budget (ADR 0018).

| File | What it is |
|---|---|
| `track_video.py` | Video → per-view landmark trajectories. Run it where the video is; the film is deliberately not in the repo. |
| `gait_dump.cpp` → `mge_gait_dump` | Runs the engine's **real** `LocomotionAnimator` and emits per-frame joint world positions, local rotations and a synthetic toe. |
| `gait_metrics.py` | The gait metrics, implemented once and applied to both sides of a comparison. |
| `gait_compare.py` | Task 18.1: measures the shipped procedural walk against the tracked human. |

## Why the engine side is a C++ tool

A Python model of `LocomotionAnimator` would be a second implementation that can drift from the
one the game plays, and the comparison would then be measuring the drift. `mge_gait_dump` links
`mge_core` and calls `buildSkeleton`, `update`, `samplePose` and `evaluatePose` exactly as the
runtime does.

## Task 18.1

```
./build/tools/mocap/mge_gait_dump --speed 1.355 --fps 60 --seconds 8 --out build/engine_walk.json
python3 tools/mocap/gait_compare.py \
    --reference assets/mocap/walk_reference_tracking.json.gz \
    --engine build/engine_walk.json \
    --figure docs/research/procedural-walk-vs-human.png
```

Findings and measurement limits: [`docs/research/procedural-walk-vs-human.md`](../../docs/research/procedural-walk-vs-human.md).

`--speed` must match the reference's measured walking speed or the comparison mixes a speed
difference into every row; the tool warns and prints the right value if it does not.

## Host analysis dependencies

`numpy` for the metrics, `matplotlib` only for `--figure`, `mediapipe`/`opencv` only for
`track_video.py`. **None of this is on any engine path** — these are host tools, and nothing here
is built into the runtime or run by `scripts/verify.sh`.
