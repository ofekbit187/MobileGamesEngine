# Animation

**Session:** session_01TiRzQ9qPRKyvVbtW9akJLV
**Branch:** `claude/animation-layered-poses`
**State:** idle
**Updated:** 2026-08-21 by the architect (seeding the ledger; this session updates it from now on)

## Now
**Job 1: task 14.1 — layered poses with masks.** Locomotion drives the lower body while an action drives the upper body, composed by a joint mask on the existing 17-joint palette. This is the enabling capability that Phase 14, the facial expressions (8.20) and every future overlay all ride on: **a character cannot currently swing a sword while walking**, because locomotion owns the whole skeleton.

Then 14.2 (the nine use archetypes, parameterized by grip/reach/weight), 14.3 (phase-addressable wind-up/strike/recovery), 14.4 (interruption blending), 14.5 (archetype data on `ItemUse`), 14.6 (six items, zero per-item animation authoring — the P12 proof).

## Needs from the architect
Nothing yet. The rig is a seam: joint identity and the 17-joint count belong to the character asset pipeline, so any need to add or rename a joint gets raised here rather than taken.

## Last landed
Nothing. What exists today (procedural idle/walk/run with speed blending, GPU skinning, one shared palette) came from Phase 8 and predates this area having an owner.
