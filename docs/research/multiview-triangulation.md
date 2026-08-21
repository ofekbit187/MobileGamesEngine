# Task 18.2 — combining the three views, and the assumption that did not survive

**Session:** motion capture · **Date:** 2026-08-21 · **Branch:** `claude/mocap-from-video`
**Status: 18.2 is PARTIAL.** The validation harness is built and trustworthy. The thing it
validates is not yet good enough to hand to 18.3, and this records why, so the next attempt
starts from the measurement rather than from the assumption.

**Reproduce:** `python3 tools/mocap/multiview.py --reference assets/mocap/walk_reference_tracking.json.gz`

---

## 1. The assumption

18.2 reads: *"Triangulate the three views into one metric 3D skeleton per frame … what remains
is relative camera pose, solvable from the subject itself across views."* Underneath it sits a
premise: **that a skeleton built from three views is better than one built from one view.**

That premise is worth an hour of measurement, because 18.3 is the expensive step — *"this is
where quality lives or dies"* — and it would be built on top of whatever 18.2 produces. A 18.3
built on a skeleton worse than a single view is a day spent making a worse thing carefully.

**It did not survive. Fusing two views beat the better single view on none of three folds.**

## 2. How the question was made answerable

Hold out one view. Build a 3D skeleton from the other two. Fit a camera for the held-out view
and measure how well the skeleton reprojects into an image it never saw. Genuine 3D structure
predicts the third view; two estimates smoothed together do not.

Errors are a percentage of the subject's torso length in that view's image, so the three panels
(1045x620, 330x620, 1405x385) are comparable.

**The noise floor, measured rather than assumed** — how well each view's own 3D explains the
image it came from, with a fully fitted camera:

| view | residual |
|---|---|
| three-quarter | 4.8% of torso (4.9 px) |
| front | 3.1% of torso (3.9 px) |
| side | 5.1% of torso (4.9 px) |

MediaPipe's world landmarks and its image landmarks agree to about 4–5 px. That is the number
everything below is measured against, and it is good.

## 3. The result

```
held out         view A   view B   A+B fused   verdict
threequarter       15.7      7.9         8.4   single wins   (A=front, B=side)
front               9.6     11.5        10.1   single wins   (A=threequarter, B=side)
side                7.8     20.0        11.7   single wins   (A=threequarter, B=front)
```

In every fold the fused skeleton lands **between** the two single views, near their
visibility-weighted average. That is exactly what averaging does when per-view errors are
**biased** rather than independent and zero-mean — and single-view depth error is biased, since
each view's estimator makes the same kind of mistake on the same axis every frame. Averaging
propagates a bias; it does not cancel it.

Three fusion variants were tried and all behave the same way: plain visibility-weighted
averaging; anisotropic weighting that trusts each view's image plane and distrusts its depth
axis; and direct least-squares triangulation from the 2D landmarks with scaled-orthographic
cameras and bundle-adjusted rotations.

**Note also that no single view is best.** `side` predicts the three-quarter view best,
`threequarter` predicts the other two best. There is real information in each view. Averaging
is simply not how to get at it.

## 4. Three wrong answers I produced first, recorded because they were convincing

Every one of these looked like a finding before it was checked.

**Bone-length constancy is not a validation metric.** The first evaluation scored fusion by how
constant limb lengths stayed over time, and the front view alone won (1.87% coefficient of
variation against 2.28–2.75% for every fusion). It is a confounded metric: it rewards whichever
view is best-conditioned and it never tests depth at all — the one thing fusion is supposed to
fix. A method that output a rigid but completely wrong skeleton would score perfectly.

**A benchmark whose arms are calibrated differently measures the calibration.** The second
evaluation reused one global alignment fitted with `front` as its reference. That handed the
`front` fold a rotation chosen to explain exactly the mapping being scored, while the other
folds got rotations fitted to explain something else. It reported triangulation as **2.5x and
4.5x better** than a single view. Refitting the camera per arm erased the margin completely and
reversed the conclusion.

**"The estimator is bad" was my harness being bad.** Scoring each view's own 3D against its own
image *without fitting the rotation* gave 26.8% and 33.7% for the two oblique views, which reads
as a damning noise floor and would have justified abandoning the world landmarks. Fitting the
rotation gives 4.8% and 5.1%. Nothing was wrong with the data.

The harness in `tools/mocap/multiview.py` now fits every camera fresh, fits rotation rather than
assuming it, and uses eight starts per fit because scaled-orthographic projection has a depth
reflection ambiguity — a point set and its depth mirror project identically, so a single start
lands in whichever minimum it began nearest.

## 5. What I think the answer is — and it is not "try harder at triangulation"

The failure has a shape. Averaging point estimates cannot beat the best of them, and no
weighting scheme fixes a bias. What is missing is a **constraint**, and the obvious one is that
these landmarks belong to a body whose bones do not change length.

**Proposal: fit our own 17-joint rig directly to all three views' 2D landmarks, and skip the
intermediate 3D point cloud entirely.**

The unknowns are the rig's joint rotations (what we actually want) plus a per-view camera; the
observations are the 2D landmarks from all three views; the bone lengths are fixed by the rig
itself. This collapses 18.2 and 18.3 into one step, and it is better than the sequence, not
merely shorter:

- **It never has a positions-to-rotations step**, so the problem 18.3 names as where quality
  lives or dies — *"limb roll is underdetermined by positions alone"* — does not arise in that
  form. Roll is a rig degree of freedom constrained by three simultaneous views, not something
  recovered from point positions after the fact.
- **Proportion mismatch stops being a separate retargeting stage.** The filmed man's limbs are
  not ours, and fitting *our* rig to *his* images resolves that in the fit rather than after it.
- **It is validated by the same held-out protocol already built and trusted here** — the rig
  reprojects into the held-out view or it does not.

The risk is honest: it is a nonlinear fit over ~17 joints x 3 rotational DOF per frame, and it
can converge to a plausible-looking wrong pose. The held-out reprojection test is what would
catch that, which is why building it first was worth doing even though it produced a negative
result.

### 5.1 The proposal, measured rather than argued

A bounded spike (`tools/mocap/rigfit_spike.py`) fits the rig — 22 rotational DOF per frame across
10 joints, with knees and elbows as 1-DOF hinges, bone lengths fixed, and seven proportion scales
fitted once for the whole take — and is scored by the identical held-out protocol.

```
held out       train fit   HELD-OUT     best single (point)   fused (point)
threequarter        3.1%       3.9%                    7.9%            8.4%
front               3.8%      10.3%                    9.6%           10.1%
side                5.5%       8.6%                    7.8%           11.7%
                              -----                    ----            ----
mean                           7.6%                    8.4%           10.1%
```

Two things in this are worth more than the mean.

**The training fit reaches the estimator's own noise floor.** 3.1–5.5% against a floor of
3.1–5.1%. **Our 17-joint rig, with anatomically-correct hinge knees and elbows, is expressive
enough to reproduce this walk to within the precision of the measurement.** That is a
feasibility result about the rig itself, it is independent of which fusion method wins, and it
is worth knowing before ADR 0020's joint count is ever reopened.

**On the fold where it wins, it wins by 2x** (3.9% against 7.9%) and lands essentially at the
noise floor. On the other two it is within about a point of the best single view.

**What I will not claim.** The mean improvement over the best single view is small (7.6% against
8.4%) and rests on one strong fold. The fitted proportions are **not stable across folds** —
shoulder width comes out 0.82, 1.00, 0.82 and torso 1.13, 0.89, 1.09 — so the variant estimate
is absorbing pose error and is not yet trustworthy. And the `front` fold shows a large
train-to-held-out gap (3.8% to 10.3%) that says it is overfitting there. A real implementation
needs temporal continuity, joint limits and a foot-contact constraint, none of which the spike
has, and all of which should reduce exactly those symptoms.

The honest summary: **the rig fit is the best of the three approaches measured, it is the only
one that outputs what we actually need (rotations, not points), and it is not yet good enough to
call finished.**

**This is a change of approach to a charted task, so it is a question for the architect, not a
decision for me.** It is raised in `docs/status/mocap.md`.

## 6. What is actually delivered

- `tools/mocap/multiview.py` — camera fitting, alignment, fusion, and the held-out validation
  protocol. The protocol is the durable part: whatever 18.2 eventually produces gets scored by
  it, including a rig fit.
- `tools/mocap/rigfit_spike.py` — the feasibility spike above. **Labelled a spike on purpose**:
  it is evidence for a proposal, not an implementation of it, and it should be rewritten rather
  than extended once there is a ruling.
- The measured noise floor (3.1–5.1% of torso) that any future method must be judged against.
- The negative result above, with the three ways of getting it wrong recorded.

**Not delivered: a metric 3D skeleton better than a single view.** 18.2 stays `[~]`.
