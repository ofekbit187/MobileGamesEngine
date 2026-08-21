# Animation

**Session:** session_01TiRzQ9qPRKyvVbtW9akJLV
**Branch:** `claude/animation-layered-poses`
**State:** ready
**Updated:** 2026-08-21 — 17.1 and 17.2 built. The clip runtime exists, and baking a clip found a
real bug in my own Phase 14 code.

## 17.1 / 17.2 — the clip runtime, and what building it exposed

**Both done and measured.** `engine/*/character/animation_clip.*`. The numbers ADR 0018 Ruling 3
actually rests on, from `tools/anim_preview` on the engine's own walk baked to a clip:

```
clip 'locomotion_walk': 64 frames x 17 joints, 4352 bytes RESIDENT ONCE
  at full float that would be 17408 bytes; quantized to 4 bytes a rotation
ClipPlayer: 32 bytes per character (a pointer and a cursor)
a 64-character crowd therefore costs 2048 bytes of players + ONE 4352-byte clip
sampling + layering a shared clip, 38400 character-frames:
  steady-state heap allocations: 0  (target: 0)
  1.460 us per character-frame
```

**Quantization measured, not assumed:** smallest-three at 32 bits gives worst 0.15°, mean 0.08°.
A limb 0.6 m long moves 1.6 mm at that error.

**Interchangeability (17.2) is proven the strongest way I could find:** bake the engine's own
procedural archetype into a clip, then play both through the same composer. They differ by
**0.1517°** — against 0.1497° measured independently as pure quantization. The two paths agree to
the bit beyond the encoding. Masked-out joints stay bit-identically locomotion's in both.
`ClipPlayer` mirrors `UsePlayer`'s verbs exactly, so a caller swaps one for the other without a
second vocabulary.

**A design decision I took that nobody specified, flag it if you disagree.** `AnimationClip`
carries a `strikeFraction`. Without it a clip cannot answer "when does the blow land", which
`UsePhases::strike` answers for an archetype — and 17.2's requirement is that nothing downstream
can tell the two apart, which includes gameplay hanging damage on the moment. It is one float,
defaulted to 0.5 until an importer sets it. Strike it if you would rather 17.3 decide the shape.

### The bug baking a clip found in my own Phase 14 work

`sampleUseArchetype` had a **step discontinuity**. The reach-driven lean was
`+ 0.20 * r * (lean >= 0 ? 1 : 0)`, so the instant `lean` crossed zero the bonus switched on or
off whole and popped the chest **3.9° in a single frame**. Every archetype whose lean changes sign
went through it, and `chop` does so on its first frame.

It survived all of Phase 14 — including the tests I wrote and the captures I checked — because
nothing sampled the motion finely enough to see one frame. Baking a clip is precisely what does.
Fixed by scaling with the lean itself: same magnitude at the extremes, continuous through zero.

Worth naming the pattern, because it is the second time on this area: **a defect invisible to the
instrument I had, revealed the moment a new instrument arrived.** The box rig could not show
shearing; per-frame sampling could not show a one-frame pop. Both were found by building the next
thing, not by looking harder at the last one.

### Export-rate guidance, measured — for 17.3's importer and 17.6's page

Clip fidelity is dominated by **frame rate, not quantization**:

| frames | 16 | 32 | 64 | 128 | 256 |
|---|---|---|---|---|---|
| worst error | 24.7° | 4.0° | 7.5° | 3.7° | 1.8° |

Not monotonic, and the reason is worth passing to whoever writes the importer: the worst error
sits at **t=0.578 for every single frame count** — exactly where an archetype's accelerating
strike hands over to a recovery that starts from rest. That deliberate velocity kink at impact is
what linear interpolation cannot cross cheaply, so the error depends on whether a frame happens to
land on it. **A fast strike wants a high export rate, and the error concentrates at the instant of
impact.** If the importer warns about rate, that is the number to warn against.

### One property I want on the record, because it protects the owner's weekend

The rig hash covers **the joint list and the bind pose, and deliberately nothing else** — not
per-variant bone lengths, not skin weights. Two consequences, both tested in
`a_reweighted_body_does_not_invalidate_a_single_clip`:

- **ADR 0016's reweight of every bending joint invalidates no authored clip.** You said as much in
  the sequencing note; this is the mechanism that makes it true rather than a hope.
- A clip authored against the template plays on a 2.10 m character and a 1.40 m one, because every
  variant shares the rig. A hash over the variant's skeleton would have refused that, which would
  have been wrong.

## 14.5 landed, and 14.3 needs a correction before anyone builds it

**Built exactly as ruled.** `engine/include/mge/framework/use_archetype.h` holds `UseArchetype`
and `ItemGrip`; `character/use_archetypes.h` includes it; `ItemUse` gains the four defaulted
fields; `motionFromItemUse()` is the bridge and clamps on the way across, because item data is
content. Thank you for checking the include direction rather than taking my "no stake" at face
value — I had not looked, and (a) would have closed a directory cycle. Noted for next time: when
I say I have no preference, that is often a sign I have not measured, not that the choice is free.

**The correction. There is no tuned delay to delete.** This task, `CHARACTERS.md` §6.2 and ADR
0017 all describe replacing a hard-coded damage delay. There isn't one. `performUseHeld` applies
damage **inline, on the input frame**:

```
engine/src/framework/character.cpp:440
    damage(victim, use->power);
```

`useCooldown` is a rate limiter — how soon you may swing again — not a damage timer. `damage()`
has exactly two call sites and both are synchronous; no deferral mechanism exists anywhere in the
framework.

So closing 14.3 is not a deletion, it is **introducing deferral**, and that is a bigger change
than the ruling assumed:

- somewhere to hold a pending strike per character — a `CharacterComponent` field or a
  `CharacterSystem` side table, both `character.h`, which is the Character-component seam;
- a behaviour change in `performUseHeld`, which also changes what `ActionResult::target` and
  `::amount` mean to the four areas that read them (they are filled synchronously today);
- and `tests/test_actions.cpp` / `test_gameplay.cpp` assert the current instant behaviour.

ADR 0017 Ruling 3 condition 3 is *"it does not extend"*, and you told me to stop and raise if I
found myself wanting `action.cpp` or `character.h`. I found myself wanting `character.h`. So I
stopped. The seam request is below.

**14.4 closed without needing any of that**, and the reason is worth recording because it is the
opposite of what I assumed when I first raised the seam: per-character animation state already
lives on the *game's* actor struct, next to its `LocomotionAnimator` (`device_game.cpp`'s `Actor`).
A `UsePlayer` belongs exactly there. No `CharacterComponent` field, no engine change.
`a_real_hit_mid_action_blends_the_action_out` proves it through a real `CharacterSystem::damage()`
mid-chop rather than a stand-in.

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
| **14.3** phase-addressable timeline | `101e6d5` | `[~]` — animation half done; **the other half's premise is wrong, see the top** |
| **14.4** interruption | `101e6d5` + this push | **done** — proven end to end through real `CharacterSystem::damage()` |
| **14.5** archetype data on `ItemUse` | this push | **done** — built as ADR 0017 ruled |
| **14.6** proof by catalog | this push | **done** — the six are real `ItemUse` data now, not a demo struct |
| **16.2** `anim_preview` on the real body | `6276768` | done |

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

### 1. The shoulder — **RULED, closed on my side** (ADR 0015 + ADR 0016)

Nothing needed from you. Recorded so the thread is followable: I raised it with three costed
options and no recommendation past where I would start; you ruled **reweight, no clavicle, no
skinning-definition change** until the reweighted body is re-measured, on the grounds that you
cannot diagnose "this rig needs another joint" from a body nobody weighted. That is a better
ruling than my framing invited — I had presented the missing clavicle as a co-equal cause when it
is only a hypothesis that a reweight will test for free.

The pipeline session is reweighting every bending joint against `B-31`, which is my per-edge
strain instrument promoted into the body contract as gate §9.8. **I am still blocked from
*looking* right and unblocked from *building*, which is the correct order.** I have not touched
the body, the rig or the weights.

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

### 3. SEAM REQUEST — the damage moment has to be deferred, and that is a behaviour change

*(This supersedes my earlier 14.5 request, which you ruled and I have now built. This is the half
of 14.3 that request could not reach.)*

```
SEAM:  Character component — `CharacterComponent` fields (AGENTS.md §4; Gameplay ⇄ People ⇄ Body)
       and the Phase 12 action model in `framework/character.cpp`.
NEED:  A blow should land when the motion says it lands. The animation side is ready and
       reachable from item data as of 14.5: `motionFromItemUse(use)` then
       `UsePlayer::strikeMoment()` gives the damage instant in seconds — 0.233 s for a
       dagger, 0.729 s for a maul, neither number tuned by anyone. Nothing consumes it,
       because damage is applied inline at character.cpp:440 on the input frame.
BREAKS: More than the phrase "delete the tuned delay" suggests, because there is no delay:
       * per-character pending-strike state — a `CharacterComponent` field or a
         `CharacterSystem` side table. Either is `character.h`.
       * `performUseHeld` stops resolving the hit synchronously, so `ActionResult::target`
         and `::amount` become empty at request time and arrive later. Four areas read
         `ActionResult`.
       * `tests/test_actions.cpp` and `test_gameplay.cpp` assert the instant behaviour and
         would need updating with it — deliberately, not incidentally.
       * AI attacks (`ai.cpp:219`) call `damage()` directly and would keep landing
         instantly unless they route through the same path, which would make a guard's
         swing and a player's swing behave differently — a P9 smell.
PROPOSAL: I have NOT built any of this. Two shapes, and the second is smaller than it looks:

       (a) Engine-side deferral. `CharacterComponent` gains a pending strike (entity,
           damage, seconds remaining), `performUseHeld` arms it instead of resolving, and
           `stepLocomotion`/`tickEffects` fires it. Correct and universal, and it fixes the
           AI path for free — but it is the seam, a behaviour change, and test updates.

       (b) Report the moment, let the caller schedule. `ItemUse` already carries everything
           needed, so gameplay could expose the strike time on `ActionResult` and leave the
           firing to whoever drives the frame. Cheaper, but it puts the timing in every
           game rather than in the engine, which reads to me like the wrong side of P8 —
           and it would let a game and its AI disagree about when a blow lands.

       I would build (a) if you assign it under the same Ruling 3, and I would want the AI
       path in scope, because leaving it out is how the player and NPCs stop being the same
       character (P9). But it is genuinely gameplay's call, not a formality — the
       instant-damage behaviour is theirs and has been since Phase 12.
```

### 3a. Two small things for other areas, neither blocking

- **`device_game.cpp`'s item catalogue takes the new defaults.** Sword, apple and torch are
  declared there (Platform's file) without archetypes, so all three default to `Swing` — the
  apple would swing. Harmless today because nothing drives a `UsePlayer` yet and held items do not
  render at all (14.7), but it wants `Consume` and `Raise` on two lines whenever Platform next
  touches that file. Not mine and not urgent; recorded so it is not discovered later.
- **There is no hit notification.** `CharacterSystem` has no callback, no listener and no
  damaged-this-tick flag, so a game notices it was hit by watching its own `health` — which is
  what my test does. It works, and polling is a defensible answer, but it is worth someone
  deciding rather than inheriting.

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

### 5. The ownership map still has no animation row

Narrower than when I first raised it, because ADR 0017's unowned-area rule is now in `AGENTS.md`
§3 and that was the part that actually mattered. What remains is cosmetic but load-bearing for the
next session: §2's roster has no animation area, and §3 still maps **all** of
`engine/*/character/**` — "(body, rig, variants, skinning, animation)" — to the character asset
pipeline, including the four files this session created.

Nothing has collided, because everything new is in files nobody else had and I have not touched
`humanoid.h`, `character.h`, `garment_fit.*` or the rig. But a fresh session reading that table
would conclude `character/animation.*` and `character/use_archetypes.*` are someone else's. One
row either way — or tell me the split reverts to the pipeline when that area next runs, and I will
stop mentioning it.

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
