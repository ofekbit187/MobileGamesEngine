# ADR 0020 — Reversing ADR 0019: no clavicle, and the gate was testing a pose no body makes

**Status:** accepted (architect ruling) · **Date:** 2026-08-21
**Reverses:** ADR 0019 · **Amends:** ADR 0016 (`B-31`) · **Raised by:** the character asset pipeline session

## What happened

I ruled the clavicle in ADR 0019 on two arguments. The session implemented it, measured it, and
**reported before spending the contract-version event.** The first argument is falsified.

| 140° total, one body, only the second joint changed | worst | edges >100% |
|---|---|---|
| arm 70° + **clavicle** 70° | 1.561 | **18** |
| arm 70° + **Chest** 70° | 1.187 | **1** |

And on its own the clavicle changes nothing: `B-31` across fifteen joint cases reads **58** on the
shipped 17-joint body and **61** with the clavicle added. **All of the improvement is the
reweighting; none of it is the clavicle.**

**My reasoning was backwards, and the session named exactly how.** I wrote that `Chest` "swings the
whole torso where a real clavicle carries only the shoulder girdle" — and offered that as the
clavicle's *advantage*. It is the disadvantage. **The tear is at the arm/torso boundary, and
relieving it requires the torso side of that seam to move.** A localised joint cannot relieve a
seam it sits entirely on one side of.

## Ruling 1 — the gate was wrong before the body was

The session's second proposal is the one that matters, and it reframes the whole problem:

> *A 140° arm raise with a rigid chest is not a pose a real body makes, nor one the archetypes
> need.*

That is correct, and **it is my error, not the body's.** I wrote `B-31` (ADR 0016) as *"at each
joint's working range"* — per joint, in isolation. But **a shoulder's working range is not
independent of the torso.** No human raises an arm to 140° with a locked chest; every archetype
that needs that reach brings the torso with it. We have been measuring an anatomically impossible
pose, calling the body defective for failing it, and were one ruling away from spending an
irreversible rig change to satisfy it.

This is the same shape as the mistake `B-31` was created to catch — a gate that tests something the
real system never does. It caught the body's genuine defect *and* carried this one.

**Ruled: `B-31`'s working range is defined per *motion*, not per joint in isolation.** Where a real
body recruits more than one joint, the gate poses them together. The shoulder case becomes
arm-plus-torso, as the archetypes actually drive it. The other fourteen cases are unaffected —
they already pass, and they are genuinely single-joint motions.

## Ruling 2 — no clavicle. ADR 0019 is reversed.

With the tear argument gone, the clavicle rests solely on the second argument: that captured human
motion contains shoulder-girdle rotation which has nowhere to go on a 17-joint rig. **That
argument is mine, it is untouched by this measurement, and it is entirely unmeasured.** We have no
capture pipeline, no tracked data, and no evidence of how much girdle motion our footage actually
contains or whether routing it to `Chest` reads wrong.

**ADR 0015's own reasoning applies to me now:** do not spend an irreversible, expensive change on a
premise nobody has measured. I used that to decline the clavicle the first time. It holds again.

And there is a convergence worth naming: **the destination for captured girdle motion and the fix
for the tear are the same joint.** A retargeter that distributes shoulder-girdle rotation into
`Chest` is doing exactly what the tear relief needs. One mechanism, both problems, no rig change.

**Revisit trigger, so this is a decision and not a drift:** when the mocap front-end exists and we
can measure how much girdle motion real captures carry, and whether `Chest` routing reads wrong on
the owner's eye. Not "later" — that condition.

## Ruling 3 — what actually lands, and it is much smaller

1. **The reweighting**, which is where all the measured improvement is (58 → 36).
2. **The hip and knee regression it introduces** (0 → 5, the far-end-of-band mechanism already
   recorded) fixed before it lands.
3. `B-31` amended per Ruling 1, and the shoulder case re-measured as arm-plus-torso.
4. Task 13.6's cut lines, the `cut_shell` tie-break, garment re-bake, new hash.

**No `Joint` enum change. No palette change. No shader change.** This is a contract-version event
but a far cheaper one, and it does not collide with the renderer's texture work in `skinned.vert`.

## Ruling 4 — the rig freezes at 17 joints, and it freezes now

**This is the good news and it belongs to the owner.** ADR 0018 held `mge_rig_export` back until
the joint list was final, so he would not author animation against a skeleton that moved under
him. **The joint list is now final at 17.** Task 17.4 publishes as soon as the reweight lands, and
his Blender work and the mocap bake both unblock — sooner than the clavicle path would have
allowed, not later.

## What this says about the method

Twice today this session has overturned a ruling of mine by measuring it, and **both times it
reported before spending the expensive step.** ADR 0019 exists because it proved no weighting could
fix the shoulder; ADR 0020 exists because it proved my fix for that didn't work either. The
willingness to come back and say *"the evidence I gave you was wrong"* about its own prior finding
is worth more than either result.

I am recording the reversal rather than quietly amending 0019, because the board and four other
sessions read these, and a ruling that changed needs to look changed.
