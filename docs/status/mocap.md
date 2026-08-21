# Motion capture

**Session:** session_01XF3PqLgmSojSaKWKFtUmyF
**Branch:** `claude/mocap-from-video`
**State:** working
**Updated:** 2026-08-21 by the mocap session

## Now
**Task 18.1** — measuring the shipped procedural walk against the tracked human reference.

Read in: `AGENTS.md` (§4.5 ledger, §3 ownership), `PRINCIPLES.md`, `CHARACTERS.md` §6.2–6.3,
ADR 0018, ADR 0020, `TASKS.md` Phase 18. Branch cut from integration at `a9235ea`.

Method for 18.1, so it is a measurement and not an opinion:

- the reference side comes from `assets/mocap/walk_reference_tracking.json.gz` (436 frames,
  three views, 60 fps);
- the engine side comes from **the engine's own `LocomotionAnimator`**, driven by a host tool
  and dumped per frame — not a Python re-implementation of it, which could silently diverge
  from what the game actually plays;
- both sides go through the *same* metric code, so a difference is a difference in the motion
  and not in how the two were measured.

## Needs from the architect
Nothing yet.

## Blocked on
Nothing.

## Environment note (not a blocker yet)
This container has no Android SDK/NDK and no `qemu-user`, so `scripts/verify.sh` currently
reports tiers 2 and 3 as SKIPPED rather than green. I will provision via
`scripts/setup-android-sdk.sh` before I mark anything `ready`, and will say plainly in this
file which tiers actually ran rather than claiming three.

## Last landed
Nothing yet. Prior work by the architect: tracking of the owner's walk reference
(436/436 frames, three views, zero misses) in `assets/mocap/`, and `tools/mocap/track_video.py`.
