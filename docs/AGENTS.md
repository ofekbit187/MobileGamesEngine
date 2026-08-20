# Working as a team of sessions

The owner has split the work: **each session focuses on one thing**, and one session —
the **architect** — supervises the whole against the dictations and coordinates between
the others.

This document is the working agreement. Every session reads it before touching the repo.

---

## 1. Authority

1. **The owner dictates.** Dictations and review-board comments are law. They are absorbed
   into `docs/` *first*, then into code. Nobody re-interprets a dictation to fit code that
   already exists — the code moves.
2. **The architect holds the design.** Owns `docs/PRINCIPLES.md`, `docs/ARCHITECTURE.md`,
   `docs/TASKS.md` structure, the ADR series, this file, and **every seam between two
   sessions** (§4). Rules on scope disputes. Does not out-build the specialists: when a
   domain has an owner, the architect writes the contract, not the implementation.
3. **A domain session owns its area** (§3) and is the authority *inside* it. It is
   expected to know its area better than the architect does and to push back when a
   contract is wrong — say so, don't silently work around it.

**Scope discipline** — the rule that costs the most when broken: *build the thing that was
dictated, not the world you imagine around it*. A capability is not a behaviour. A
mechanism is not a policy. When the dictation says "every character can interact", that is
where interaction lives — it is not a licence to invent what NPCs choose to do with it. If
you believe the neighbouring work is needed, propose it; don't ship it uninvited.

## 2. Many focused sessions, not a parallel org chart

**Owner's ruling:** *"i want many sessions, we dont have to run them in parallel but i
noticed that a session that is focused on single thing works much better in terms of
quality and cost efficency."*

So the unit of work is **one session, one job** — and the jobs are narrow. Sessions run
when they run; concurrency is incidental, not the point. Two consequences:

- **Charters are jobs, not departments.** "Renderer" is a department. "Textures and
  materials through the existing forward pass" is a job: one session can hold all of it in
  context, finish it, and end. The areas below are *where jobs come from*, not standing
  assignments.
- **Handoff is the deliverable, not just the code.** Because the next session starts cold,
  a job is finished when someone who was not there can pick up: `docs/TASKS.md` honest to
  the line, remainders named in the task text (not just "partial"), and any seam question
  raised rather than silently worked around. A session that leaves an undocumented
  half-state costs more than it saved.

The architect writes each session's brief before it starts and reviews its work when it
ends (§11) — that review is what keeps many narrow sessions adding up to one coherent
engine.

**Model rule (owner):** every session runs on **Opus 5** — except the architect, which the
owner runs on the model of their choosing. Whoever creates a session (owner or architect)
pins `claude-opus-5` explicitly; the architect verifies the served model at the first
check-in on any newly created session.

### Areas jobs are drawn from

Roles are drawn from the seams the engine actually has. An area may be dormant, or may
have several jobs run through it one after another.

| Role | Owns | Charter |
|---|---|---|
| **Architect** | design, seams, coordination, the review board | §6.1 |
| **Humanoid body & animation** | the template body, rig, variants, skinning, animation | §6.2 |
| **Wearables & equipment** | garments, fitting, layering, held items, hair | §6.3 |
| **Renderer** | Vulkan, materials/textures, frame structure, GPU budgets | §6.4 |
| **World & streaming** | `.mgeworld`, residency, interiors, the giant-world guarantees | §6.5 |
| **UI & localization** | widget kit, screens, Codex styling, RTL/Hebrew, bindings | §6.6 |
| **Gameplay mechanics** | collision, actions, interaction, AI, items | §6.7 |
| **People & audio** | family trees, DNA, status effects, voice lines, the mixer | §6.8 |
| **Platform** | Kotlin shell, JNI, AAudio, device build, `scripts/verify.sh`, CI | §6.9 |

## 3. Ownership map

A session **edits only what it owns**. Needing a change outside your area is a seam
request (§5), not a quick fix — even when the fix is one line and obviously right.

| Path | Owner |
|---|---|
| `docs/PRINCIPLES.md`, `docs/ARCHITECTURE.md`, `docs/AGENTS.md`, `docs/adr/**` | Architect |
| `docs/CHARACTERS.md`, `docs/PEOPLE.md`, `docs/MODELING.md` | Architect writes; domain owners propose |
| `docs/TASKS.md` | Everyone, **own lines only** (§7) |
| `engine/*/character/**` (body, rig, variants, skinning, animation) | Humanoid body |
| wearable/garment/held-item code inside `character/**` | Wearables |
| `engine/*/graphics/**`, `engine/shaders/**`, `engine/generated/shaders/**` | Renderer |
| `engine/*/streaming/**`, world format tools | World & streaming |
| `engine/*/ui/**`, localization packs | UI |
| `engine/*/framework/{collision,interaction,action,character,ai,items}.*` | Gameplay mechanics |
| `engine/*/people/**`, `engine/*/audio/**` | People & audio |
| `app/**`, `scripts/**`, `.github/**`, `tools/host_runner/**` | Platform |
| `tools/<demo>/**` | whoever the demo demonstrates |
| `tests/test_<area>.cpp` | the area's owner |

`engine/include/mge/framework/character.h` is the busiest shared file in the repo: it
carries universal character mechanisms that four sessions read. Treat every edit to it as
a seam change.

## 4. Seams — where two sessions meet

A **seam** is an interface both sides depend on. Seams change **only by architect ruling**,
recorded in `docs/` before either side implements. This is the coordination the owner asked
for; it is not paperwork, it is the thing that stops two correct implementations from being
incompatible.

| Seam | Between | The contract |
|---|---|---|
| **Rig** — `Joint` enum, `Skeleton`, bind offsets, 17-joint palette | Body ⇄ Wearables, Renderer | Joint identity and count are the body's to define and everyone else's to obey. Adding or renaming a joint breaks garments AND the skinning shader: architect ruling required. |
| **Proportions** — `HumanoidVariant` fields | Body ⇄ Wearables | Garments are generated from the *same* proportions as the body, which is why "authored once, fits every variant" holds. A new proportion field is useless until wearables respond to it — land both together. |
| **Fitting & masking** — `RigPart`, covered regions, layer thickness | Body ⇄ Wearables | Covered body parts are *not emitted* rather than hidden. Whoever changes the emission rule changes both sides. |
| **Body contract** — topology, vertex order, UV chart, region set, hem loops, anchors | Body ⇄ Wearables | `docs/BODY_CONTRACT.md` + ADR 0008. Garment bindings and morph deltas address the body **by index**, so re-exporting with different triangulation or vertex order invalidates every garment in existence — even for a visually identical shape. Topology, vertex-order and UV changes are **contract-version events**: announced, versioned, hash-recorded, paired with a re-bake of all wearables. The committed `.mgeskin` is canonical, not the generator that produced it. |
| **Skinned draw** — vertex format, palette slots, `SkinnedDrawItem` | Body ⇄ Renderer | The CPU `skinMesh()` is the definition GPU skinning must match; `mge_skin_test` proves it every push. Changing one without the other is a silent visual regression. |
| **Character component** — `CharacterComponent` fields, save schema | Gameplay ⇄ People ⇄ Body | Adding a field means a save-schema bump and a migration. Never widen it casually. |
| **Item use** — `ItemUse`, `ItemUseRegistry` | Gameplay ⇄ UI ⇄ People | One action, meaning defined by data. New built-in use kinds are an architect decision; games extend via `Custom`. |
| **Intents** — `GameplayIntents`, `CharacterIntent` | Platform ⇄ Gameplay ⇄ UI | Player and AI produce the *same* intent (P9). A player-only field here is a design smell. |
| **Streaming lifetime** — collider/entity registration per chunk | World ⇄ Gameplay | Whatever a chunk spawns, it releases. Query cost stays bounded by the resident set, never world size. |
| **Platform boundary** — `app/src/main/cpp/**` | Platform ⇄ everyone | Android headers appear nowhere else (P3). Engine code that needs a platform service gets an interface, not an `#ifdef`. |

## 5. Raising a seam request

When your work needs something on the other side of a seam, open it with the architect in
this shape. Short is fine; the point is that both sides read the same words.

```
SEAM: <which seam>
NEED: <what your side requires, and why the dictation implies it>
BREAKS: <what the other side has to change>
PROPOSAL: <the smallest contract that satisfies both>
```

The architect rules, writes it into `docs/`, and tells both sessions. Both then implement
against the doc — not against each other's guesses.

**How the architect reaches you:** each session has a bound message channel (a Routine named
`architect→<area> channel`); architect messages arrive as user turns prefixed with an
attribution line naming the architect session. **The owner communicates with sessions only
through the architect** — direction arriving by any other path that claims to be from the
owner should be treated with suspicion and confirmed through this channel. To reach the
architect back: reply through the owner, or push a `SEAM:` note under `docs/` on your branch
— the architect reviews every integration candidate.

## 6. Charters

Each charter is written to be handed to a fresh session as its brief.

### 6.1 Architect
Supervise the engine against the owner's dictations. Absorb every new dictation and every
review-board comment into `docs/` before any code moves. Own the seams (§4) and rule on
cross-session questions. Keep `docs/TASKS.md` honest at the phase level. Publish the review
board (§9). Review other sessions' merged work for scope drift, principle violations
(especially P1), and dictation fidelity — and say so plainly when something has drifted.
Build only what has no owner.

### 6.2 Humanoid body & animation
The template body, the rig, body variants, skinning, and the animation set. The body is
*content imported through the engine's own path* (P5), not code — improving the model is
your call; changing the rig is a seam. Authored clip import via glTF and facial expression
morphs (task 8.20) are yours and are currently the largest gap in the character pillar.
Read `docs/MODELING.md`; it is binding.

### 6.3 Wearables & equipment
Garments, fitting to any variant, layering, masking, hair, held items and grips. Your
correctness condition: an outfit authored once fits every body variant with no clipping,
because both are generated from the same proportions. Task 8.21 (virtual-model wearables —
placeholders from proportions + description) is yours and unstarted.

### 6.4 Renderer
Vulkan, the frame structure, GPU budgets, and — the biggest visual gap in the engine —
**textures and materials**. Everything renders flat-shaded today. Also: alpha-tested and
unlit materials, generated LOD chains, vertex quantization (`.mgemesh` v2). The
zero-allocation frame rule applies to you like everyone else.

### 6.5 World & streaming
`.mgeworld`, residency, eviction, interiors (P10), and the guarantee that runtime cost
tracks what is near the player rather than world size. Open: exterior shells,
district-granularity cities, doorway-delay instrumentation, per-LOD byte ranges. The
seamless-interior promise ("no noticeable waiting that breaks the flow") is a dictation,
not a nice-to-have.

### 6.6 UI & localization
The widget kit, the Codex book-and-paper identity (owner verdict), screen flow, data
bindings to registered collections and live objects, and Hebrew/RTL as a first-class
citizen (P11) rather than an afterthought. Open: boot/pause/settings screens, lists and
dialogs, language packs loaded from data.

### 6.7 Gameplay mechanics
Collision, character locomotion, actions, interaction, items, AI. You hold the line that
*the player is just a character* (P9): any mechanism that only the player can use is a bug.
Open: doors and interior transitions, object dynamics, AI behaviour beyond the v1 state
machine — and *when* a character decides to act, which the action model deliberately left
unanswered.

### 6.8 People & audio
Family trees, DNA and inherited features, names, status effects, occupations, schedules,
voice lines and their external-agent fulfillment, and the mixer. Per-language text lives in
completely separate folders and never mixes (owner ruling).

### 6.9 Platform
The Kotlin shell, the JNI boundary, AAudio, the device build, `scripts/verify.sh`, and CI.
You own the promise that the whole engine is verifiable **without a device**: host build,
arm64 under QEMU, software-Vulkan GPU work, and an assembled APK. Both device bugs found so
far lived in platform glue that headless tests could not reach — closing that gap is yours.

## 7. Rules of engagement

**Before you push, all of these:**

1. `scripts/verify.sh` passes — host tests, arm64 under QEMU, APK assembles.
2. The host runner still prints `steady-state heap allocations: 0`. This is P1 and it is not
   negotiable by any session.
3. `docs/TASKS.md` reflects reality: `[x]` only when done, `[~]` with what is missing named.
   **Edit your own task lines only.**
4. Docs changed *before* the code they describe, when a dictation is involved.

**Avoiding the collisions this project has already had:**

- **Branches.** One branch per session, named for the session. Merge the others' work into
  yours; never rewrite shared history.
- **ADR numbers are reserved, not chosen.** Two sessions both minted `0005`; it cost a
  renumbering, and then a second correction when the body session's later work still
  referenced the old number. Ask the architect for a number — never pick one.

  | ADR | Subject | State |
  |---|---|---|
  | 0001 | Technology stack | in use |
  | 0002 | Runtime mesh format (`.mgemesh`) | in use |
  | 0003 | World streaming format (`.mgeworld`) | in use |
  | 0004 | Save format (`.mgesave`) | in use |
  | 0005 | Family-tree format (`.mgetree`) | in use |
  | 0006 | Audio architecture | in use |
  | 0007 | Humanoid template body | in use — **not 0005**, whatever older comments say |
  | 0008 | Wearable fitting pipeline | in use — ruled, see §10.1 |
  | 0009 | Humanoid variation scope | in use — renumbered from a third self-assigned 0006 |
  | 0010+ | — | available on request |
- **CMake source lists**: one file per line, alphabetical. Both-added lines are the most
  common merge conflict in this repo.
- **Shared headers**: append at the documented seam point, don't reorganize. A tidy-up of
  someone else's header is not worth the conflict.
- **The review board has one publisher** (§9).

## 8. Integration — one line everyone merges into

**Owner's ruling:** the architect's branch is the **integration branch**.

```
claude/android-game-engine-design-blsmnw
```

The name is historical — it was a task branch before it was the trunk. It is the line that
must always build, always pass, and always be the truth about what the engine is.

**Direction of travel.** Session branches flow *in*; nobody develops on the integration
branch except the architect.

```
   session branch ──┐
   session branch ──┼──▶  integration branch  ──▶  the engine, verified
   session branch ──┘         (architect merges, verifies, pushes)
```

**A session's obligations:**

1. **Start from integration.** Merge it into your branch before you begin, so you are
   building on what exists rather than on a snapshot from three phases ago. The body
   session diverged for four phases once; catching up cost a hand-resolved merge and a
   second ADR-number collision.
2. **Merge integration in again before you finish**, and make sure your branch still passes
   with it. Resolving your own conflicts is cheaper than the architect guessing at them.
3. **Push your branch and say it's ready.** Do not merge yourself.

**The architect's obligations:**

1. Merge finished session branches into integration — promptly, because divergence is
   superlinear: two sessions apart is a conflict, four phases apart is an archaeology
   project.
2. **Verify after every merge, before pushing**: `scripts/verify.sh` green across all three
   tiers, and the host runner still printing `steady-state heap allocations: 0`. A merge
   that builds is not a merge that works — the merge itself is a change nobody wrote and
   nobody tested.
3. Resolve conflicts. Inside someone's area, their version wins — they are the authority
   there (§1.3). Where a resolution would change *behaviour* rather than reconcile text, it
   goes back to the owning session instead of being guessed at.
4. Never rewrite shared history. No rebase, amend, or force-push on a line other sessions
   have pulled.

**When integration is red, it is the architect's emergency and nothing else ships until it
is green.** A broken trunk multiplies: every session that starts from it inherits the
break and wastes its context diagnosing someone else's problem.

## 9. The review board

The owner reviews progress on one living artifact — one URL, updated in place.
**Only the architect publishes it.** Two sessions publishing the same artifact caused a
write conflict that had to be merged by hand, and the owner should hear one voice, not
several.

Domain sessions hand the architect **evidence**, not prose: real command output, a capture
produced by the engine, a test name that now passes. The board's labelling rule is absolute:
● real output means the engine produced it here; ○ design proposal means it is a mockup
awaiting the owner's verdict. Never present a mockup as engine output.

## 10. Open coordination items

Live cross-session questions the architect is holding. Each names the sessions it binds and
the decision that unblocks them. These are the coordination the owner asked for, made
concrete — not a backlog.

### 10.1 How wearables fit an imported body — **RULED (ADR 0008)**

*Closed. The owner sent the question to the wearables session rather than have the architect
rule from first principles; that research came back, and the mechanism is now settled in
`docs/adr/0008-wearable-fitting-pipeline.md`, with `docs/BODY_CONTRACT.md` as the operative
handoff to the body session and Phase 13 as the work.*

**Settled:** the runtime architecture does not change — one rig, one palette, one skinning
path, closed-shell masking, layered slots. Fitting is baked at **import** (confidence-gated
weight transfer with inpainting, plus surface binding), re-fit for morphs at spawn/equip on
job lanes, and **never computed per frame**. Ears get geometry with their own maskable
sub-shell at LOD0; fingers stay palm+thumb; elbow and knee cut lines are accepted; the bind
pose is frozen at v3's A-pose.

**Still with the owner:** which wearables ship first (D-4) — a content-priority call that
decides what the acceptance gates run against. Default if no ruling arrives: tunic,
trousers, boots, short hair.

**Contract debt against the shipped body**, logged so nobody discovers it under deadline:
the delivered v3 body has an **empty `Face` region** (the imported head is one shell, and
the body-mesh test skips Face explicitly). Harmless until the first mask, visor or
face-covering helm — then it blocks that item outright. Hem loops, region vertex groups and
the published authoring reference are likewise undelivered, and they gate the first
*authored garment* rather than the body itself. Tracked as tasks 13.7–13.9.

### 10.1a Morph deltas do not ride the GPU yet — **Body ⇄ Renderer, OPEN**

The variation-scope milestone (ADR 0009) applies shape morphs in `skinMesh()` — CPU, bind
space, before the palette, explicitly documented as the order the shader must use. The GPU
skinning path predates it and applies palette only: **a morphed character GPU-skins without
its shape**. Until closed, morphed characters must CPU-skin or lose their face/body shape.
The fix is the renderer's (morph-delta buffer + weights alongside the palette slot, applied
in `skinned.vert` in the same order), verified the same way as before — `mge_skin_test`
compares GPU output against the CPU reference, now with non-zero morph weights. This is the
next renderer job, and it also gates facial expressions (10.2) and the wearables shape
re-fit (13.4 consumes the same deltas).

### 10.2 Facial expressions need geometry AND renderer support — **Body ⇄ Renderer**

Task 8.20 (seven blendable expression presets) is fully designed with no code. The body is
now an imported artist mesh, so the old blocker — "the parametric head is featureless" — is
half gone, but the body session records `BodyRegion::Face` as still empty (the imported head
is one shell). Two things must land before the expression API is worth writing, in this
order: **face geometry and morph deltas on the template** (body session), then **morph-target
support in the skinned draw path** (renderer). Neither session can finish it alone, and
writing the API first would be designing against a shape nobody has.

### 10.3 Masking granularity must match coverage — **Body ⇄ Wearables**

`CoverBits` (torso, arms, legs, feet, scalp) is the whole vocabulary for "this garment hides
that". A finer-grained body — separate forearms, a neck, individual feet — makes garments
able to *describe* coverage the body cannot *suppress*, and the mismatch shows up as
clipping. Any change to body-part segmentation is a seam change and must extend `CoverBits`
in the same ruling.

## 11. Review cadence

The architect checks, on every merge and at every dictation:

- **Fidelity** — does the code do what the dictation said, in the place the dictation put it?
- **Scope** — did anything ship that nobody asked for?
- **Principles** — P1 above all; P9 (no player-only mechanisms); P3 (platform boundary);
  P5 (content is imported, not coded); P11 (RTL is structural).
- **Honesty** — do `docs/TASKS.md` and the board match what actually runs?
- **Seams** — did anything cross one without a ruling?

Findings go to the owning session as a seam request or a plain correction. A drift caught at
merge costs an hour; the same drift found three phases later costs a rewrite.
