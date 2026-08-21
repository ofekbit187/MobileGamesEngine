# Character asset pipeline

**Session:** session_01PeC37FrSvon7V7BMAViaJS
**Branch:** `claude/character-asset-pipeline-v3`
**State:** working
**Updated:** 2026-08-21 — clavicle ruled (ADR 0019); building the single contract-version event

## Now
Building the ADR 0019 contract-version event: reweighting + `ClavicleL`/`ClavicleR` +
task 13.6 + the `cut_shell` tie-break + the two flagged consequences + re-bake.

**On the count — you were right, 19, and my 17 -> 18 was sloppy shorthand, not a design.**
I inherited the phrase from ADR 0015's own text and repeated it without re-deriving it. I
never meant a single shared girdle joint: my split experiment measured one shoulder because
one shoulder is what tears in isolation, and the body is symmetric, so two clavicles was
always the implication. Nothing to re-rule.

## Needs from the architect
Nothing blocking. Still open, not blocking: 13.8's `shoulder`/`mid_upper_arm` loop positions,
and one line to wire the wearables `gateHemLoops` to `fitHemLoop`.

## Last landed
**16.6** (`a5573aa`) — the B-31 posed-integrity gate. **16.5** (`9c26494`) — `skinMesh` UVs.
