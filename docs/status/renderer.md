# Renderer

**Session:** session_011sZSvTYWLnkb7R8wYFHGGV
**Branch:** `claude/renderer-gpu-morphs`
**State:** ready
**Updated:** 2026-08-21 by the renderer session

## Done — a character can sample a texture (`c872c15`)

The blocking job is closed. Built as the ADR 0014 amendment ruled: `SkinnedDrawItem` gains
`surface`, `skinned.vert` outputs the UV it was already reading, `recordSkinnedItems` binds the
item's material at set 2 instead of always binding the default. Same `lit.frag`, same 1×1-white
default, one pipeline for textured and untextured — no new permutation.

**Evidence — ● real output, llvmpipe, `mge_skin_test`:**

```
CPU reference vs GPU skinning (15 morph weights on): 0.000 / 255
TEXTURED CPU vs GPU:                                 0.000 / 255
chromaticity spread across the body: 127.7 textured vs 7.3 flat-shaded (x1000)
```

Captures: `build/skin_textured_gpu.ppm`, `build/skin_textured_cpu.ppm`.

The second number is what makes the first mean anything, and it is worth one sentence because
the obvious test would have passed while broken: **a body sampling one texel for its whole
surface agrees with itself perfectly**, so 0.000 alone proves nothing. Chromaticity —
R/(R+G+B) — divides the lighting out, which is what separates "the chart reached the fragment
stage" from "the body is lit". Brightness cannot do that: a lit untextured body varies in
brightness *more* than a textured one, so the first metric I tried scored the broken case higher.

Morphs need no term in the UV: a delta displaces the vertex in bind space and the texel it wears
travels with it — which is also why a re-shaped face keeps its features.

All three tiers green, arm64 under QEMU 226 tests 0 failed, heap gate 0.

## Needs from the architect

### 1. The palette change will fail my build on purpose — here is the whole fix

Not a collision, and no sequencing needed: **land ADR 0019 whenever you like, and the pipeline
session does not need to coordinate with me.** But it will hit a deliberate stop, so it should not
be a surprise.

GLSL cannot import `kJointCount`, so `skinned.vert` hardcodes it — and the morph weights sit
immediately *after* the palette in the same uniform slot. A rig that grew to 19 would leave the
shader reading its weights out of the last joint's matrix, with **no error anywhere**: no
validation failure, no crash, just a character whose face quietly stops matching its variant. That
is the class of bug the skinning seam exists to prevent, so I made it a build failure instead.

Three edits, and nothing else in the renderer needs touching — the C++ slot size, stride and
descriptor range all derive from `kJointCount` already:

1. `engine/shaders/skinned.vert` — `const int kJointCount = 17;` → `19`
2. re-run `scripts/compile-shaders.sh` (the generated header is committed)
3. `engine/src/graphics/renderer.cpp` — the `static_assert(kJointCount == 17, ...)` → `19`

The assert's message says this too, so whoever hits it does not need to find this file. If you
would rather I make the change myself once the rig lands, say so in my `Now` and I will.

### 2. `mge_skin_test`'s dressed comparison is at 0.948 against a 1.000 threshold

Not mine and not new work — I measured it with my changes stashed and got the same 0.948, so it
arrived with the re-imported body (LOD0 is now 2097 vertices / 2388 triangles and 7 body ranges,
up from 1640 / 2200 / 6). Flagging it because it is **5% from failing**, and when it does fail it
will look like a renderer regression while actually being a masking-seam discrepancy: the CPU
reference rebuilds the body from `buildTemplateBody` with a region set, the GPU draws index
sub-ranges of the full shared mesh, and the two disagree on a few edge pixels at region
boundaries. Growing the region count grows the disagreement.

I have not touched it, because closing it means changing how masking is expressed on one side of
the Body ⇄ Renderer skinned-draw seam, which is your ruling rather than my edit.

```
SEAM: Skinned draw — how region masking is expressed on each side
NEED: A decision on whether the dressed comparison should be brought back to ~0, and which side
      moves. It is drifting toward a false failure that will cost someone a debugging session.
BREAKS: Either the CPU reference stops rebuilding a masked mesh and draws ranges like the GPU
      does, or the GPU builds masked meshes like the CPU does. The first is cheaper and matches
      what the device actually does; the second is closer to the current reference semantics.
PROPOSAL: Rule the CPU reference onto index ranges over the shared mesh, so both sides mask the
      same way and the comparison measures skinning rather than masking. If you would rather
      just raise the threshold, that is your call — but it hides a real difference, so I would
      rather not do it silently.
```

## Then — standing backlog

Frame-graph structure, alpha-tested and unlit materials, `.mgemesh` v2 vertex quantization,
generated LOD chains, per-LOD byte ranges — in that order unless measurements argue otherwise.

Two items from my own earlier work that are still open and that I would put ahead of that list if
you agree: **texture eviction** (task 2.3 — nothing drops a resident texture under pressure, so
the residency half of streaming-by-mip does not exist) and the **ASTC/ETC2 encoder** (the
container carries both shipping packs and nothing writes them, so "every shipped texture is
block-compressed" is not yet true). The second is arguably the textures session's; I flagged the
ownership question last time and it has not been ruled.

## Last landed

- `c872c15` — the skinned path samples textures.
- Texture runtime: sampling, upload, budgeting, formats, `.mgetex`, `.mgemesh` v2.
- GPU morphs: shape on the GPU in the CPU reference's order.
