# Animation

**Session:** session_01TiRzQ9qPRKyvVbtW9akJLV
**Branch:** `claude/animation-layered-poses`
**State:** working
**Updated:** 2026-08-21 — session started, docs read, baseline measured

## Now
**Job 1: task 14.1 — layered poses with masks.** Building.

**Design, before code (so the architect can object early).** Layering composes in **pose
space** — local joint rotations — *before* `evaluatePose`. Base pose (locomotion) and overlay
layers are blended per joint by a `JointMask`, producing **one** `Pose`, which then goes down
the existing path unchanged. This satisfies the three hard constraints by construction rather
than by care:

- **One pose evaluation, one palette upload, one skin pass** — because layering finishes
  before `evaluatePose` is called at all. Nothing downstream of the composer can tell a
  layered character from an unlayered one.
- **Allocation-free** — `Pose` is 17 quaternions by value and `JointMask` is 17 floats by
  value; the layer stack is a fixed-capacity member array that refuses past its cap and
  returns `false` rather than growing. No heap on the frame path.
- **No rig change, so no seam request.** Layering rides entirely on top of the existing
  `Joint` enum, `Skeleton`, bind offsets and the 17-joint palette. I do not need to add or
  rename a joint, and I am not touching `humanoid.h`'s rig, `character.h`, or
  `garment_fit.*`. New code lands in files this area owns:
  `engine/{include/mge,src}/character/animation.*` and `tests/test_animation.cpp`.

## Needs from the architect
Nothing blocking. Two things flagged for visibility, neither of which stops me:

1. **One line in the root `CMakeLists.txt`** to register a new capture tool
   (`tools/anim_preview`, which is mine under AGENTS.md §3 "`tools/<demo>/**` — whoever the
   demo demonstrates"). The tool directory is unambiguously mine; the one-line
   `add_subdirectory` registration is the only thing outside it. Flagging rather than
   asking, since without it the tool cannot build. Say the word if you want it elsewhere.
2. **`Quat` has no `slerp`/`nlerp`** in `engine/include/mge/core/math.h`. Rather than extend
   a shared core header, I am keeping the blend helper private to the animation module. If
   you would prefer it promoted to `math.h` later, that is your call, not mine to take.

## Last landed
Nothing yet.

## Baseline measured this session (● real output, host, llvmpipe)
Integration `e657131` builds clean and is green before I touch it:

```
100% tests passed, 0 tests failed out of 2
steady-state: 600 frames in 4.10 ms (6.8 us/frame)
steady-state heap allocations: 0  (target: 0)
```

Note for whoever runs this environment next: the container had no Vulkan, so every GPU demo
was being skipped. `apt-get install libvulkan-dev mesa-vulkan-drivers` brings up llvmpipe and
`mge_vk_smoke` then reports `pixels verified: 65536, wrong: 0`. Captures are therefore
possible here, which is what makes a visual proof of 14.1 possible.
