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
measurement says it does not. Taking "planted" as both down and still:

| | planted | down but sliding | still, but airborne |
|---|---|---|---|
| human | **0–54%** of cycle | 55–58% (toe-off) | 94–99% |
| engine | **4–22%** | **23–52%** | 79–92% |

Not "our foot slides a bit more" — **it is planted at the wrong times.**

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

## 18.2 — partial, and the assumption underneath it did not survive

`docs/research/multiview-triangulation.md`. **Delivered:** a held-out validation protocol
(`tools/mocap/multiview.py`) and the measured noise floor — each view's own 3D explains its own
image to **3.1–5.1% of torso length**. **Not delivered: a 3D skeleton better than a single view.**

```
held out         view A   view B   A+B fused   verdict
threequarter       15.7      7.9         8.4   single wins
front               9.6     11.5        10.1   single wins
side                7.8     20.0        11.7   single wins
```

Three methods, none of which beat the best single view on any fold: visibility-weighted
averaging, anisotropic fusion that distrusts each view's depth axis, and direct least-squares
triangulation from the 2D landmarks with bundle-adjusted rotations. Fused skeletons land
*between* their inputs — the signature of averaging **biased** errors rather than independent
ones.

**Three of my own intermediate results were wrong, and I am recording them because each looked
convincing.** Bone-length constancy as a quality metric (it rewards the best-conditioned view
and never tests depth). A benchmark whose arms were calibrated differently — that one reported
triangulation as **2.5x and 4.5x better** than a single view, and the margin vanished entirely
once every arm got a freshly fitted camera. And "the estimator is bad" (26.8%/33.7% self-error),
which was my harness not fitting the rotation; fitted, it is 4.8%/5.1%.

**I have not built 18.3 on this.** Doing so is the expensive step, and building it on a skeleton
measurably worse than one view is the mistake worth an hour of measurement to avoid.

## Needs from the architect

### 1. An approach ruling on 18.2/18.3 — this is the one I am asking for first

```
SEAM: none yet — this is a method question about charted tasks 18.2 and 18.3,
      and changing the shape of a charted task is your call, not mine.
NEED: Permission to replace "triangulate to 3D points, then convert points to
      rotations" with "fit our 17-joint rig directly to all three views' 2D
      landmarks", collapsing 18.2 and 18.3 into one step.

      Measured reason: averaging point estimates cannot beat the best of them,
      and no weighting fixes a bias (0 of 3 folds, three methods). What is
      missing is a CONSTRAINT, and the obvious one is that these landmarks
      belong to a body whose bones do not change length. Our rig is exactly
      that constraint, and we already have it.

      It is also better than the sequence, not merely shorter. 18.3's stated
      risk is that "limb roll is underdetermined by positions alone" — in a
      direct rig fit there is no positions-to-rotations stage for roll to be
      underdetermined in; roll is a rig DOF constrained by three simultaneous
      views. Proportion mismatch likewise stops being a separate retargeting
      stage: fitting OUR rig to HIS images resolves it in the fit.

BREAKS: Nothing shipped. 18.2's deliverable changes from "a metric 3D skeleton"
      to "rig rotations per frame", which is what 18.5 needs anyway — `.mgeanim`
      stores rotations, so the point cloud was always an intermediate we would
      have thrown away. The held-out protocol already built scores a rig fit
      unchanged. Tasks 18.4, 18.5 and 18.6 are untouched.
EVIDENCE, because a proposal is worth less than a measurement. A bounded spike
      (`tools/mocap/rigfit_spike.py`, labelled a spike and not an implementation)
      fits the rig — 22 rotational DOF per frame, hinge knees and elbows, bone
      lengths fixed, seven proportion scales fitted once for the take — and is
      scored by the identical held-out protocol:

        held out       train fit   HELD-OUT   best single (point)   fused
        threequarter        3.1%       3.9%                  7.9%    8.4%
        front               3.8%      10.3%                  9.6%   10.1%
        side                5.5%       8.6%                  7.8%   11.7%
        mean                           7.6%                  8.4%   10.1%

      The number I care most about is the training fit: 3.1–5.5% against an
      estimator noise floor of 3.1–5.1%. **Our 17-joint rig, with hinge knees
      and elbows, is expressive enough to reproduce this walk to within the
      precision of the measurement.** That is a fact about the rig, independent
      of which method wins, and you may want it on file against ADR 0020.

WHAT I WILL NOT CLAIM: the mean gain over the best single view is small (7.6% vs
      8.4%) and rests on one strong fold. Fitted proportions are NOT stable
      across folds (shoulder 0.82/1.00/0.82, torso 1.13/0.89/1.09), so the
      variant estimate is absorbing pose error. The `front` fold overfits
      (3.8% train, 10.3% held out). A real implementation needs temporal
      continuity, joint limits and a foot-contact constraint — none of which the
      spike has, all of which should attack exactly those symptoms.

RISK I am not hiding: a nonlinear fit over 17 joints per frame can converge to a
      plausible-looking wrong pose. The held-out reprojection test is what
      catches that, and it exists and is trusted now — which is what the
      negative result above bought.
PROPOSAL: Re-scope 18.2 to "recover per-view cameras and fit the rig to all
      three views", 18.3 to "constrain and regularise that fit", and I proceed
      on the direct-fit path. If you would rather I keep pushing on point
      triangulation, say so and I will — but I would be doing it against a
      measurement that says averaging is the wrong tool.
```

### 2. The root-motion seam request from 18.1 — still open, still not blocking

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

**Holding at 18.2 for the approach ruling above.** While it is outstanding I am on **18.4**
(foot contact detection and locking), which is independent of how the 3D is recovered — its
detector is already written and validated against the engine's own animator phase in 18.1.

## Blocked on

Nothing.

## Environment note

This container had **no repository checked out and no push credential** at session start; I
cloned `ofekbit187/MobileGamesEngine` and attached it before pushing. Recorded in case other
sessions hit the same thing.

`scripts/verify.sh` — **all three tiers green** after provisioning this container
(`scripts/setup-android-sdk.sh`, plus `apt-get install qemu-user-static`; the apt index needed
an `update` first, which is worth knowing):

```
=== 1/3 host: build + tests + runner      steady-state heap allocations: 0  (target: 0)
=== 2/3 arm64 (NDK 27.0.12077973)         226 tests, 0 failed
                                          steady-state heap allocations: 0  (target: 0)
=== 3/3 android: assembleDebug            app-debug.apk (5,361,405 bytes)
=== verification complete
```

**No engine code has changed on this branch.** Everything landed so far is host analysis tooling
(`tools/mocap/`, one new `mge_core`-linked executable) and documentation.
