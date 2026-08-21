# Character asset pipeline

**Session:** session_01PeC37FrSvon7V7BMAViaJS
**Branch:** `claude/character-asset-pipeline-v3`
**State:** blocked
**Updated:** 2026-08-21 — clavicle implemented and measured; **it does not fix the tear**

## Now
**I implemented ADR 0019's clavicle, measured it, and it does not do what my own evidence
predicted. I am reporting before spending the contract-version event**, because that event
breaks every garment binding and the shader palette and is not worth spending on a premise
that has failed. Tree is green and unchanged; nothing landed.

**On the count, first: 19 is right and my 17 -> 18 was sloppy shorthand**, inherited from ADR
0015's own text and repeated without re-deriving. I never meant a single shared girdle joint.
Nothing to re-rule there.

## Needs from the architect — I supplied the evidence for ADR 0019 and it was wrong

### 1. The clavicle does not fix the shoulder tear. Torso participation does.

I inferred that a clavicle would do what the `Chest` stand-in did, *only better*, because
`Chest` "swings the whole torso where a real clavicle carries only the shoulder girdle".
**That reasoning was backwards.** Being localised is exactly why it does not help: the tear is
at the ARM/TORSO boundary, and relieving it needs the torso side of the seam to move. `Chest`
moves it; a clavicle does not.

Control, one body, one pose split, only the second joint changed:

| 140 deg total | worst | edges >100% |
|---|---|---|
| arm 70 + **clavicle** 70 | 1.561 | **18** |
| arm 70 + **Chest** 70 | 1.187 | **1** |
| arm 93 + clavicle 47 (2:1 rhythm) | 1.561 | 18 |
| arm 93 + **Chest** 47 | 1.387 | **1** |

The clavicle is not inert — 214/192 vertices, ~35 weight mass per side. It just does not
relieve this seam.

And on its own it changes nothing at all. B-31 across all fifteen joint cases:

| body | shoulders | hips | knees | B-31 total |
|---|---|---|---|---|
| shipped, 17 joints, automatic weights | 32 + 26 | 0 | 0 | **58** |
| **clavicle, 19 joints, automatic weights** | 33 + 28 | 0 | 0 | **61** |
| clavicle + reweighting | 20 + 11 | 4 | 1 | **36** |

**All of the improvement is the reweighting; none of it is the clavicle.** And the reweighting
brings a regression: hips and one knee start tearing (0 -> 5) from the far-end-of-band
mechanism already recorded.

```
SEAM: The rig (Joint 17 -> 19) — re-ruling requested on ADR 0019
NEED: ADR 0019 rests on two arguments. The FIRST — that a clavicle stops the tearing — is the
      one I supplied and it is falsified above. The SECOND — that captured human motion
      contains shoulder-girdle rotation which has nowhere to go on a 17-joint rig — is
      untouched, comes from the owner, and justifies the clavicle on its own.
BREAKS: Nothing yet; I have landed nothing. Spending the event costs every garment binding and
      the shader palette.
PROPOSAL: (a) Decide the clavicle on the MOTION-CAPTURE requirement alone, where the case is
      sound, and stop crediting it with the tear fix; and (b) before treating the tear as a
      body defect at all, measure whether a torso-participating shoulder pose closes B-31 —
      a 140 deg arm raise with a rigid chest is not a pose a real body makes, nor one the
      archetypes need. If it closes, the body needs the reweighting (hip/knee regression
      fixed) and nothing more, and the fix lives next to the animation session's layered-pose
      work rather than in a rig-version event. I can implement whichever you rule; the rig
      edits are five files and about thirty minutes.
```

### 2. If you still want the clavicle landed (I think you should, for capture)
Say so and I will land it as its own small rig-version event with the reweighting, the hip/knee
regression fixed, 13.6, the `cut_shell` tie-break and the re-bake — the full ADR 0019 bundle —
on the understanding that **B-31 will not go green from it** and the shoulder pin stays.

### 3. Still open, not blocking
13.8's `shoulder`/`mid_upper_arm` loop positions; one line wiring the wearables `gateHemLoops`
to `fitHemLoop`.

## Last landed
**16.6** (`a5573aa`) — the B-31 posed-integrity gate, which is what caught all of this.
**16.5** (`9c26494`) — `skinMesh` writes UVs. Body hash unchanged `1067c74324b6e091`;
ctest 13/13; heap 0. Evidence for the above: `docs/research/shoulder-reweight.md`, second
addendum. Working pipelines saved for whoever lands it: `clav_only.py.keep` and
`clav_reweight.py.keep` in this session's scratch; engine-side it is five edits — `Joint` enum,
`buildSkeleton`, `mirrored`, `jointName`, and `skinned.vert`'s palette constant.
