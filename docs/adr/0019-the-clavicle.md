# ADR 0019 — The clavicle is earned; the rig goes to 19 joints and then freezes

**Status:** accepted (architect ruling) · **Date:** 2026-08-21
**Depends on:** ADR 0015 (which left this open on evidence), ADR 0016 (`B-31`), ADR 0018 (rig freeze)
**Raised by:** the character asset pipeline session, task 16.4

## ADR 0015's condition is met

ADR 0015 declined a clavicle on the grounds that the evidence was contaminated — a body with
broken weights cannot tell you whether the *rig* is inadequate. It said, explicitly: *"if a
properly weighted shoulder still tears at 140°, that is a clean measurement that one joint cannot
carry the rotation."*

**It is properly weighted now, and it still tears.** The measurement is clean, and it is decisive
in a way I did not expect — the session did not merely fail to find a working weighting, it showed
**no weighting can exist**:

> A vertex at radius `r` from the joint displaces by `2r·sin(θ/2)`. Spreading the influence band
> out to radius `R` gives that vertex gradient `1/R` and displacement proportional to `R` — **the
> two cancel. Strain is scale-invariant in the band width.**

That is why widening the band never helped, across band widths of 6–45 cm, 4–20 smoothing rounds,
tapered and hard prunes, dense and decimated meshes. Reweighting drives the quality factor `K`
from 1.87 to 0.84 — a real improvement, the 1.00-weight cliff falling from 53 vertices to 5 — and
`B-31` at 140° needs `K < 0.53`. Every configuration plateaus above it.

Splitting the rotation across two joints is what closes it (`Chest` standing in for the clavicle
the rig does not have):

| | one joint (140° arm) | split 70° arm + 70° chest |
|---|---|---|
| shipped weights | 3.513 worst, **32** edges >100% | 1.672, **20** |
| reweighted | 1.578, **18** | 1.197, **1** |

**Neither half reaches zero alone; together they take 32 → 1.**

## Decision — add clavicles. And there is a second reason, which the owner supplied today.

**Ruled: the rig gains a clavicle per side. `Joint` goes from 17 to 19.**

The tearing measurement alone would justify it. But there is an argument the session could not
have known to make, because it arrived hours ago in a different conversation:

**The owner is about to capture real human motion from video.** A real shoulder girdle moves —
the clavicle rotates and elevates through every arm raise, and captured data *contains that
motion*. On a 17-joint rig it has nowhere to go: we would have to discard it, or dump it into the
torso, which is exactly the `Chest` stand-in whose weakness the session already named — *"it
swings the whole torso where a real clavicle carries only the shoulder girdle."* A character
raising an arm would rock its whole chest.

So the clavicle is not only what stops the mesh tearing. **It is what makes captured human motion
representable at all**, and hand-authored animation natural to key. Both of the owner's animation
routes want it.

**On the count: two, not one.** The session's request says 17 → 18 throughout, and its split
experiment measured a single shoulder. A clavicle is a per-side bone and symmetry is not optional
— `ClavicleL` and `ClavicleR`, so **17 → 19**, and the skinning shader's palette sizes to 19.
Confirm against your own measurement before landing; if you meant something else by 18, say so in
your ledger and I will re-rule.

## Ruling 2 — this is the last rig-version event, so everything pending rides it

Breaking every garment binding and the shader palette is expensive exactly once. **ADR 0011's
rule — such work rides an existing event rather than causing its own — now points the other way:
if we are paying for the event anyway, everything else that needs one goes in it.**

The session was right not to land the reweight alone; that would have spent a contract-version
event and six garment re-bakes on a partial fix. **One event, containing:**

1. The reweighting (built and measured, held deliberately).
2. `ClavicleL` / `ClavicleR`, palette to 19, skinning shader updated.
3. **Task 13.6** — the elbow/knee cut lines (ADR 0008 D-3) and ear sub-shell, still open, and
   itself a cut-geometry contract-version event. It rides this one.
4. The `cut_shell` tie-break fix ADR 0011 folded into 13.6.
5. The two consequences the session flagged: the region re-partition (`regionOf()` reads the
   dominant bone, so a reweight re-segments the body and needs the wearables `jaw` pit assertion
   re-baselined) and the degenerate needle slivers the moved face-split plane creates.
6. Garment re-bake, new body hash, `B-31` green at 140°.

## Ruling 3 — after this, the rig is frozen

**This is the event ADR 0018 was waiting for.** Once it lands, the joint list is final and
`mge_rig_export` (task 17.4) publishes — the owner can author in Blender against a skeleton that
will not move under him, and the mocap path can bake against a stable target.

Frozen means: **no joint added, renamed, or moved in the bind pose without an architect ruling
that treats it as breaking published content**, because from that point it *is* breaking the
owner's own work, not just ours. `B-31` and gate §9.8 exist to make sure we never again discover
a rig inadequacy after content depends on it.

## Also recorded

`16.6` landed the executable gate and it immediately earned its place: posing the body at fifteen
joint cases showed that **only the shoulders tear** — elbows, knees, hips, ankles, wrists, neck,
spine and chest are already at zero, the hip closest at 0.999. **ADR 0016 assumed the other joints
shared the shoulder's fate.** They do not. Checking was right; the assumption was wrong, and the
gate is what distinguished them. Worth knowing before anyone widens a hip range later.
