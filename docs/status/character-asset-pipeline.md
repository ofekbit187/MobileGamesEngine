# Character asset pipeline

**Session:** session_01PeC37FrSvon7V7BMAViaJS
**Branch:** `claude/character-asset-pipeline-v3`
**State:** blocked
**Updated:** 2026-08-21 — 16.6 landed; 16.4 measured to a conclusion, needs the clavicle ruling

## Now
**16.6 landed.** **16.4 is measured to a conclusion and held.** The owner's priority is the
shoulder, and I can now say precisely what closes it — but the last step is a rig-version
event, which is yours. I am `blocked` on that one ruling, not on work.

## Needs from the architect

### 1. The clavicle. ADR 0015's condition is met, and here are the numbers.

ADR 0015: *"if a properly weighted shoulder still tears at 140 deg, that is a clean
measurement that one joint cannot carry the rotation."* **It is properly weighted now, and it
still tears.**

**Weighting alone cannot close it, and that is measured, not argued.** Sweeping the shipped
body 40–140 deg, worst strain divided by the rotation's own kinematic factor is nearly
constant — `worst ~= K * 2*sin(theta/2)`, ratio 1.32–1.87. K is what weighting controls.
Reweighting drives **K from 1.87 to 0.84** (the 1.00-weight cliff falls 53 vertices -> 5), but
zero-over-100% at 140 deg needs **K < 0.53**, and every configuration plateaus above it: band
widths 6–45 cm, 4–20 smoothing rounds, tapered and hard prunes, dense and decimated meshes.

The reason is geometric and it is why widening the band never helped: a vertex at radius `r`
from the joint displaces by `2r*sin(theta/2)`, so spreading the band to radius `R` gives it
gradient `1/R` and displacement proportional to `R` — **the two cancel. Strain is
scale-invariant in the band width.**

**Two joints do close it.** 140 deg of total rotation, carried two ways, `Chest` standing in
for the clavicle the rig does not have:

| | one joint (140 arm) | split 70 arm + 70 chest |
|---|---|---|
| shipped weights | 3.513 worst, **32** edges >100% | 1.672, **20** |
| reweighted | 1.578, **18** | 1.197, **1** |

**Neither half reaches zero alone; together they take it 32 -> 1.** `Chest` is a poor stand-in
— it swings the whole torso where a real clavicle carries only the shoulder girdle — so a real
one should do better.

```
SEAM: The rig (Joint enum, 17 -> 18) — ADR 0015 left this door open on evidence
NEED: A ruling on adding clavicle joints. Reweighting alone cannot meet B-31 at 140 deg; the
      arithmetic above says why, and the split experiment says what does.
BREAKS: 17 -> 18 joints breaks every garment binding and the skinning shader's palette size,
      exactly as ADR 0015 says. It is a rig-version event.
PROPOSAL: Rule the clavicle, then land reweighting + clavicle + garment re-bake as ONE
      contract-version event and delete the pin in the new gate. I have deliberately NOT
      landed the reweighting on its own: it would spend a contract-version event and six
      garment re-bakes on a partial fix, and ADR 0011's own rule is that such work rides an
      existing event rather than causing its own. If you would rather have the partial
      improvement now, say so and I will land it — it is built and measured.
```

### 2. Two things reweighting drags with it, when it does land
- **It re-cuts the region partition.** `regionOf()` reads the dominant bone, so a reweight is
  a re-segmentation; it fails the wearables `jaw` pit assertion on a body whose head did not
  change shape. Needs their re-baseline.
- **Degenerate triangles.** The face-split bisect works on the set of faces the region rule
  calls "head"; that set moves with the weights and the plane then grazes faces it used to
  miss, making needle slivers `dissolve_degenerate` cannot catch (a needle has long edges and
  no area).

### 3. Still open, not blocking: 13.8's `shoulder`/`mid_upper_arm` loop positions, and one
line to wire the wearables `gateHemLoops` to `fitHemLoop`.

## Last landed
**16.6** (§9.8 / B-31 executable gate) and **16.5** (`skinMesh` writes UVs) — `9c26494` and
this push. The gate poses the body at fifteen joint cases. **Measured across all of them, only
the shoulders tear**; elbows, knees, hips, ankles, wrists, neck, spine and chest are already at
zero, the hip closest at 0.999 worst — worth knowing before anyone widens a hip range. ADR
0016's suspicion that the other joints shared the shoulder's fate was right to check and the
check says otherwise.

No asset changed; body hash still `1067c74324b6e091`. ctest 13/13, heap 0.
