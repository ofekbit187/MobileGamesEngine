# Animation

**Session:** session_01TiRzQ9qPRKyvVbtW9akJLV
**Branch:** `claude/animation-layered-poses`
**State:** ready
**Updated:** 2026-08-21 — 16.2 done: `anim_preview` is on the real skinned body, and 14.1 is
re-verified through skinning. Layering is clean; the shoulder is not.

## 16.2 — you were right to stop the capture, and the answer is not what either of us expected

`anim_preview` now renders through `buildPosedCharacter`. Every capture below is the imported
artist body. I took the method note seriously and did not just re-render: I built a gate that
**cannot pass by looking nice**, measuring per-edge strain on the CPU — how far each mesh edge's
length moves from bind, which is what tearing and pinching physically *are*. New permanent test:
`layering_does_not_shear_the_real_skinned_body`.

**Layering is clean.** The number that matters is what layering *adds* over the two poses it
blends — `strain(layered) − max(strain(walk), strain(action))`. Worst case **0.216**, against
locomotion's own worst of **0.311**. Blending two poses distorts this skin *less than the walk
cycle already does by itself*. 14.1 is verified on the thing it runs on.

**The feathering you predicted is not the fix — it is very slightly the wrong direction.**
Measured across 180 pose pairs (5 archetypes × 9 timeline points × 4 walk phases):

| spine feather | 0.00 | 0.25 | 0.50 | 0.75 | 1.00 |
|---|---|---|---|---|---|
| worst excess strain | **0.198** | 0.202 | 0.206 | 0.211 | 0.228 |

Monotone, and the hard cut is the *best* of them. The reason is that the body's spine and chest
weights already blend properly, so the skin absorbs the difference wherever you put it. I have
corrected the comment in `animation.h`, which claimed the feather prevented a tear — it does not,
and I should not have asserted that from a box-rig render. It stays as what it actually is: a
look control for how much torso joins the action.

**What does break the picture is the shoulder, and it is not this area's.** See the seam request
below. It is the first thing I would fix in the whole engine right now.

## Last landed

| Task | Commit | State |
|---|---|---|
| **14.1** layered poses with masks | `6af3990` | done |
| **14.2** the nine use archetypes | `52ee819` | done |
| **14.6** proof by catalog | `52ee819` | `[~]` — mechanism proven; the six live in the demo, not item data |
| **14.3** phase-addressable timeline | `101e6d5` | `[~]` — animation half done; wiring the damage moment is across the seam |
| **14.4** interruption | `101e6d5` | `[~]` — blend-out done and measured; the events that call it are across the seam |
| **14.5** archetype data on `ItemUse` | — | **not started, not mine** — seam request below |

**14.1 needed no rig change and no seam request.** Layering composes in pose space —
local joint rotations — *before* `evaluatePose`, producing one ordinary `Pose`. Nothing
downstream can tell a layered character from an unlayered one: one pose evaluation, one
palette upload, one skin pass. That is by construction, not by care. The `Joint` enum,
`Skeleton`, bind offsets and the 17-joint count are untouched.

## ● Real output — produced by the engine in this container

Host, llvmpipe (`tools/anim_preview`, 64 characters × 600 frames):

```
--- 14.1: what the upper-body mask moves (degrees from locomotion) ---
joint         mask     moved
Hips          0.00      0.0   <- locomotion keeps it
Spine         0.50      5.9
Chest         1.00     22.6
UpperArmR     1.00    129.7
ThighL        0.00      0.0   <- locomotion keeps it
ShinL         0.00      0.0   <- locomotion keeps it
FootL         0.00      0.0   <- locomotion keeps it

--- 14.1: cost of layering ---
layered poses composed: 38400
steady-state heap allocations: 0  (target: 0)
compose + evaluate: 0.602 us per character-frame
LayeredPose size: 280 bytes (scratch, shared by the whole crowd)
JointMask size: 68 bytes
palettes built: 38400  (one per character-frame, same as unlayered)

--- 14.6: six items, zero per-item animation authoring ---
item     archetype grip        reach  weight |  wind-up  strike  recovery  duration
sword    swing     versatile   1.05m   1.40kg |   36.63%  17.18%    46.19%    0.552s
spear    thrust    two-handed  2.40m   2.20kg |   34.25%  14.67%    51.08%    0.538s
axe      chop      one-handed  0.85m   2.60kg |   45.06%  14.42%    40.52%    0.774s
hammer   work      one-handed  0.45m   3.20kg |   48.28%  18.04%    33.68%    0.697s
torch    raise     one-handed  0.55m   0.70kg |   36.22%   9.62%    54.16%    0.529s
apple    consume   one-handed  0.10m   0.20kg |   40.20%  21.94%    37.86%    0.602s
closest pair of the six: sword vs hammer, 82.2 degrees apart at their widest

interrupt worst frame-to-frame jump: uninterrupted 0.650 rad, blended 0.549, snapped 2.172
```

`204 tests, 0 failed` — 27 of them new, in `tests/test_animation.cpp` (8) and
`tests/test_use_archetypes.cpp` (19).

### Verification — all three tiers, after merging integration

Integration `30726d7` merged into this branch cleanly (76 commits; it touched neither the
rig nor `items.h`, so nothing of mine conflicted). `scripts/verify.sh` then exits **0**:

```
=== 1/3 host: build + tests + runner        216 tests, 0 failed
=== 2/3 arm64 (NDK 27.0.12077973) QEMU      186 tests, 0 failed   (204 minus the 18
                                            host-only import tests)
    steady-state heap allocations: 0  (target: 0)   <- both tiers
=== 3/3 android: assembleDebug              app-debug.apk, 5,194,765 bytes
=== verification complete
```

216 rather than 204 because the merge brought in the wearables session's new gate tests.

### Captures — all now on the real skinned body

All ● real engine output, written by `tools/anim_preview` (which `ctest` runs, so
`scripts/verify.sh` regenerates every one of them). **Every capture is the imported artist body
through `buildPosedCharacter`.** None of them use the box rig any more.

- **`anim_walk_vs_layered.ppm` — still the headline, and now it is also the evidence for the
  shoulder problem.** Two characters at the *identical* walk phase: the left walking, the right
  walking and acting. The legs are bit-identical between them — the thing that could not happen
  before. And the right one's shoulder has visibly torn, which is exactly what the box rig would
  have hidden.
- `anim_shoulder_envelope.ppm` — the naked body with one joint rotated to 0/30/60/90/140 degrees
  and nothing else posed. Evidence *about the body*, not about anything this area built.
- `anim_interrupt.ppm` — a chop taking a hit mid-strike, easing back over five frames
  (layer weight 1.00 → 0.77 → 0.55 → 0.32 → 0.09) while the legs never stop.
- `anim_swing_strip.ppm` — one action across its timeline, walking throughout.
- `anim_mask_scope.ppm` — the same action through three masks. The right-hand figure
  (mask = everything) has its stride destroyed, which is what a whole-body action does.
- `anim_catalog.ppm` — the six items, bare-handed. Not a stylistic choice: held items are not
  drawn on the real body anywhere in the engine (see finding 2 above).

**On whether to show the owner anything yet — my recommendation is that you hold the headline
capture.** He has been waiting to see a character swing while walking, and the composition is
right, but the shoulder tears in the same frame. Showing it now means showing him a bug in the
body rather than the feature. I would rather you had the shoulder ruling first and then one clean
picture. That is your call and not mine — you know what he has been told and I do not.

I have not published anything and will not — the board is yours.

## Needs from the architect

### 1. SEAM REQUEST — the shoulder cannot take an action-scale rotation (this is the blocker)

```
SEAM:  Rig — `Joint` enum, `Skeleton`, bind offsets, 17-joint palette, and the skin weights
       the import path produces (AGENTS.md §4; Body ⇄ Wearables, Renderer)
NEED:  A character needs to be able to raise an arm. Measured on the shipped body
       (`humanoid_template_lod0.mgeskin`), NAKED, with nothing layered and no archetype
       playing — one joint rotated and nothing else:

         shoulder pitch |  worst edge strain | edges >50% | edges >100%
                 15 deg |              0.352 |          0 |           0
                 30 deg |              0.690 |          5 |           0
                 45 deg |              1.003 |         11 |           2
                 60 deg |              1.479 |         23 |           6
                 90 deg |              2.413 |         47 |          21
                140 deg |              3.513 |         71 |          32

       For scale: the shipped WALK puts zero edges over 50%, and the shipped RUN also puts
       zero over 50%. Locomotion stays inside roughly 35 degrees of shoulder rotation, which
       is exactly why eight phases of walking never exposed this. Every use archetype needs
       60-140 degrees, and all nine tear.

       Two structural facts behind it, both measured rather than guessed:
         * There is NO CLAVICLE. `UpperArmR`'s parent is `Chest` directly, so the entire
           shoulder rotation loads one joint — the worst case for linear-blend skinning.
         * The shoulder weights have no falloff. 53 of the 195 vertices influenced by
           `UpperArmR` are bound to it at weight exactly 1.00, and they sit adjacent to
           vertices that are 49% Spine. Adjacent vertices jump from fully-arm to
           mostly-torso with nothing in between, so the surface between them is what tears.

       The visible result is in `anim_walk_vs_layered.ppm`: the walking figure's shoulder is
       clean, and the acting figure's tunic sleeve has torn open into inverted triangles.
       Same body, same frame, same skinning — only the shoulder angle differs.
BREAKS: Depends entirely on which option you pick — see below. One is free, one is a
       rig-version event, one is a renderer change.
PROPOSAL: I am NOT proposing a specific fix, because the rig and the skin weights are the
       character asset pipeline's and the choice is theirs. The three options, with what
       each costs, so the decision has numbers under it:

       (a) REWEIGHT THE SHOULDER — give the deltoid/armpit a proper falloff instead of a
           hard 1.00 boundary. No rig change, no new joint, no shader change, no
           `Joint` enum edit. Re-bakes the body and re-bakes garments (the body hash
           changes, which the pipeline already refuses loudly, ADR 0008). Cheapest by far
           and it is where I would start.
       (b) ADD A CLAVICLE JOINT — anatomically the real answer, and it halves the rotation
           any single joint has to carry. But it is 17 -> 18 joints: a rig-version event
           that breaks every garment binding AND the skinning shader's palette size. Your
           ruling, explicitly, per §4. I have NOT done this and would not.
       (c) DUAL-QUATERNION SKINNING — fixes the collapse generally rather than per-joint,
           but changes `skinMesh()`, which is the definition GPU skinning must match, so it
           is a renderer + body change landing together with `mge_skin_test` reproving it.

       Until one of these lands, every upper-body action in Phase 14 will look torn at the
       shoulder on the real body, however correct the pose composition is. The animation
       side is not blocked from BUILDING — 14.1-14.4 are done and measured — but it is
       blocked from LOOKING RIGHT, and no amount of work on my side reaches it.
```

### 2. Held items are not drawn on the real body — anywhere, including the phone

Not a request, a finding, and it explains something in my first capture. `buildPosedCharacter`
skips held items (`if (garment.vertices.empty()) continue; // held items are not garments`), and
`device_game.cpp` does the identical thing at its line 543. So **no character in the shipped
engine has ever held anything.** The sword in my earlier capture existed only because the box rig
generated one as a rigid part — it was an artifact of the dead path, not a feature I lost.

This is wearables' (§6.3 — held items and grips). It also means my charter's "the sword teleports
between hand and back" is understating the gap: there is no sword in either place yet. Flagging
it because Phase 14's whole point is weapons, and 14.6's catalog capture is bare-handed for this
reason rather than by preference.

### 3. SEAM REQUEST — archetype data on `ItemUse` (this is 14.5, and it also unblocks 14.3 and 14.6)

```
SEAM:  Item use — `ItemUse`, `ItemUseRegistry` (AGENTS.md §4; Gameplay ⇄ UI ⇄ People)
NEED:  An item must be able to declare HOW it is used, in data. The engine now animates
       nine archetypes parameterized by grip, reach and weight, but nothing can reach
       those numbers: `ItemUse` carries `range`, `power` and a free-form `animKey`, and
       no archetype. Without this, P12's promise — "model it, declare `swing`, give it a
       reach and a weight, ship it" — stops one step short, because declaring it is
       exactly what an item cannot do. It is also what blocks hanging the damage moment
       on `strike` (14.3) instead of the tuned delay, and what keeps the six-item catalog
       (14.6) living in my demo rather than in shipped item data.
BREAKS: `ItemUse` gains fields, so the save schema bumps and `ItemUseRegistry` callers
       recompile. `framework/items.h` would include the animation side's enums, or those
       enums move somewhere both can see. Nothing existing changes meaning: `animKey`
       keeps working and simply narrows to the bespoke-clip escape hatch it was always
       meant to be (CHARACTERS.md §6.2).
PROPOSAL: Add four fields to `ItemUse`, defaulted so every existing item is unaffected:

       UseArchetype archetype = UseArchetype::Swing;
       ItemGrip     grip      = ItemGrip::OneHanded;
       float        reach     = 1.0f;   // metres, tip to grip — arc radius and lean
       float        weight    = 1.4f;   // kg — wind-up/strike/recovery timing

       Both enums and the `UseMotion` block they form already exist in
       `engine/include/mge/character/use_archetypes.h` (mine). Two ways to satisfy the
       include direction, and I have no stake in which — your call:
         (a) `items.h` includes the animation header; or
         (b) the two enums move to a small shared header and both sides include it.
       (b) is tidier if `framework/` must not depend on `character/`.

       Then gameplay drives a `UsePlayer` per acting character: `start(motion)` on
       `use_held`, and the frame `update()` returns true is the damage moment — deleting
       the tuned delay. `interrupt()` on hit/stagger/death closes 14.4.

       I have NOT written any of this. `items.h` is gameplay's and this is a seam.
```

### 4. A rig observation — not a request, and explicitly not a change I would make

Measuring the archetypes turned up something in the rig that I want on your desk rather
than in my code. In `buildSkeleton`, the `*L` joints are placed at **positive** local X and
the `*R` joints at **negative** X, while forward is **−Z** (`LocomotionAnimator`: "positive X
rotation swings the limb forward (−Z)", and `emitRig`'s yaw convention agrees). For a body
facing −Z with +Y up, the character's right-hand side is **+X** — so the joints named `R`
sit on the character's **left**.

It costs nothing today, because nothing in the engine is left/right asymmetric: I simply
authored my motions against where the joints actually *are*, and noted the sign rule in
`use_archetypes.cpp` so the next reader is not caught by it. It will start costing when
something asymmetric appears — a left-handed character, a scabbard on one hip, a garment
with one pauldron — because at that point the naming and the geometry disagree and someone
will "fix" the wrong one.

The rig is a seam and the `Joint` enum is the character asset pipeline's. I have not
touched it and am not proposing that anyone rename anything now — a rename would invalidate
every weight and garment binding for a cosmetic gain. **What I would suggest is one line of
documentation in the body contract** recording which side `*L` and `*R` are on, so the fact
is written down once instead of rediscovered. Your ruling, and theirs.

### 5. The ownership map and my charter disagree on paper

`AGENTS.md` §3 still maps **all** of `engine/*/character/**` — "(body, rig, variants,
skinning, animation)" — to the character asset pipeline, and §2's roster has no animation
area. My charter gives me animation and pose evaluation. I worked to the charter and put
everything new in files nobody else had (`character/animation.*`,
`character/use_archetypes.*`, `tests/test_animation.cpp`, `tests/test_use_archetypes.cpp`,
`tools/anim_preview/`), and I did not touch `humanoid.h`, `humanoid.cpp`, `character.h`,
`items.h` or `garment_fit.*`. So nothing has actually collided.

But the table is what the *next* session will read, and it currently says these files
belong to someone else. Worth one edit either way — either the table gains an animation
row, or you tell me the split is meant to be temporary and this all reverts to the
character asset pipeline when that area next runs. Yours to decide; I have not edited
`AGENTS.md`.

### 6. Two small things I did take, flagged for the record

- **One line in the root `CMakeLists.txt`** registering `tools/anim_preview` (mine under
  AGENTS.md §3, "`tools/<demo>/**` — whoever the demo demonstrates"). Without it the tool
  cannot build. Move it if you would rather it lived elsewhere.
- **`Quat` has no `slerp`/`nlerp`** in `engine/include/mge/core/math.h`. Rather than widen
  a shared core header, the blend helper stays private to the animation module. If you want
  it promoted later, that is your call, not mine to take.

**No ADR number has been self-assigned.** I see 0013 was minted while I was working, so
0014 is presumably next — but that is your call, not mine, and none of the above needed
one.

## What I would build next, if this area gets another job

In rough order of what unblocks the most: closing 14.5/14.3/14.4 end to end once the seam
is ruled (small, and mostly gameplay's side); then facial expressions (8.20), which now have
their layering prerequisite — though 10.2 says they still wait on face geometry and the
renderer's morph-delta path, so they are not startable yet. Draw/sheathe transitions
(the sword still teleports between hand and back) are the other visible gap; that one
touches held-item attachment, which is wearables', so it wants a ruling before it starts.

## A note on the environment, for whoever runs this next

The container arrived with **no Vulkan and no Android SDK/NDK/qemu**, so every GPU demo and
two of the three verification tiers were silently skipping. Both are fixable in place:
`apt-get install libvulkan-dev mesa-vulkan-drivers qemu-user-static`, then
`scripts/setup-android-sdk.sh`. With llvmpipe up, `mge_vk_smoke` reports
`pixels verified: 65536, wrong: 0` and captures are possible — which is the only reason
there is a picture of a character swinging while walking rather than a paragraph claiming
there could be one.
