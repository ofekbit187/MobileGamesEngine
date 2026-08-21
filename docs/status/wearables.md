# Wearables & equipment

**Session:** session_01TV2bC4KKZ42EJLh6UdtAun
**Branch:** `claude/wearables-system-research-1h16tq`
**State:** ready
**Updated:** 2026-08-21 — 14.7 done: characters hold things now

## Now

**14.7 is complete. A character in this engine holds something for the first time.**

`engine/*/character/held_items.*`: named sockets (`hand_r`, `hand_l`, `back`, `hip_l`,
`hip_r`) derived from the rig so they follow a variant's proportions with no per-variant
data; grip type and grip transform declared in data; `placeHeldItem` puts the item's static
mesh into character-local space through `jointWorld[anchor] · socket · grip`.
`buildPosedCharacter` emits held items instead of skipping them — the line that dropped them
is replaced, and commented with why, so it does not come back.

A sword is an ordinary `.mgewear` row with `held true` naming a static `.mgemesh`, so a
second weapon is a data change (13.12 applies here too). `tools/item_bake` produced the
shipped `item_sword.mgemesh` once, offline; the runtime knows nothing about how it was made.

**On your instruction to re-check 8.19 rather than assume it survived: almost none of it
did, and you were right not to assume.** Nothing visual survived — `buildPosedCharacter`
skipped held items outright, so no character on the skinned path had ever rendered one. What
*was* real and stayed real is the slot machinery and the sheathed flag: the AI genuinely
draws on contact and sheathes afterwards, it simply had nothing to show for it. 8.19 is
updated to say that instead of what it used to claim.

## Needs from the architect

Nothing blocking. Two items are open and both sit across seams I did not cross:

- **Dual wield.** `GripType::TwoHanded` is parsed and carried on the item, but nothing yet
  occupies both hand slots — slot occupancy is gameplay's equipment machinery, not mine. The
  item now declares its grip, which is the half that was missing; when 14.5 lands, the same
  word reaches `ItemUse` and animation reads it too.
- **Draw/sheath transition animations.** The archetype library already has `draw` (14.2).
  Playing it on a draw/sheath state change is animation's to trigger; my side supplies the
  two placements it moves between.

Both are noted rather than requested — say the word if you want either raised properly.

## Last landed

**14.7** (`this push`) — held items on the real body. 10 tests in `tests/test_held_items.cpp`;
capture `build/held_sword.ppm` from `tools/held_preview`. 199 → 241 tests, all green;
`steady-state heap allocations: 0`; the seven wearable gates still read 6/0/1.

**The render earned its place three times, and this is the part worth carrying forward.**
`tools/held_preview` rasterizes on the CPU because the other previews need Vulkan, which this
container does not have. Every one of these passed a plausible numeric check first:

1. The sheathed sword was **lying flat across the shoulder blades** — 0.18 m of vertical
   extent for an 0.83 m weapon. Cause: the hand grip rotation was being applied to the back
   socket, where it is meaningless.
2. Corrected, it then stood **hilt-down with its point above the character's head** (tip at
   y = 2.17 m on a 1.75 m body). Cause: grip space runs the blade +Y away from the hand, so a
   carry socket has to invert it — a scabbard is 180° plus the cant, not just a translation.
3. Drawn, the blade pointed **horizontally forward like a presented lance**. Cause: I guessed
   the grip rotation. The hand's local +Y runs *up* the arm, so an unrotated blade aims at the
   sky and the resting value is ~160°, which I picked by looking at three renders.

A centroid is silent on all three. MODELING.md §6's "looked at" gate is not a nicety.

**One real bug found and fixed while doing this:** caches keyed by catalogue *index* — garment
meshes, bindings, held-item meshes — were function-local statics built once, so
`reloadWearableCatalogue()` left them stale. After a reload that reordered rows, an index did
not merely miss, it named a **different garment**. They now follow a catalogue generation
counter, with `reloading_the_catalogue_does_not_leave_stale_meshes_behind` pinning it. That
was mine, introduced in 13.12 and caught by the 13.12 proof test failing once held items
shifted the row order.

## Watching

- The trouser hem at the ankle with no boots still leaves 42 mm of rim against a 45 mm
  allowance. The first authored trousers that sit higher will fail that gate.
- `WearableKind` must survive task 16.3's deletion of `buildHumanoidVisual` from
  `character/humanoid.h` (ADR 0017 Ruling 2). Noted here because I am the one who would feel
  it: `held_items.h` and the catalogue both reach for it.
- Expect my bindings to re-bake when the pipeline session's `B-31` reweighting moves the body
  hash again. `mge_garment_fit` re-runs it; `garment_bindings_match_the_shipped_body` is the
  alarm.
