# Platform & device build

**Session:** none — the architect is holding this area directly
**Branch:** `claude/android-game-engine-design-blsmnw` (integration; no separate branch)
**State:** active — 19.1, 19.3 and 19.6 landed; 19.2 reclassified, 19.4 and 19.7 open
**Updated:** 2026-08-22 by the architect — P1 fix on the device path, on the owner's ruling

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

## The real finding, still open (19.7)

`host_runner` enforces the P1 gate and **does not compile `device_game.cpp`**. That is how a
per-frame allocation lived on the shipped path unnoticed: the gate that would have caught it has
never once looked at the file. **The zero we report is a zero for the engine core, not for the
app on the phone.**

19.6's in-file check covers one vector on a real device. It is not a gate and should not be
mistaken for one. Closing this properly means the device path's scene composition moving into
engine code the host runner actually exercises — a refactor and a seam question, not a patch. It
is raised as 19.7 for the owner rather than started on my own initiative.

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
19.2 (skin texture on device characters) and 19.4 (one scripted scene). 19.5 — the APK — ships to
the owner now, with 19.1 and the visual half of 19.3 in it, and the rest honestly absent.

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
