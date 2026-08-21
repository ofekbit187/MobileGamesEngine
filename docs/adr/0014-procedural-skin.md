# ADR 0014 — Skin is sourced, not painted and not originated

**Status:** accepted, then **reversed the same day by the owner** — read the reversal at the end first (2026-08-21) · **Date:** 2026-08-21
**Depends on:** ADR 0009 (variation scope), ADR 0010–0013 (the chart this paints on), `docs/TEXTURING.md`
**Supersedes:** the open "textures as generators" proposal in TASKS.md Phase 15

## The dictation

> *"ok procedural texture it is"* — the owner, ruling on the fork raised when the UV gate went
> green: rule on procedural textures, commission a human artist, or park the face question.

**Accepted. Skin is produced by a generator, not by a brush.**

## Why this is the right answer, and not merely the available one

The fork was raised as a staffing problem — there is no artist, a skin must match *our* chart so
it cannot be downloaded, and a session must never paint by eye because a session cannot see what
it is making (the lesson that cost this project a body and a retired agent, `AGENTS.md` §6.2).

That framing undersells it. **Painted skin cannot satisfy Dictation 5 at all.**

The owner dictated that every person is unique, with hereditary features carried in their DNA.
A painted skin is *one* skin: paint ten and you have ten, worn by thousands. The engine already
has a `Genome` producing a phenotype (ADR 0009, task 9.4) and nothing downstream of it to vary
a face's *surface* — only its shape. A generator that takes the phenotype as input is the only
construction under which a crowd of hereditarily-related, individually-distinct people is
representable at all.

So this is not a workaround. **It is the same pattern this engine already uses everywhere else** —
virtual models from descriptions (P5), families from culture packs, wearables from archetypes —
applied to the one surface that had been left waiting for a human. P12 is exactly on point: pay
once in the mechanism, and every face after it is free.

## Ruling 1 — memory is O(1) in crowd size. Per-person texture memory is refused.

This is the trap, and it is being closed before a line is written rather than after a catalogue
depends on it.

The obvious implementation of "every person has their own skin" is to generate a texture per
person. **That is refused.** It makes texture memory scale with crowd size, which is P1 —
the ranked-first principle — inverted. Fifty villagers would cost fifty skins.

**Ruled:**

- The generator produces a **small shared set of base maps**, baked offline into `.mgetex`,
  resident once and sampled by every character. The count is fixed and budgeted, not per-person.
- **Per-person variation rides as material parameters** — a handful of scalars and colours fed
  from the phenotype (melanin depth, undertone, weathering, blemish placement seeds) — evaluated
  in the shader against the shared maps.
- Anything that would allocate texture memory per character is **a defect in the design**, not a
  cost to be budgeted. The `BudgetRegistry` cap refuses; it does not grow.

The property to hold: **adding the fifty-first villager costs zero texture bytes.** That is the
same shape as the body itself — one mesh, per-character parameters — and it is the reason a crowd
is affordable at all.

## Ruling 2 — a generator is engineering, so it is in charter. Its failure mode is not.

`AGENTS.md` §6.2 forbids originating art by eye. A generator is **code with measurable
properties** — colour space, texel density, tiling, histogram, conformance to
`assets/standards/skin_texture.mgestd` — so writing one is engineering and sits inside the
charter. The rule is not weakened.

**But the failure mode must be named, because measurement will not catch it.** A generator can
produce output that passes every numeric gate and looks like painted plastic. Every rule in the
standard can be green while the result is unusable. There is no metric for "reads as skin".

So: **the owner's eye stays the gate**, exactly as §6.2 requires, and it is put in the loop
*early and cheaply* rather than after a system is built. See the sequencing below. A session that
tunes a generator against its own judgment of how skin looks is repeating the retired modeler's
failure with different tools.

## Ruling 3 — sequencing: one face, rendered, before any system

The first deliverable is **not** a texture pipeline. It is **one skin on the shipped body,
rendered, put in front of the owner.**

Two reasons. It is the cheapest possible test of Ruling 2's failure mode — if generated skin reads
as plastic, that is knowable in one render rather than after a framework exists. And it **unblocks
a decision the owner is already holding**: ADR 0012 Ruling 1b waived the Face's 2.08× stretch
*provisionally*, with "the owner judges the first authored face texture" as its retirement
condition. That condition cannot be met until a face texture exists. This is the shortest path to
meeting it.

Only once the owner has seen a face and said whether it reads does the generator become a system:
parameter binding to the phenotype, the rest of the map set, budgets, the baker.

## Consequences

- The textures area gains a charter and stops being idle; `TEXTURING.md`'s standard finally has
  content to govern.
- The Face stretch decision becomes answerable, and ADR 0012's provisional waiver gets its test.
- The `.mgetex` baker and the map set in `TEXTURING.md` Part I acquire a producer.
- **Held open, not decided here:** whether *other* surfaces (cloth, leather, wood, stone) are also
  generated. This ADR rules on skin, which is what the owner ruled on. The same argument does not
  automatically transfer — a tunic is not required to be unique per person, so the Dictation 5
  reasoning that makes generation *necessary* for skin does not apply, and the question is an
  ordinary cost trade rather than a structural one. It goes back to the owner when skin lands.
- The second open Phase 15 proposal — **virtual textures on the P5 pattern** — remains
  undecided and is untouched by this ruling.


---

# REVERSAL — the base maps are IMPORTED, not generated (owner, same day)

> *"i think that importing textures will be more cost effective and higher quality"* — the owner,
> hours after ruling for generation.

**Accepted, and it corrects an inconsistency in my own reasoning that he caught and I did not.**

## What I got wrong

I raised the fork claiming a skin texture **cannot be imported**, because it must match *our*
chart and ours is bespoke. That was overstated, and two routes were open the whole time:

1. **Re-projection is mechanical and we already own the transform.** `tools/model/repack_uv.py`
   moved the source's 24 UDIM islands **as units** — no splits, no cut seams, vertex order
   untouched (ADR 0010). The mapping from the source's own layout to ours is therefore a
   per-island affine transform we wrote ourselves and can invert. **Any texture authored against
   the Blender base mesh's original layout transfers onto our chart by construction.**
2. **Tileable detail is chart-independent.** Pore and micro-detail applied in tangent space does
   not care what the chart looks like at all.

## The deeper error, which matters more than the factual one

`AGENTS.md` §6.2 exists because **a session cannot see what it is making**. It cost this project
a body, a retired agent and a week to learn, and it is why the character asset pipeline **sources
meshes and never originates them**.

I applied that rule rigorously to geometry and then, one layer up, proposed **originating skin by
writing a generator** — and even wrote the failure mode into the ADR ("a generator can pass every
numeric gate and look like painted plastic") without noticing I was describing the *same*
blindness the rule already forbids. Naming a hazard is not the same as heeding it.

**The owner's instinct was more consistent with this project's own principle than my ruling was.**
Recorded plainly, because the ADR that got it wrong is the one people will read.

On quality he is also straightforwardly right: photographed or scanned human skin is far beyond
what a generator we wrote would produce. Convincing procedural skin is close to a research
problem, and we would have been spending our scarcest resource on the one thing the outside world
supplies best.

## What actually changes — and what does not

**Ruling 1 stands entirely, and it was the load-bearing part.** Texture memory is still O(1) in
crowd size: a **small shared set of base maps**, resident once, sampled by everyone, with
**per-person variation as material parameters** driven by the phenotype. Adding the fifty-first
villager still costs zero texture bytes. Nothing about that depended on where the maps came from.

**Rulings 2 and 3 change:**

- **The base maps are imported**, re-projected through the inverse of the repack transform where
  they were authored for the source layout, or applied tileably in tangent space where they are
  chart-independent. The area's rule becomes the same one the mesh side already follows:
  **acquire → validate → process → integrate.**
- **What we build is the import path, not the content** — the re-projection tool, the
  conformance gates against `assets/standards/skin_texture.mgestd`, and the parameter layer that
  makes one imported base serve many people. That is engineering with measurable outputs, which
  is squarely in charter, and it is P12 in its proper form: pay once in the mechanism so the
  hundredth face is cheap.

**Dictation 5 is still satisfied, by the hybrid rather than by generation.** One imported skin is
one face; uniqueness comes from the parameter layer over it — melanin depth, undertone,
weathering, per-person blemish masks — which is how every shipped character system does this. The
argument that made generation *necessary* was never about generation; it was about **variation not
costing memory**, and that survives unchanged.

## The one thing to establish before building

**Sourceability is now the first question, and it is a measurement, not an assumption** — I have
just been caught asserting one. Before any pipeline work: establish what actually exists under an
acceptable licence for this body, and confirm a re-projected sample lands correctly on the frozen
chart.

**If nothing suitable is sourceable, that is a finding to bring back — not a licence to fall back
to generating.** Falling back silently would reinstate the reversed ruling without anyone deciding
it.

---

# AMENDMENT — the reversal named two routes; both were wrong for the actual need

**Date:** 2026-08-21 · **Raised by:** the textures session, task 15.0 · **Status:** accepted

I reversed this ADR on the owner's instruction and listed two ways an imported skin could reach
our chart. **The session measured both and neither answers 15.1.** Every number below is a file it
downloaded and measured or a licence it read at source.

- **Route 1 — re-project through the inverse of our repack.** Correct, and we do own the
  transform. **But its input set is empty.** Blender Studio's Human Base Meshes bundle — our
  body's source — ships base meshes with UV maps and *no textures at all*. There is nothing
  authored against that layout to apply the inverse to. A capability held in reserve, not a way to
  obtain a skin.
- **Route 2 — tileable detail in tangent space.** Real: ShareTextures `human_skin_4`, CC0, 4096²
  across 7 maps. It supplies pore and micro-detail over the whole body and needs no transfer.
  **But a tiling material has no layout, so it cannot place an eye.** It supplies skin, not a face,
  and 15.1 needs a face.

**The answer is a third route I did not list.** MakeHuman's system asset pack is CC0 and holds 22
complete human skins across age, ethnicity and sex — the session read the 268 MB zip's central
directory by byte range rather than downloading it, then pulled one entry the same way:
`young_caucasian_male`, 2048², photographic, a full-body layout with a real face. Authored for
MakeHuman's mesh, so **our affine says nothing about it and the transfer is mesh-to-mesh.**

**That changes what 15.2 builds** — a mesh-to-mesh transfer, not an inverse-affine re-projection.
Ruled: build that. The two earlier routes stay available and are worth keeping: Route 2 layers
detail over whatever base wins, and Route 1 remains the path for anything ever authored against
the Blender layout.

Recorded plainly because it is the second time this ADR has been corrected by measurement, both
times against something I asserted without one.

## Ruling — the Face island repack rides the clavicle event

The session also measured that **the face carries 483 px/m and needs ~965 to render a legible
pupil**, with both renders committed. Repacking the Face island 2× inside the existing 1024²
sheet is a `B-27` contract-version event.

**Ruled: it rides the ADR 0019 event.** That ruling already says everything pending goes into the
one rig-version event we are paying for; this qualifies exactly, and a chart change afterwards
would cost a second full garment re-bake for nothing. Added to that event's contents.

## Ruling — the skinned draw path cannot sample a texture at all, and that is the renderer's

`SkinnedDrawItem` carries no material, and `skinned.vert` reads `inUv` at location 2 and never
outputs it. **An imported skin is as useless on that path as a generated one would have been** —
which means the texture work has been aimed at a path that cannot display its output. Mirror what
`DrawItem` already does: same set 2, same `lit.frag`, same 1×1-white default so one pipeline serves
textured and untextured. Renderer-owned, and it is that area's charter after a day idle.
