# ADR 0018 — Authored animation: the round trip is the deliverable

**Status:** accepted (owner dictation, 2026-08-21) · **Date:** 2026-08-21
**Depends on:** ADR 0007 (rig), ADR 0008 (P12), ADR 0015/0016 (the joints being fixed now)
**Supersedes:** the "authored clip import via glTF still open" note on task 8.11

## The dictation

> *"i want you to add a capebility to import animations that were made for our models externally,
> (i want to animate some of the models myself using blender) i want that it will be easy — i will
> get the rigged model from the engine and i will create animation specifically for it"*

Two things are being asked for, and the second is the harder one. **An importer is not the
deliverable. The round trip is** — *get the rigged model out → animate it → put it back in →
see it in the game*, repeatedly, without an engineer in the loop. That is P12 applied to
animation, and it is the standard this work is measured against.

## What exists today

Nothing of it. Measured before designing:

- **No clip data structure at all** — no `AnimationClip`, no `.mgeanim`. Every animation in the
  engine is *procedural*: idle, walk and run are generated from speed and phase.
- **No glTF animation import.** `gltf_skin_import.cpp` reads meshes, skins and morph targets, and
  ignores the `animations` array entirely.
- **No published rig to author against.** Task 13.9 (`B-29`) was scoped as a *garment* authoring
  reference — geometry, region groups, hem loops. Animating needs the *skeleton*, which is a
  different export with different requirements.

One thing does exist and is worth more than it looks: **`jointName()` and a canonical
name → index map already live in the importer.** The naming half of the contract is built.

## Decision

**Build both halves of the round trip, as two tools with one contract between them.**

**Out — `mge_rig_export`.** A single `.glb` the owner opens in Blender: the skeleton with
canonical bone names, the bind pose, the body mesh bound to it, and **the engine's existing
procedural locomotion baked in as reference clips**. That last part matters — matching the
timing and style of what the engine already does is far easier with idle and walk sitting in the
file than with a written description of them.

**In — `mge_anim_import`.** Validates a `.glb` and bakes it to `.mgeanim`, with **pass/fail and a
reason a non-engineer can act on** (P12). What it must catch, because these are the mistakes that
actually happen: a renamed, missing or extra bone; a bind pose that differs from the published
rig; non-uniform joint scale (linear-blend skinning cannot represent it); keys on joints that do
not exist; and a duration or frame rate the budget refuses.

## Ruling 1 — clips are in-place by default, and root motion is reported, never silently dropped

Locomotion today is **distance-driven**: the walk cycle's phase comes from ground travelled, which
is what keeps feet from sliding. A clip carrying its own root translation fights that directly.

**Ruled: imported clips are in-place.** Root translation is **measured and reported at import**,
not quietly discarded — an animation authored walking across the scene must tell its author *"I
removed 2.4 m of root travel"* rather than mysteriously playing on the spot. Root-motion-driven
clips are a later capability with its own ruling, not a v1 default.

## Ruling 2 — a clip records the rig it was authored against and refuses on mismatch

This is the part that protects the owner's own hours, and it is the reason the sequencing below
matters more than the code.

**His clips are content addressed to a specific skeleton.** Exactly as garment bindings are
content addressed to a specific body — and that machinery already exists and works: bindings carry
`rootBodyHash` and the pipeline refuses loudly rather than rendering something wrong. Clips get
the same treatment: **a rig version hash, checked at load, refused with the mismatch named.**

## Ruling 3 — memory: clips are shared, players are per-character

A clip is 17 joints × keyframes × rotation, and the naive version — a keyframe array per clip per
character — is exactly the shape that blows a mobile budget in a crowd. **The clip is immutable,
resident once, and sampled by everyone; only the playback cursor is per-character.** Quantized
rotations, a registered `BudgetRegistry` budget whose cap refuses, and eviction like any other
asset. Same rule the body and the skin maps already follow.

## Sequencing — and a warning I would rather give now than after a lost weekend

**Two changes would invalidate every clip the owner authors: adding or renaming a joint, and
changing the bind pose.** Both are rig-version events.

**One of those is currently an open question.** ADR 0015 deliberately left the clavicle undecided —
if a properly reweighted shoulder still tears at 140°, adding a clavicle joint (17 → 18) becomes
the answer, and **that would break every hand-authored clip in existence.** The reweight (task
16.4) is in flight now and will produce the measurement that settles it.

**Ruled: `mge_rig_export` does not publish until the clavicle question is closed.** Everything
that does *not* depend on the final joint list is built in the meantime — the clip format, the
runtime, playback and blending, the importer's validation — so nothing waits unnecessarily. But
the rig the owner is invited to animate against is published **once, deliberately, and frozen**,
not published now and revised under him.

To be precise about what is and is not at risk: **the reweighting itself does not threaten his
work.** Skin weights are not part of the skeleton, so a clip authored against today's joint list
survives 16.4 untouched. It is only the joint *count and names* that must settle first.

## Consequences

- Phase 17 opens with the split above; the runtime half starts immediately in the animation area.
- Task 13.9's authoring reference gains a sibling: geometry for garment authors, skeleton for
  animation authors — related exports, different requirements, not one file pretending to be both.
- The nine procedural use archetypes (Phase 14) are **not** replaced. They remain the zero-cost
  default that makes a new weapon free (P12); authored clips become the opt-in hero path, which is
  exactly the escape hatch `ItemUse::animKey` was always reserved for.
- Mocap retargeting — the character asset pipeline's charter names it — becomes reachable once
  clip import exists, but is not in scope here.
