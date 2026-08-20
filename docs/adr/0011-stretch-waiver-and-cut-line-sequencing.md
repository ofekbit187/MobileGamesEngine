# ADR 0011 — Inherited unwrap stretch, and paying for one re-bake instead of two

**Status:** accepted (architect ruling) · **Date:** 2026-08-20
**Depends on:** ADR 0007, ADR 0008, ADR 0010, `docs/TEXTURING.md`, `assets/standards/skin_texture.mgestd`
**Evidence:** `docs/research/uv-repack.md` · **Raised by:** the character asset pipeline session, as a seam request

## Context

The repack ruled in ADR 0010 has landed (`943e072`; body content hash `e6eae4e58a7ec271`).
It did what it was asked to: triangles with no UV area went 89.7% → 0%, vertices pinned to
a tile edge 91.1% → 0%, regions with texture space 3/12 → 11/12, density spread 3.6× → 0%.
Arms, hands, legs and feet had no texture space at all before it.

Two rules in `mge_uv_report --gate` still refuse, and the pipeline session flagged rather
than answered both. That was the correct instinct in both cases and this ADR rules on them.

## Ruling 1 — `stretch_above_max`: the standard does not move, and the decision is not due yet

Eight of eleven measurable regions stretch between 1.40× and 1.97× against a 1.50× rule.
The pipeline session established the important fact: **this is the CC0 source's own authored
unwrap distortion, not the repack's.** A per-region uniform scale cannot change the ratio of
UV area to surface area *inside* a region. The repack did not cause it; it made it
*measurable*, because before it there was nothing to measure the stretch of.

It offered three options and recommended relaxing `stretch_max` to 2.0, or letting the rule
fail until 13.7.

**Ruled: `stretch_max` stays at 1.50. The rule keeps refusing. Nothing is built for this
now. The decision comes due at the completion of task 13.7 and not before.**

**Why not relax the rule.** A standard that moves to fit the content it happens to be
pointed at is not a standard. Relaxing `stretch_max` to 2.0 globally would silently admit
every future body that stretches 1.99×, and would do it invisibly, because the number in the
file would look deliberate to whoever read it next. 1.50× is what we commission new content
against, and the imported body's failure to meet it is a *fact about the imported body* that
should read as one.

**Why not build a waiver mechanism today.** The tempting middle answer is a per-asset waiver
so the gate can go green with the debt recorded. It is premature: `chart_regions_required`
(Face) refuses anyway until 13.7 lands, so the gate is red either way and a waiver buys
literally nothing between now and then. Building a mechanism whose only effect is invisible
is the opposite of P12 — that principle says pay once in the mechanism *so content gets
cheaper*, not pay for mechanism on principle.

**Why the decision genuinely becomes due at 13.7 and not later.** Two things change then.
Face stops refusing, so stretch becomes the *lone* remaining refusal — and a gate that is
permanently red for one known reason is a gate people stop reading, which is the silent-clamp
failure wearing different clothes. And 13.7 re-topologises the head, which may move the head
region's stretch reading on its own. Deciding now would be deciding without that number.

**What the answer will be shaped like, so nobody is surprised.** If stretch is still the lone
refusal after 13.7, it lands as a **per-asset waiver expressed as data** in
`assets/standards/skin_texture.mgestd` — one line naming the asset, the rule, the measured
value, the reason, and the condition that retires it — surfacing in the gate as a third
verdict (`WAIVED`), never as `pass`. The standard's own design brags that one line of data
tightens a rule; the same must hold for loosening one, or the brag is false.

**On commissioning a re-unwrap** (the session's option (c) — sourced, not originated, and
therefore inside its charter): **not rejected, but it never travels alone.** A re-unwrap
invalidates every UV, which is a contract-version event requiring a full garment re-bake. We
have just paid for one. If a re-unwrap is ever commissioned it rides an *existing*
contract-version event rather than causing its own — the same rule Ruling 2 applies below.

Worth stating plainly: 1.97× in the worst region (Hands) means a painted circle reads as a
2:1 ellipse there, on the smallest thing on screen in a third-person game. This is a real
defect and it is also nowhere near the top of the list.

## Ruling 2 — the `cut_shell` tie-break bug folds into task 13.6

The session found that `cut_shell` in `tools/model/humanoid_template.py` breaks region ties
by lowest region *name* — its comment claims the importer does the same, and the importer
in fact breaks ties by `BodyRegion` enum index. So garment cut boundaries can disagree with
the regions the engine masks. That is a hole waiting to happen, and the same bug in the
packer is what cost three failed implementations before it was caught by measurement.

It is in the pipeline's own file, so it could have been fixed in this push. It correctly
was not: fixing it alters every garment's cut geometry, which is a contract-version event,
and burying one inside a chart repack is how a change becomes invisible.

**Ruled: fix it as part of task 13.6, not before and not separately.** 13.6 (the
`BodyRegion` extension for elbow/knee cut lines and the ear sub-shell, ADR 0010's
assignment) is *already* a cut-geometry change and *already* a contract-version event. Two
separate cut-geometry events cost two full garment re-bakes and two re-verifications to buy
exactly what one buys. Pay once.

Until then the disagreement is latent and not shipping damage: the current cut geometry and
the current masking were both baked from the same run, so they agree with each other. The
bug bites the next time one is regenerated without the other — which is precisely 13.6.

## Ruling 3 — the re-bake of `.mgefit` was in scope

The pipeline session ran `mge_garment_fit` to re-bake bindings against the new body,
touching `assets/models/*.mgefit`, which are outputs of the wearables session's tool. This
is not a seam violation and needs no request: it *ran* the tool, it did not *edit* it, and
ADR 0008 requires the trio, the cuts and the bindings to land as one atomic push. A body
landing with knowingly-stale bindings to respect a file boundary would be the letter of
§3 defeating its purpose.

Recorded so the boundary is clear both ways: **running another area's tool as its documented
contract requires is always in scope; changing what the tool does never is.**

## Consequences

- The pipeline session proceeds to 13.7 (`Face` shell) immediately; nothing blocks it.
- `mge_uv_report --gate` stays `REFUSED` until 13.7, expected and recorded, on two named
  rules and no others.
- No skin texture is authored against this chart yet — unchanged from before the repack,
  but now for two specific reasons instead of a wholly unusable chart.
- 13.6 grows the `cut_shell` tie-break fix, and its re-bake covers both changes.
- The stretch decision has a named trigger. If 13.7 lands and nobody re-opens it, that is a
  process failure, not a decision.

## Also recorded — how this landed, because the method is the point

ADR 0010 quoted 607 px/m for the disjoint layout; the shipped chart measures 505 px/m. The
difference is not a regression and not an error in either measurement: the predecessor's
pack put every island in one undifferentiated pile, which is denser and **fails
`chart_islands_disjoint` outright** — 50 region pairs sharing texels, measured. Giving each
region its own box is what `B-27`/`B-28` require and it costs that headroom. 505 px/m is
1.4% under the 512 px/m standard at 1024², with density now perfectly even across regions
where it previously varied 3.6× across the three regions that had any texels at all. If the
target is ever to be met outright the lever is sheet size, not layout.

Three implementations of the repack passed inspection and failed measurement: collapsing all
24 tiles onto one stacks the mirrored halves and arrives at the layout ADR 0010 *rejects*, by
accident; packing the base mesh while the gate measures LOD0 disagrees at region seams; and
the enum-index tie-break above. Every one was caught because disjointness was **rasterised
and measured** (100.0% shared for a mirrored pack, 0.0% for this one) rather than asserted.

This is the second time in two jobs that measuring first overturned a conclusion that looked
obvious — the first being my own corruption hypothesis, disproved in ADR 0010. It is the
working method of this area and it is why the area exists in its current form.
