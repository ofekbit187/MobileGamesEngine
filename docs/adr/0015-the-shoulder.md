# ADR 0015 — The shoulder: fix the weights before spending a rig-version event

**Status:** accepted (architect ruling) · **Date:** 2026-08-21
**Depends on:** ADR 0007, ADR 0008, ADR 0012 · **Raised by:** the animation session, task 16.2

## Context

The animation session rebuilt `anim_preview` on the real skinned body (task 16.2) and, instead of
re-rendering, built a gate that **cannot pass by looking nice**: per-edge strain measured on the
CPU — how far each mesh edge's length moves from bind, which is what tearing and pinching
physically *are*.

Two results, both of which overturn something previously asserted.

**Layering is clean, and my predicted fix was wrong.** The number that matters is what layering
*adds* over the two poses it blends. Worst case **0.216**, against locomotion's own worst of
**0.311** — blending two poses distorts this skin *less than the walk cycle already does by
itself*. And the mask feathering I predicted would be needed is monotonically **the wrong
direction** across 180 measured pose pairs (0.198 at a hard cut, rising to 0.228 fully feathered),
because the body's spine and chest weights already blend. The session corrected its own comment,
which had asserted the feather prevented a tear on the strength of a box-rig render. Feathering
stays as what it actually is: a look control for how much torso joins the action.

**The shoulder is the real defect, and it is not the animation area's.** Measured on the shipped
body, naked, nothing layered, no archetype playing — one joint rotated and nothing else:

| shoulder pitch | worst edge strain | edges >50% | edges >100% |
|---|---|---|---|
| 15° | 0.352 | 0 | 0 |
| 30° | 0.690 | 5 | 0 |
| 45° | 1.003 | 11 | 2 |
| 90° | 2.413 | 47 | 21 |
| 140° | 3.513 | 71 | 32 |

The shipped walk and run both put **zero** edges over 50%, because locomotion stays inside roughly
35° of shoulder rotation. **Every use archetype needs 60–140°, and all nine tear.** Eight phases of
walking never exposed it.

Two structural facts, measured rather than guessed: **there is no clavicle** (`UpperArmR`'s parent
is `Chest`, so one joint carries the entire shoulder rotation — the worst case for linear-blend
skinning), and **the shoulder weights have no falloff** (53 of the 195 vertices influenced by
`UpperArmR` are bound at weight exactly **1.00**, adjacent to vertices that are 49% `Spine`).

## Decision

**Do (a) — reweight the shoulder — now. Do not add a clavicle, and do not change the skinning
definition, until the reweighted body has been re-measured.**

**Why the cheap option first is not timidity but the only sound order.** The table above is not a
measurement of "does this rig need a clavicle". It is a measurement of **a body whose shoulder
weights are broken** — adjacent vertices jumping from fully-arm to mostly-torso with nothing in
between is not a rig limitation, it is a bad weight transfer, and the surface between those
vertices is exactly what tears. Ruling for a clavicle on this evidence would be **adding a joint
to compensate for bad data, permanently**, and we would never learn which of the two was
responsible.

The asymmetry in cost decides the rest. Option (a) is data: no `Joint` enum edit, no shader
change, no new joint. It is a contract-version event — the body hash moves, so garments re-bake —
and this project has done four of those this week with the machinery refusing loudly when anyone
forgets. Option (b) is **17 → 18 joints**, which breaks every garment binding *and* the skinning
shader's palette size, and is irreversible in the WoW sense: cheap now, impossible once a
catalogue exists. Spending that on contaminated evidence would be the exact mistake ADR 0008 is
built to prevent.

**On (c), dual-quaternion skinning: rejected for this defect, on grounds beyond cost.** DQS fixes
*volume collapse under twist*. It does not fix a hard weight boundary — a 1.00-to-0.49
discontinuity tears under DQS too, because the discontinuity is in the weights, not in the
interpolation. It would also change `skinMesh()`, which is the definition GPU skinning must match,
making it a renderer-plus-body change landing together. Wrong tool, and an expensive one.

**Acceptance criterion, so "fixed" is not a matter of opinion:** at 140° shoulder pitch, **zero
edges over 100% strain**, with the >50% count reported alongside. Locomotion's zero-over-50% is
the standard to aim at, not a threshold to squeak under. Re-measure with the animation session's
own gate — it is committed and it is the same instrument, which is the point.

**Then re-open (b).** If a properly weighted shoulder still tears at 140°, that is a *clean*
measurement that a single joint cannot carry the rotation, and the clavicle case gets decided on
evidence instead of inference. It is not rejected here; it is **not yet earned**.

**Owner: the character asset pipeline** — skin weights and the import path are its files, and this
preempts 13.9. Every upper-body action in the engine looks torn until it lands.

## Also ruled, briefly

**`skinMesh()` drops the UV — approved, fix it.** It writes position and normal but leaves
`Vertex::uv` at `{0,0}`, so a CPU-skinned body samples one texel for its whole surface. One line,
nothing downstream, and it **blocks any textured character** — which means it blocks the first
face texture, which is the retirement condition on ADR 0012's provisional Face waiver. The
textures session is working around it locally; that workaround must not outlive this. Same owner.

## A finding that is not a request, and is worse than it sounds

**No character in the shipped engine has ever held anything.** `buildPosedCharacter` skips held
items (`if (garment.vertices.empty()) continue;`) and `device_game.cpp` does the identical thing.
The sword visible in earlier captures existed only because the dead box rig generated it as a
rigid part — **it was an artifact of the obsolete path, not a feature that regressed.**

This matters beyond a missing prop: Phase 14 exists to make weapons animate, and its proof
catalogue is currently bare-handed for this reason rather than by preference. It belongs to
wearables (`AGENTS.md` §6.3 — held items and grips) and is queued as task 14.7.

## Consequences

- 14.1 is **verified on the real body** and needs no rework; the layering design was right.
- Phase 14 is not blocked from *building*, but is blocked from *looking right*, and no amount of
  animation work reaches it. That is now a pipeline task with a number and a threshold.
- ADR 0012's Face waiver retirement moves a step closer: the UV fix unblocks the first texture.
- My own prediction that feathering was the fix is recorded as wrong, along with why it was a
  reasonable guess and why the measurement beat it.
