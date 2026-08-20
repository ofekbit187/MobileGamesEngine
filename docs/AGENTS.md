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

## 2. The roster

**Status: proposed — awaiting the owner's verdict.** Roles are drawn from the seams the
engine actually has, not from an org chart. One session per role; a role may be dormant.

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

## 6. Charters

Each charter is written to be handed to a fresh session as its brief.

### 6.1 Architect
Supervise the engine against the owner's dictations. Absorb every new dictation and every
review-board comment into `docs/` before any code moves. Own the seams (§4) and rule on
cross-session questions. Keep `docs/TASKS.md` honest at the phase level. Publish the review
board (§8). Review other sessions' merged work for scope drift, principle violations
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
- **ADR numbers are reserved, not chosen.** Two sessions both minted `0005` once and it cost
  a renumbering. Ask the architect for a number. Reserved so far: 0001–0007 in use;
  **0008+ available on request**.
- **CMake source lists**: one file per line, alphabetical. Both-added lines are the most
  common merge conflict in this repo.
- **Shared headers**: append at the documented seam point, don't reorganize. A tidy-up of
  someone else's header is not worth the conflict.
- **The review board has one publisher** (§8).

## 8. The review board

The owner reviews progress on one living artifact — one URL, updated in place.
**Only the architect publishes it.** Two sessions publishing the same artifact caused a
write conflict that had to be merged by hand, and the owner should hear one voice, not
several.

Domain sessions hand the architect **evidence**, not prose: real command output, a capture
produced by the engine, a test name that now passes. The board's labelling rule is absolute:
● real output means the engine produced it here; ○ design proposal means it is a mockup
awaiting the owner's verdict. Never present a mockup as engine output.

## 9. Open coordination items

Live cross-session questions the architect is holding. Each names the sessions it binds and
the decision that unblocks them. These are the coordination the owner asked for, made
concrete — not a backlog.

### 9.1 Authored garments vs generated garments — **Body ⇄ Wearables**

*The biggest unresolved seam in the engine, and it decides both sessions' direction.*

Today wearables are **parametric templates** (`WearableKind`: tunic, armor, pants, boots,
hair, sword) generated from the same `HumanoidVariant` proportions as the body, plus a
per-layer thickness. That is *why* "authored once, fits every variant" currently holds and
why masking is exact — a covered region is simply not emitted, so clipping is impossible by
construction.

The body session is moving to an **imported artist body** with smooth skinning. When that
lands, generated-from-proportions garments no longer automatically match a mesh nobody
generated. Two futures:

- **A — garments stay generated.** Fitting stays exact and free; the price is that clothing
  can only ever be as expressive as the parameter set, which will hold the art back.
- **B — garments become authored meshes**, skinned to the same rig and deformed to the
  variant by the same proportion machinery the body uses. Expressive; the price is that
  fitting becomes an approximation and clipping becomes possible, so masking has to earn
  its exactness a harder way.

**Architect position:** B is where a real game ends up, but not yet — it is only worth
paying for once the imported body and its variant deformation are proven, because B's
fitting quality is bounded by them. Until then wearables should treat the parametric path
as the shipping path and *not* start authoring garment meshes against a body that is still
changing shape. Both sessions must land any move to B together; a half-migration renders
characters in two incompatible ways at once.

**Needs:** the owner's direction on how much clothing expressiveness matters versus
guaranteed-clean fitting, and the body session's confidence that variant deformation on the
imported body is stable.

### 9.2 Facial expressions are blocked on geometry — **Body ⇄ Renderer**

Task 8.20 (seven blendable expression presets) is fully designed and has no code, for a real
reason: the v1 parametric head has no facial geometry to morph. It unblocks when the
imported face lands — and it also needs **morph-target support in the skinned draw path**,
which is the renderer's, not the body's. Neither session can finish it alone; sequence it
body-first, renderer-second, and don't start the expression API until both are in place.

### 9.3 Masking granularity must match coverage — **Body ⇄ Wearables**

`CoverBits` (torso, arms, legs, feet, scalp) is the whole vocabulary for "this garment hides
that". A finer-grained body — separate forearms, a neck, individual feet — makes garments
able to *describe* coverage the body cannot *suppress*, and the mismatch shows up as
clipping. Any change to body-part segmentation is a seam change and must extend `CoverBits`
in the same ruling.

## 10. Review cadence

The architect checks, on every merge and at every dictation:

- **Fidelity** — does the code do what the dictation said, in the place the dictation put it?
- **Scope** — did anything ship that nobody asked for?
- **Principles** — P1 above all; P9 (no player-only mechanisms); P3 (platform boundary);
  P5 (content is imported, not coded); P11 (RTL is structural).
- **Honesty** — do `docs/TASKS.md` and the board match what actually runs?
- **Seams** — did anything cross one without a ruling?

Findings go to the owning session as a seam request or a plain correction. A drift caught at
merge costs an hour; the same drift found three phases later costs a rewrite.
