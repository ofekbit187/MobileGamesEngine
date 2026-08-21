# ADR 0014 — Skin is generated, not painted

**Status:** accepted (owner dictation, 2026-08-21) · **Date:** 2026-08-21
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
