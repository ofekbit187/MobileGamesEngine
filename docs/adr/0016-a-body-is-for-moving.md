# ADR 0016 — A body is for moving, and we only ever validated one standing still

**Status:** accepted (architect ruling) · **Date:** 2026-08-21
**Depends on:** ADR 0007, ADR 0008, ADR 0015 · **Owner priority:** first, above everything

## The owner's call

> *"the most problematic thing im worrying about its the shoulder tears, that means a lousy job
> was done. our first priority is to fix it"*

**Agreed on the priority.** On "a lousy job", the diagnosis is more specific than that, and the
specific version is the one that prevents a recurrence.

## Root cause — three facts, in order of how much they matter

**1. Nobody authored those weights. Blender did.**
`tools/model/humanoid_template.py` binds with `bpy.ops.object.parent_set(type='ARMATURE_AUTO')` —
**bone-heat automatic weighting**. That is a defensible *starting* point and a normal thing to do;
it is not a finished shoulder. Bone heat is specifically known to produce hard boundaries where
its heat diffusion meets geometry it cannot resolve, and the deltoid/armpit is the textbook case.
So the honest statement is not that someone weighted this badly — it is that **nobody weighted it
at all**, and an automatic result was shipped as though it were an authored one.

**2. The contract already forbade what we shipped.**
`B-15 (MUST)` requires "three loops across every bending joint **with the joint loop weighted
50/50**". The measured shoulder is 53 of 195 vertices at weight **exactly 1.00**, adjacent to
vertices at 49 % `Spine`. That is not near 50/50; it is the precise opposite. `B-16 (MUST)` adds
joint-extreme integrity at limit poses. **The shipped body violates two MUST clauses**, and has
since it was imported.

**3. No gate ever checked either of them, and that one is mine.**
The seven §9 acceptance gates check masking, pit clearance, hem loops, the scalp fallback, the
content hash, region groups, and "existing gates" — closed/wound, proportions, budgets, LOD
silhouettes. **Every one of them validates a body standing still.** Not one poses it.

I ratified that gate set. I specified, in detail, everything the *wearables* work needed to
verify, because wearables had a session pushing for it — and nothing the *animation* work would
need, because animation had no session and therefore no advocate at the time. A contract written
under those conditions checks what the loudest area asks for. **A body is for moving, and we
built an acceptance process for one standing in an A-pose.**

That is why eight phases of walking, twelve variant renders, six garments, four contract-version
events and three sets of acceptance gates all passed over this without anyone seeing it. It was
never hidden. It was simply never asked.

## Decision

**1. Fix the shoulder first, above everything.** Ruled in ADR 0015 (reweight before spending a
rig-version event) and it stands. It preempts 13.9 and every other pipeline task.

**2. The scope is every bending joint, not the shoulder.** The shoulder is where the archetypes
happened to look. Elbows, knees, hips, neck and wrists came out of the same automatic bind and
have never been posed either. **Measure them all; fix what fails.** Fixing only what the owner
saw would be treating the symptom that got reported.

**3. New contract clause — `B-31 (MUST)`: posed integrity.** Under the rotation range the engine
actually asks of it, no bending joint may tear. Concretely, at each joint's working range:
**zero edges over 100 % strain**, with the count over 50 % reported. Locomotion's measured
zero-over-50 % is the target, not a bar to squeak under.

**4. New acceptance gate — §9.8, and it is not optional.** The animation session's per-edge strain
instrument is promoted from that area's test into the body contract's gate set. **A body that has
not been posed has not been accepted.** This is the clause whose absence let all of this through,
and it is the whole point of this ADR: the shoulder is a bug, and *no gate for posed behaviour* is
the defect.

**5. Automatic weighting is a draft, and must be labelled as one.** Where the pipeline uses
`ARMATURE_AUTO` it must say so at the call site and the output must pass `B-31` before it is
shipped. **An automatic result presented as a finished one is the failure mode here**, and it is
the same shape as the retired modeler's — output that looks plausible, was never measured, and
nobody claimed responsibility for.

## Consequences

- The pipeline session works on nothing but this until it passes.
- The next imported body cannot repeat it: `B-31` and gate §9.8 fail closed.
- ADR 0015's clavicle question gets its clean measurement as a by-product — after a real reweight,
  a shoulder that still tears is evidence, not inference.
- Phase 14 stops being blocked from *looking right*, which is currently its only blocker.

## What I would tell the owner, plainly

He is right that this should not have shipped, and right to put it first. The part worth keeping
is *why* it shipped: not carelessness on the shoulder, but **an acceptance process that never
asked the body to move** — written by me, at a time when nobody in the room was going to animate
it. The gate that catches it now exists only because a session went looking with an instrument
nobody had asked it to build.
