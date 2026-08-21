# Renderer

**Session:** session_011sZSvTYWLnkb7R8wYFHGGV
**Branch:** `claude/renderer-gpu-morphs`
**State:** idle — **chartered by the architect, awaiting pickup**
**Updated:** 2026-08-21 by the architect

## Now — your job, and it is blocking two other areas

**The GPU skinned path cannot sample a texture at all.** Raised by the textures session and
confirmed: `SkinnedDrawItem` carries no material, and `skinned.vert` reads `inUv` at location 2
and never outputs it.

**Ruled (ADR 0014 amendment): build it as proposed** — mirror what `DrawItem` already does. Same
set 2, same `lit.frag`, same 1×1-white default so one pipeline serves textured and untextured.
Prove it on the seam the usual way: `mge_skin_test`.

**Why it is urgent rather than merely open.** Every character in this engine is drawn through the
skinned path. The owner ruled skin is *imported*; the textures session has located a CC0
photographic human skin with a real face; the body's UV chart went green this morning after a
repack and a face split. **All of that is aimed at a path that physically cannot display its
output.** And ADR 0012 waived the face's UV stretch *provisionally*, with the owner's own eye on
the first face texture as the retirement condition — a decision he is personally holding open,
which cannot be met until a character can show a texture.

Two things already landed in your favour: `skinMesh()` now writes `Vertex::uv` (task 16.5), and
the texture runtime this hangs off is your own earlier work. You are finishing what you built.

## Coordination — read before touching the shader

**The skinning palette is about to go from 17 to 19 joints.** ADR 0019 rules that the rig gains
`ClavicleL`/`ClavicleR`: the shoulder tears under *any* weighting, and the pipeline session proved
no weighting can exist — strain is scale-invariant in the influence band width. That is a
rig-version event landing in its next push, and **it resizes the skinning shader's palette, which
is your file.**

The pipeline session holds a recorded grant to make that palette change under ADR 0017's
unowned-area rule, because you were idle when I ruled it. **You are not idle now.** Read
`docs/status/character-asset-pipeline.md` when you merge; put anything you need in `Needs:` below;
if the two changes collide, say so and I will sequence them. Do not assume the shader you see is
the shader that will ship.

## Then — your standing backlog
Frame-graph structure, alpha-tested and unlit materials, `.mgemesh` v2 vertex quantization,
generated LOD chains, per-LOD byte ranges. That order unless your own measurements argue
otherwise — and if they do, say so with numbers.

## Needs from the architect
Nothing. The ruling above is complete; the only open item is sequencing against the palette
change, and that is a `Needs:` entry for you to raise if it bites.

## Last landed
GPU morphs, and the texture runtime — sampling, upload, budgeting, formats on device.
