# ADR 0012 — The face's stretch, the LOD0 budget, and a gate that measures the wrong thing

**Status:** accepted (architect ruling) · **Date:** 2026-08-21
**Depends on:** ADR 0007, ADR 0008, ADR 0010, ADR 0011, `docs/BODY_CONTRACT.md`, `docs/MODELING.md`
**Evidence:** `docs/research/face-shell.md` · **Raised by:** the character asset pipeline session

## Context

Task 13.7 landed (`8dd2403`; body content hash `c4f91ac3fa2fcff5`). `BodyRegion::Face` is real
geometry on all three LODs, twelve of twelve regions own geometry, and the `B-8` debt ADR 0008
logged against v3 is closed. `chart_regions_required` passes.

That fires ADR 0011's named trigger: **`stretch_above_max` is now the lone refusal**, which is
the state ADR 0011 said must not persist. Three decisions come due together.

## Ruling 1 — the waiver, and the one region I will not decide by number

ADR 0011 predicted the reading would move because 13.7 re-topologises the head. **It moved, and
that reasoning was wrong.** 13.7 does not re-topologise anything; it classifies existing geometry
into two regions. The reading moved because splitting the head stopped *averaging* a low-stretch
cranium together with a high-stretch face:

| | before 13.7 | after 13.7 |
|---|---|---|
| Scalp (whole head → cranium only) | 1.69 | **1.36** |
| Face | did not exist | **2.09** |
| worst region on the body | Hands 1.97 | **Face 2.09** |

The body did not get worse. The measurement got honest, and what it now says is that the worst
unwrap distortion on this body is **on the face** — the one surface a player looks at, and the
surface every part of Dictation 5 depends on. Unique people, hereditary features carried in DNA,
faces that read as individual rather than as slider positions: all of it is painted there.

**Ruled, in two parts, because the two halves are not the same question:**

**(a) The seven non-face regions over the rule are waived outright.** Hands, arms, legs, feet and
the rest are inherited from the CC0 source's authored unwrap, cannot be improved by packing, and
sit on small or rarely-scrutinised surfaces. The waiver takes the form ADR 0011 designed: a
**per-asset data line** in `assets/standards/skin_texture.mgestd` naming asset, rule, measured
value, reason and retirement condition, surfacing in the gate as a third verdict `WAIVED` —
never as `pass`. `stretch_max` itself stays at 1.50; the standard does not move to fit the
content pointed at it.

**(b) The Face is waived provisionally, and its retirement condition is the owner's eye, not a
threshold.** I am not ruling that 2.09× on a face is acceptable, because that is an aesthetic
judgment and the owner is the aesthetic gate by his own dictation (`AGENTS.md` §6.2 — "never
this session's judgment, never a number"). What I am ruling is that it does not block authoring
now: **the first skin texture authored on the face is the test.** If the distortion reads to the
owner's eye, a **face-only re-unwrap** — 301 triangles at LOD0, a small and well-scoped piece of
sourced content, in charter under §6.2 — rides the next contract-version event, per ADR 0011's
rule that a re-unwrap never travels alone. The pipeline session proposed setting the retirement
condition on the Face specifically; that is the right instinct and this is it, sharpened.

**Consequence: a skin texture may now be authored.** That was blocked outright before the repack
and blocked on Face before 13.7.

**Implementation, with a recorded seam exception.** The waiver mechanism touches
`assets/standards/skin_texture.mgestd` and `tools/uv_report` — textures-area files, not the
pipeline's. No textures session is live, the change is small and fully specified above, and it
gates the pipeline session's own work. **I grant the character asset pipeline session an explicit,
recorded exception to build it.** It is an exception, not a precedent, and it is written here
rather than taken silently precisely so it does not become one.

## Ruling 2 — raise the LOD0 triangle cap to 2 400; LOD1 and LOD2 do not move

`B-9` requires the hairline to be an **authored loop, not an accident**. It is not delivered: the
boundary is classified per face, so it steps one face wide. The session implemented the cut and
measured it rather than arguing it — +74 triangles for the hairline, +128 for the ear plane —
against a LOD0 sitting exactly at `B-5`'s 2 200 cap, and correctly refused to buy them by
decimating.

**Ruled: `B-5`'s LOD0 cap becomes ≤ 2 400. LOD1 (≤ 1 300) and LOD2 (≤ 650) are unchanged.**

The reasoning is that the cap and the need are sized against different things.
`MODELING.md` §2 states plainly that these budgets sit below published mobile guidance
"because this engine draws crowds in an open streamed world". **But the crowd does not draw
LOD0.** A crowd is LOD1 and LOD2 — the body ships 1 200 and 560 against caps of 1 300 and 650,
and those numbers are what the working set is made of. LOD0 is the near mesh, of which few are
on screen at once. Raising it by 200 triangles leaves the budget that P1 actually cares about
**exactly where it was**, and buys a boundary that ADR 0008's whole argument says is cheap now
and impossible later: this is the WoW-hair lesson, on the specific edge where hair, helmets and
the bald fallback all meet.

Holding LOD1/LOD2 fixed is the load-bearing half of this ruling, not a detail. A cap raised at
every level would be exactly the standard-moves-to-fit-the-content failure Ruling 1 refuses.

`B-5` and `MODELING.md` §2 are updated to match. This is a contract-version event when the loop
lands: trio, cuts and bindings atomic, hash announced, as ever.

## Ruling 3 — `body_mesh_has_human_proportions` measures the wrong thing and gets fixed with it

The session found, while measuring Ruling 2, that this gate **passes by a single vertex**. It
samples the Torso region's width in a 60 mm band at 0.82–0.88 m, and across decimation targets:

```
target 2200: hips 0.3468 m   <- passes (rule: 0.30 < hips < 0.44)
target 2100: hips 0.1457 m   <- fails
target 1990: hips 0.1457 m   <- fails
```

The collapse is **discrete**. The hips do not change width; below 2 200 the Torso region simply
stops reaching into the sampling band, and the gate then measures a narrower part of the body and
reports it as a proportion failure. A gate that fails for a reason unrelated to what it claims to
check is worse than no gate — it trains people to route around it, which is the silent-clamp
pathology in yet another costume.

**Ruled: fix it in the same change as Ruling 2**, which will otherwise re-trigger it immediately.
Measure the torso's actual width extent over its own vertices rather than sampling a fixed
world-space band, so the number tracks the body's proportions instead of the decimator's choices.
Left untouched, deliberately, until now — the session was right to flag rather than quietly widen
the band, which would have hidden the defect instead of fixing it.

## Consequences

- Skin texture authoring is unblocked; `mge_uv_report --gate` can reach green with one `WAIVED`.
- The Face carries a live, named question for the owner, attached to the first face texture.
- The hairline loop (`B-9`) is fundable and becomes the pipeline session's work after 13.8/13.9.
- The crowd memory budget is untouched, which is the only reading of P1 that matters here.

## Also recorded

The morph-target bug 13.7 exposed deserves a line, because it was invisible and it was shipping:
glTF hangs morph targets off each *primitive*, and the importer emitted one `MorphTarget` per
(primitive, parameter). A body split by material turned 15 shape parameters into 25 targets, with
`face.jawWidth` appearing twice and `mesh.morph()` returning whichever came first — **half the
jaw would have moved.** Targets are merged across primitives now, and the single-primitive path
is byte-identical, so the previously shipped body re-imports to the same 1 916 vertices, 15
targets and 4 282 deltas it always did.

It was found only because 13.7 split the body by material for the first time. That is the second
time in this area that doing the work correctly surfaced a defect that inspection had walked past.
