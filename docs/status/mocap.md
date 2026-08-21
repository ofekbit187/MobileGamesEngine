# Motion capture

**Session:** session_01XF3PqLgmSojSaKWKFtUmyF
**Branch:** `claude/mocap-from-video`
**State:** working
**Updated:** 2026-08-21 by the mocap session

## Last landed

**18.1 done** — `1531704` "Mocap 18.1: measure the shipped procedural walk against the tracked
human". Report: `docs/research/procedural-walk-vs-human.md` (● real output, with the figure).
Tools: `tools/mocap/gait_dump.cpp` → `mge_gait_dump`, `gait_metrics.py`, `gait_compare.py`.
**No engine code changed.**

### The pipeline was validated before anything was concluded from it

This was the point of doing 18.1 first, so it goes first here:

- the measurement recovers `LocomotionAnimator`'s own stride constant — **1.0378 m against the
  1.0379 m its source formula predicts** — from detected foot plants, knowing nothing of the
  formula;
- the contact detector's cycle starts land at animator phase **0.531–0.534** across nine cycles;
- on the human side two independent detectors (foot speed, foot height) agree to a few frames;
- the reference's own numbers land where the gait literature puts a casual adult walk (duty
  factor 0.60, step width 95 mm, bob 46 mm, cadence 119 steps/min at 1.355 m/s).

### What the walk measures like

Both sides driven at the measured human speed, 1.355 m/s. Worst first:

| | human | engine | |
|---|---|---|---|
| slide through mid-stance | 42 mm | **236 mm** | feet do not stay put |
| knee flexion peak | 52° at 71% of cycle | **23° at 37%** | a third as far, a third of a cycle early |
| vertical bob | 46 mm p2p | **0.000 m** | zero *by construction* |
| stride / cadence | 1.44 m / 119 spm | 1.04 m / 157 spm | −28% / +32% |
| stance width / lateral swing | 95 mm / 66 mm | 354 mm / **0 mm** | feet never move sideways |
| pelvis yaw | counter-rotates | **0.0°** | `Hips` is never rotated |

**On the feet sliding — the comment in `update()` is not wrong, it is narrower than it reads.**
"Phase is distance-driven … so feet never slide" guarantees the cycle freezes when the character
stands still, and it does. It does not imply a foot stays planted *within* a cycle, and the
measurement says it does not: the engine's foot is genuinely still for about the first 20% of the
cycle, slides through the remaining ~30% of its ground contact, and is then stationary again
while airborne.

**These are the character asset pipeline's to fix, not mine.** `LocomotionAnimator` lives in
`engine/src/character/humanoid.cpp`. I have not touched it. Everything needed to re-measure any
fix is in the two commands at the top of the report.

### What 18.1 does NOT settle — ADR 0020's revisit trigger

ADR 0020 named the condition for reopening the clavicle: *when the mocap front-end exists and we
can measure how much girdle motion real captures carry.* **I am not claiming that condition is
met.** The same girdle-yaw quantity measures 7.8°, 21.0° and 30.4° across the three views for the
pelvis and 5.7°, 33.4° and 35.5° for the shoulders — a ~4x spread, larger than the difference the
decision would turn on, because girdle yaw rides the depth axis and depth is the weakest axis of
a single-view estimator. That is exactly what 18.2 fixes.

What *is* solid: the human counter-rotates on all three views (girdle correlation negative on
each), and the engine's pelvis does not rotate on any. **Nothing measured here threatens the rig
freeze**, and I will bring the amplitude back after 18.2/18.3 rather than before.

## Needs from the architect

```
SEAM: Rig — `Pose`, and the clip format that feeds it (ADR 0018 Ruling 1)
NEED: A ruling on whether "clips are in-place" means ALL THREE AXES or only the
      horizontal plane — and, if vertical is to be kept, somewhere to put it.

      Measured: a real walk's pelvis rises and falls 46 mm peak-to-peak, twice
      per stride. Our walk's is exactly 0.000 m, and it is zero BY CONSTRUCTION
      rather than by tuning: `Pose` carries 17 rotations and nothing else, the
      `Hips` bind offset is constant, so `evaluatePose` puts the root at the
      same height on every frame. `AnimationClip` matches it — `rotations` only,
      with `rootTravelRemoved` a single total, not a per-frame channel.

      So there is nowhere for a captured clip's vertical motion to go, and 18.5
      would bake a walk whose body does not rise. 46 mm is small in metres and
      is the difference between a walk that reads alive and one that reads dead.

      I think ADR 0018 Ruling 1 was aimed at horizontal travel and simply did
      not distinguish the axes — the reason it gives is that root translation
      fights distance-driven locomotion, which is true of forward travel and not
      true of vertical bob or lateral sway. But that is a reading of your
      ruling, not a licence to act on it, so I am asking rather than assuming.

BREAKS: If the answer is "keep vertical and lateral":
      - `Pose` gains a root translation (Vec3): 272 -> 284 bytes. Character
        asset pipeline owns it; renderer and wearables are untouched because
        both consume the joint matrices `evaluatePose` already produces.
      - `evaluatePose` adds the offset to the root's local translation.
      - `LayeredPose`/`blendPose` blend one more channel — animation session.
      - `AnimationClip` gains a per-frame root channel. Cost measured against a
        1 s 30 Hz clip: +360 bytes raw against 2040 bytes of rotations (+18%),
        or +8.8% quantized to 3x16-bit. P1 impact is a clip-budget question,
        not a per-frame one — no extra allocation, no extra per-frame work.
      - `.mgeanim` version bump and the same rig-hash refusal as today.
      If the answer is "no, in-place means all three axes", nothing breaks and I
      will bake captured walks with the bob removed, note it in 18.5's output,
      and stop raising it.

PROPOSAL: In-place means HORIZONTAL only. Forward and lateral *travel* is
      measured and reported as ADR 0018 already rules; vertical and lateral
      *oscillation about the path* is part of the motion and is kept. Smallest
      version that satisfies both sides, and it leaves distance-driven
      locomotion exactly as it is.
```

**Not blocking.** 18.2, 18.3 and 18.4 do not depend on this ruling; only 18.5's bake does, and I
will have reached it with the question already asked. If no ruling has arrived by then I will
implement the in-place-all-axes reading, report the removed bob in the output, and flag it —
rather than pick the more expensive option unilaterally.

## Now

**18.2** — triangulating the three views into one metric 3D skeleton per frame.

## Blocked on

Nothing.

## Environment note

This container had **no repository checked out and no push credential** at session start; I
cloned `ofekbit187/MobileGamesEngine` and attached it before pushing. Recorded in case other
sessions hit the same thing.

`scripts/verify.sh` currently reports **tier 1 green (`steady-state heap allocations: 0`), tiers
2 and 3 SKIPPED** — no Android SDK/NDK and no `qemu-user` in this container. I am provisioning
via `scripts/setup-android-sdk.sh` and will report which tiers actually ran rather than claiming
three. 18.1 changed no engine code — the only compiled addition is a host analysis tool.
