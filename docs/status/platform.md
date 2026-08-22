# Platform & device build

**Session:** none — the architect is holding this area directly
**Branch:** `claude/android-game-engine-design-blsmnw` (integration; no separate branch)
**State:** active — 19.1 landed, 19.2–19.4 not started
**Updated:** 2026-08-22 by the architect

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

## Found while landing it — not fixed, and not mine to fold in
**Task 19.6, a live P1 violation on the shipped path.** `DeviceGame::frame` declares its
`std::vector<DrawItem> items` *inside* the function, so the device render path heap-allocates
every single frame. This predates 19.1 and is untouched by it. It has stood this long because the
host runner — the thing that enforces the P1 gate — does not compile `device_game.cpp`, so the
gate that would have caught it has never looked at this file. **The zero we report is a zero for
the engine core, not for the device build.** The fix is two lines (hoist to `Impl`, `clear()` per
frame so capacity persists after the first). It is left undone on purpose: it is unrelated to the
task in hand, and quietly widening a commit is how a P1 fix ends up unreviewed.

## Needs from the architect
Nothing blocking. Open question for whoever takes this area: the P1 gate should cover the device
path, not just the core. That is a real gap in the verification story and larger than 19.6's
two-line fix.

## Next
19.2 (skin texture on device characters), 19.3 (use archetypes on the action button), 19.4 (one
scripted scene). 19.5 — the APK itself — ships to the owner now, with 19.1 in it and 19.2–19.4
honestly absent.
