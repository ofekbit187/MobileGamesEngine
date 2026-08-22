# Platform & device build

**Session:** none — the architect is holding this area directly
**Branch:** `claude/android-game-engine-design-blsmnw` (integration; no separate branch)
**State:** active — 19.1, 19.3, 19.4, 19.6 and 19.7 landed; 19.2 reclassified (textures')
**Updated:** 2026-08-22 by the architect — the damage moment lands on `strike` (ADR 0021)

## Now
**Phase 19 — the vertical slice.** Everything the character pillar promises works, in four
different places, and no single build contains all of it. Bring it together and **ship the owner
an APK**. That is the deliverable; captures are not.

Chartering a session for this failed three times (the session service was unavailable), so rather
than let the APK block on it, the architect is doing the work in-area. That is a departure from
the roster and it is written down here rather than left implicit: when a platform session does
come up, this file is its handover, and `app/` is its area, not the architect's.

## Last landed

**19.1 — held items on the device path.** Every character in every shipped APK to date has held
nothing. The sword in the early captures came from `buildHumanoidVisual` (the v1 box rig) as a
part bolted to a joint; when the body became a skinned mesh, the device build's outfit loop began
dropping held rows on the same `mesh.vertices.empty()` line the desktop path fixed last week.

Landed on the **allocation-free** route deliberately. `placeHeldItem` bakes the item's geometry
into character-local space per call, which is right for the offline previews and wrong here — it
would allocate on the frame path, and P1 outranks the convenience of reusing one function. The
device path instead uses the two pieces `held_items.h` exposes separately for exactly this reason:

    model x attachPointTransform(anchor, variant, jointWorld) x gripTransform(def)

one matrix per item per frame, no geometry touched. The joint transforms come from
`evaluatePose(buildSkeleton(variant), pose, ...)` — the same skeleton the skinning palette is
built from — so the grip lands on the hand *that* character has, not a template's. `Skeleton` is
a POD of fixed arrays, so building it per frame costs stack, not heap. The item mesh is uploaded
once per catalogue row into `Impl::heldMeshes` and shared by every actor holding one.

Verified on all three tiers: 227 tests / 0 failed on host and on arm64 under QEMU, both runners
printing `steady-state heap allocations: 0`, and `assembleDebug` producing a 5.9 MB APK.

**19.3 — the action button now moves the body.** Pressing use has fired a real action since
Phase 12, but the character never moved: it printed a line. The archetype library (14.2) has been
sitting complete and unused on this path. Now a successful use starts the motion the ITEM
declares, and the pose is composed exactly as 14.1 intends:

    layered.reset(walkPose); layered.addLayer(archetypeOverlay, archetypeMask, player.weight())

The mask comes from `useArchetypeMask`, so grip decides which arms participate rather than this
file guessing: a one-handed swing leaves the off arm to locomotion and the legs never stop
walking. The held item follows for free, because the sword's matrix is built from the same
composed pose the palette is.

Three item rows got their real archetypes at the same time. `ItemUse::archetype/reach/weight`
were decoration while nothing played them, so apple and torch both sat at the `Swing` default —
an apple would have been eaten with a swordsman's swing the moment the motion turned on.

**Evidence:** `evidence/platform/swing_with_sword.png` — eight frames, wind-up through recovery,
sword in hand throughout, legs striding. Produced by `mge_held_preview --swing`, which composes
the pose the *same* way the device build does and rasterizes it on the CPU. **It is real engine
output, but from the preview path, not from a phone** — `device_game.cpp` cannot run headlessly,
so the wiring itself is verified by compilation and by parity with this composition, not by a
device capture. Worth saying plainly rather than letting a strip imply a screenshot.

The first capture of that strip was a side view and appeared to show a sword that never moved.
It was the camera: a Swing travels ~0.7 m across the body in x, and a side view puts x down the
depth axis. Measured the hand trajectory before touching anything, which is the only reason the
archetype did not get blamed for a working motion.

**Not done in 19.3:** damage still resolves on the button press inside the action model rather
than on `strike`. The *message* is now held back to the strike instant so what the player reads
agrees with the arm, but that is presentation. Moving the damage itself is a `CharacterSystem`
change and belongs to gameplay.

## 19.6 — the P1 violation, fixed on the owner's ruling
`DeviceGame::frame` declared its `std::vector<DrawItem>` inside the function: the device render
path heap-allocated **every frame**. Hoisted to `Impl::drawItems`, `clear()`ed per frame, and
reserved once to the exact bound — `world.entities().capacity()` (the hard cap on renderables,
since `forEachRenderable` walks registry slots) plus the held items on the three actors, counted
after `dress()` has run. Exact rather than generous on purpose: a reservation that provably
covers the maximum makes "never grows" a property, where a padded guess only makes it likely.

Added with it: the device build's **first allocation check**. If the list ever outgrows its
reservation, the frame path has allocated, and it logs at error level once. Not a gate — see
below — but it turns the next occurrence of this exact bug from silent into loud.

Verified on three tiers, both runners still `steady-state heap allocations: 0`. Note honestly
what that does *not* prove: neither runner compiles this file. The fix is verified by
construction and by build, not by a device-path allocation counter, because none exists.

## 19.7 — the gate, closed on the owner's ruling
`host_runner` enforces the P1 gate and did not compile `device_game.cpp`. That is how a per-frame
allocation lived on the shipped path unnoticed, and it meant the zero we reported was a zero for
the engine core, not for the app on the phone.

The device's per-frame render composition now lives in
`engine/include/mge/framework/character_render.h` (+ `src/framework/character_render.cpp`), and
`host_runner` runs it inside the allocation counter every steady frame: three dressed characters,
one of them armed and swinging on a loop, plus 24 scenery renderables — **39 skinned rows and 25
props per frame**, on host and again on real arm64 instructions under QEMU.

**The constraint that dictated the design**, and the reason it is not simply "move the code":
`mge_core` builds for arm64 under QEMU with **no Vulkan at all** — the emulated tier is
deliberately graphics-free, and `host_runner` links only `mge_core` — yet that tier runs the gate
too. So nothing shared may name a GPU type, and `DrawItem` lives behind `<vulkan/vulkan.h>`. The
split follows:

- `composeCharacterFrame` is an ordinary function. Pose, palette and joint transforms involve no
  GPU types, so the bulk of the per-frame work is shared outright.
- draw emission (`emitBodyParts`, `emitGarments`, `emitHeldItems`, `emitWorldRenderables`) is a
  **template on the item type**. The device instantiates it on the renderer's real items; the
  runner on stand-ins with the same fields and an opaque mesh handle. Same source, both sides.
  Every GPU-typed decision in the static pass — which mesh, which material — is pushed into a
  caller-supplied resolver, which is the only part that cannot follow the rest into `mge_core`.

**The gate was proved to fail rather than assumed to.** Two probes, both reverted:

| Probe | Result |
|---|---|
| 19.6's exact bug — the vector declared inside the frame loop | **600 allocations** (one per steady frame), `FAIL`, exit 1 |
| a list merely under-reserved | **7 allocations** (the growth doublings), `FAIL`, exit 1 |

A gate that cannot be shown failing is decoration, and this one had every reason to be checked:
the whole reason we are here is a number that read as proof while proving less than it appeared.

Five tests in `tests/test_character_render.cpp` now cover invariants that were unreachable while
this code sat in the app: chiefly that a use motion **layers over** locomotion rather than
replacing it, so pressing the action button cannot freeze the walk.

**What the gate still does not cover**, stated plainly so the number keeps its meaning: the
Vulkan submission after composition (which allocates nothing on our side), the HUD/UI build, and
the swapchain and lifecycle paths. Those remain device-only.

## 19.4 — the practice yard
A quintain stands in the yard. The guard walks up to it, **draws**, swings, **connects**,
recovers, and after three blows steps back and comes in again. The player can walk over and do
the same thing to the same post with the same button.

Three decisions worth recording:

- **The quintain is a prop to look at and a character to hit.** `strikeTarget` only considers
  entities with a `CharacterComponent`, so a post that can be struck has to be one. It renders
  through the static draw path (it has a `ModelComponent`), never moves and decides nothing.
  Being a character is precisely what makes it hittable — the owner's "every character can be
  interacted with", arriving from the other side.
- **Scripted in the game layer, not the AI.** A scene is policy; the AI is mechanism. And
  concretely: `AiSystem`'s attack calls `CharacterSystem::damage` **directly**, so an AI-driven
  drill would show a guard dealing damage without ever drawing, without using the item he holds
  and without playing an archetype. The script goes through the ordinary character calls — the
  same ones the player's button takes — which is the only reason watching the guard tells you
  anything true about the game. *(That the AI bypasses the action model is a real gap, and it is
  gameplay's file, not this one's. Raised, not fixed.)*
- **It lives in `app/src/main/cpp/practice_yard.h`, not inline in `device_game.cpp`.** That file
  cannot be compiled by any host tool, and a scene verified by "it compiled" is how you find out
  on the owner's phone that the guard is swinging at thin air — a failure that looks *exactly*
  like success until you notice the post never reacts. The header is portable C++ over engine
  types, so `tests/test_practice_yard.cpp` drives it against a real `World` and
  `CharacterSystem`.

**The tests caught a real failure on their first run**, which is the argument for having written
them: two of the four failed because the harness ticked locomotion but not `tickEffects`, and
`tickEffects` is what counts the use cooldown down. The guard swung once and then stood there.
That was my harness rather than the scene, but it is an exact rehearsal of the bug the tests
exist to catch, and I would not have known the difference from a build log.

Four tests now assert he closes the distance before swinging, draws before he swings, **connects**
rather than merely swings, and that after a simulated minute the drill is still cycling and the
post is still standing.

The drill also joins the P1 gate, since it is device frame-path code: `host_runner` reports
`scripted scene: guard landed 6 blows on the quintain during the gate` on host and on arm64, at
zero steady-state allocations.

**Not delivered: `textured`.** The task asked for a dressed, *textured* character. The characters
are dressed and flat-coloured; the texture is blocked on 19.2's missing encoder. One word of the
task, and it is the one that is missing.

## 14.3 / ADR 0021 — the damage moment now lands on `strike`
The action model resolved a Strike **on the button press**: `performUseHeld` picked a victim and
applied damage in the same call that started the swing. Correct when nothing moved — Phase 12
predated the archetype library — and visibly wrong since 19.3, worst for heavy weapons, whose
longer wind-up put the damage furthest from the moment it looked like it should arrive.

**The animation session's warning was right and saved this from starting in the wrong place.**
14.3 and ADR 0017 both described *deleting a tuned delay*; that session measured and found there
was none — damage was applied inline, `useCooldown` is a rate limiter, and no deferral existed
anywhere. So the task was to *introduce* one. They also judged ADR 0017's grant did not stretch
this far and raised it instead of taking it. The roster working as intended.

**The obstacle, and the ruling.** The strike moment is a property of the motion timeline in
`character/use_archetypes.h`, and `character/` includes `framework/`, never the reverse (ADR 0017
measured that too). Recomputing the timing in `framework/` would have given two copies of the
formula and a silent desynchronisation on the first change to either. Moving `usePhases` down
would have dragged the per-archetype timing table — *how a swing feels* — out of animation's
hands. So: **injection**, the pattern `CharacterSystem` already uses for the item registry,
collision and interactions. Gameplay declares `StrikeDelayFn`; the character pillar supplies
`strikeDelaySeconds`; the game wires them. Written up as ADR 0021 before any code moved.

With no timing installed, damage resolves immediately exactly as before — which is why the full
suite passed unchanged the moment the mechanism landed and before anything was wired to it. That
is the compatibility property doing its job, not luck.

Two consequences worth stating because they are behaviour changes, not refactors:

- **The target is chosen at the strike, not the press.** A victim who steps out of reach during
  the wind-up is missed; one who steps into it is hit. Under the old model both were decided
  before the arm moved.
- **`perform` no longer knows who was hit.** `ActionResult::pending` says so, so a caller cannot
  read "not yet" as "missed" — they mean opposite things. Outcomes are polled through
  `consumeStrike`, drained once per frame by the game, since the queue is shared between the
  player's swings and the guard's drill.

This also deleted the hand-rolled message delay 19.3 put in `device_game.cpp`. That existed only
to hold back a message about damage that had *already* been applied; now the damage is late too,
so there is nothing left to fake.

Six tests in `tests/test_actions.cpp` cover it, including the one that would have failed before
(`a_strike_does_not_land_on_the_button_press`) and the compatibility case
(`without_a_strike_timing_installed_damage_still_resolves_at_once`).

**Loose end, named rather than hidden:** `cancelStrike` exists and is tested, but nothing calls
it — nothing today interrupts a player's swing. When something does, the side that interrupts the
motion is what must cancel the blow.

## Needs from the architect
Nothing blocking. Open question for whoever takes this area: the P1 gate should cover the device
path, not just the core. That is a real gap in the verification story and larger than 19.6's
two-line fix.

## A note for the wearables session — I edited your tool
`tools/held_preview/main.cpp` gained a `--swing` mode. It is additive: no existing behaviour or
output changed, and the default path is untouched. I put it there rather than in a new tool
because what needed proving is *held item and swing together*, which is exactly that tool's
subject — "is the sword in the hand?", now through a motion instead of a bind pose. Say the word
and I will move it.

## Next
Phase 19 is done except 19.2, which sits with the textures area and needs the ASTC/ETC2 encoder
before a face can reach the phone. One thing still owed from this phase: **`AiSystem::stepAgent`
attacks by calling `damage()` directly** — it never draws, never uses the item it holds, never
plays an archetype, and now also bypasses the strike timing every other path goes through. That
gap is wider after ADR 0021 than before it. Gameplay's file; raised, not taken.

**19.2 is bigger than it looks, and worth knowing before someone picks it up.** No texture asset
ships in this repo at all. The imported sheet exists only as an evidence PNG, and the source it
came from is deliberately uncommitted. Worse, `bakeTexture` emits `Rgba8` and nothing else — no
ASTC, no ETC2 encoder exists yet. So a textured character on device today means shipping the
uncompressed pack. That is *legal* — `TexturePack::Uncompressed` is the documented verification
and fallback pack — but a 1024-square sheet with mips is ~5.6 MB against a 5.9 MB APK, and
TEXTURING §7's "a shipped texture is already block-compressed" is written for exactly this
temptation. **The real 19.2 is the ASTC/ETC2 encoder, and that is the textures area's, not a
device-build task.** Ruling it that way rather than sneaking an uncompressed sheet into the APK
to make a demo look better.
