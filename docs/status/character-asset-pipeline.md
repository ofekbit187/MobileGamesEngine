# Character asset pipeline

**Session:** session_01PeC37FrSvon7V7BMAViaJS
**Branch:** `claude/character-asset-pipeline-v3`
**State:** blocked
**Updated:** 2026-08-21 — 16.5 landed; 16.4 measured and held, needs a ruling

## Now
**16.5 is landed.** **16.4 is built, measured and deliberately NOT landed** — it misses its
acceptance number and it has a consequence ADR 0015 did not scope. I need a ruling before
either landing it or going further, so I am `blocked` rather than `working`. 13.9 is next
once this is settled.

## Needs from the architect

**1. 16.4 reweighting: land it, or hold it for the clavicle?** The reweight works and is not
enough:

- worst edge strain at 140 deg: **3.513 -> 1.578**; vertices at weight exactly 1.00: **53 -> 5**
- at 30 deg (locomotion's range): edges over 50% **5 -> 0**; the animation session's own
  layering test reports locomotion worst **0.311 -> 0.265**
- **acceptance was zero edges over 100% at 140 deg. It reaches 18.**

I stopped tuning on evidence, not fatigue: band widths 6-26 cm and rounds 4-20 are flat at
11 edges over 100% at 90 deg, smoothing the decimated LOD makes it *worse* (hops are 1.3 cm
upstream and 3 cm on LOD0 — ADR 0013's lesson again), and the same weights on the
**full-resolution 21 582-triangle body are worse still** (5.151 / 66), so it is not a
density limit either.

```
SEAM: Skin weights ⇄ region segmentation ⇄ wearables gates
NEED: A ruling on how a reweighting may move region boundaries, and whether 16.4 lands now.
      `regionOf()` reads the bone that moves a vertex most, so a reweight IS a
      re-segmentation: the Face/Neck/Torso boundaries move with the weights. It surfaced as
      the wearables `pit_measurement...` test failing its `jaw` assertion — "the tightest
      spot on the template is under the jaw; if this stops being true the body's head
      changed shape". The head did not change shape; the region partition did. ADR 0015
      scoped 16.4 as "data: no Joint enum edit, no shader change" — true of the weights,
      not true of their consequences.
BREAKS: Any reweight fails that assertion and re-cuts every region seam, which moves the UV
      chart's region boxes and the garment cut boundaries with them. It also currently
      produces degenerate triangles: the face-split bisect works on the set of faces the
      region rule calls "head", that set moved, and the plane now grazes faces it used to
      miss, making needle slivers that `dissolve_degenerate` cannot catch.
PROPOSAL: (i) hold 16.4 until the clavicle is decided and land ONE change — a reweight alone
      is a contract-version event re-baking six garments for a partial win, and ADR 0011's
      own rule is that such work rides an existing event rather than causing its own; or
      (ii) land it now and dispatch the wearables `jaw` assertion to be re-baselined. I lean
      (i), because of the next item.
```

**2. The clavicle, on the evidence ADR 0015 asked for.** ADR 0015: *"If a properly weighted
shoulder still tears at 140 deg, that is a clean measurement that one joint cannot carry the
rotation."* The shoulder is now properly weighted — no cliff, a real falloff band — **and it
still tears: 18 edges over 100%.** Under linear blending
`|dp| ~= dw * 2r * sin(theta/2)`; at 140 deg near the deltoid that is `dw * 0.30 m`, so a
2.7 cm edge needs `dw <= 0.09` between neighbours, which needs ~11 steps across a band that
has ~5 vertices. Adding triangles is measured not to help. **A clavicle halves theta per
joint and `sin(theta/2)` is where the term lives: 2*sin(35) = 1.15 against 2*sin(70) = 1.88,
a 39% reduction for the same weights.** I have not implemented it and am not ruling on it.
Numbers and full search space: `docs/research/shoulder-reweight.md`.

**3. Still open from 13.8** (not blocking me): where B-11's `shoulder` and `mid_upper_arm`
loops sit, and one line to wire the wearables `gateHemLoops` to `fitHemLoop`.

## Last landed
**16.5** — `skinMesh()` now writes UVs. One line, and it blocked every textured character: a
CPU-skinned body left `Vertex::uv` at {0,0} and sampled one texel for its whole surface.
Verified on the shipped body: skinned UVs span u 0.005..0.888, v 0.005..0.995 with **0 of
2097 vertices at (0,0)**, where before all 2097 were. Unblocks the first face texture and so
the retirement of ADR 0012's provisional Face waiver. No asset changed; body hash still
`1067c74324b6e091`. ctest 13/13, heap 0.

**Before that: 13.8** — hem-loop table and region vertex groups.
