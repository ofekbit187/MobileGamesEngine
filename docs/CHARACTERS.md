# Character System

The engine's character model: a universal `Character` entity that every acting being in the game is built from, with `HumanoidCharacter` as the engine-provided specialization carrying the template body, body variants, skeleton/animations, and the wearables mechanism.

Governed by the [principles](PRINCIPLES.md) — especially **P9 (the player is just a character)** — and slotted into the [architecture](ARCHITECTURE.md) inside the Game Framework, with rendering/animation support in the Graphics Engine and asset support in the Asset System.

---

## 1. The character hierarchy

```
Entity
└── Character                      ← universal mechanisms live here
    ├── HumanoidCharacter          ← engine-provided: template body, variants,
    │                                 skeleton, wearables, humanoid animations
    │   └── Person                 ← Dictation 5: identity layer — family tree,
    │                                 name, DNA, status effects, voice (PEOPLE.md)
    └── (game-defined characters)  ← animals, monsters, anything the game
                                      creator wants — built directly on Character
```

- `Character` is a **basic engine entity**. Every acting being — the player, human NPCs, animals, monsters — is a `Character`.
- `HumanoidCharacter` inherits from `Character` and adds everything specific to human-shaped bodies.
- Non-humanoid characters (animals, monsters, whatever the game creator invents) extend `Character` directly. They get all universal mechanisms for free and supply their own body/model/skeleton.

## 2. Player/NPC parity (P9)

**There is no difference between the player and NPCs except how they are controlled.**

- One character class serves both. There is no `PlayerCharacter` type with special powers; there is a `Character` with a controller attached.
- **Controllers** are the only distinction:
  - `PlayerController` — feeds gameplay intents from the input pipeline / gameplay controls (architecture §2.2, task 3.5).
  - `AIController` — feeds gameplay intents from the AI system.
  - Both drive the exact same character interface (the intent stream: move, look, act, use, equip…). Swapping controllers at runtime is legal and cheap — possession of any NPC, cutscene control, or AI-driving the player are all the same operation.
- **Same features on every character**: every NPC has everything the player has — including **inventory**, **wearable slots**, and a **tool/weapon slot**. An NPC can pick things up, wear armor, wield tools, and lose them on death, exactly like the player.
- Consequence for systems design: any feature written for "the player" must be written for `Character`. Code that asks "is this the player?" is a design smell; ask "which controller?" or "which faction?" instead.

## 3. Universal mechanisms (on `Character`)

These work identically for every character, humanoid or not:

| Mechanism | Notes |
|---|---|
| **Locomotion** | Walking/running as movement intents resolved against the world; per-character movement parameters (speeds, turn rates). The *animation* of locomotion is body-specific; the *mechanism* is universal. |
| **AI** | The AI system drives any character through `AIController`. Behavior definitions are data, assignable to humanoids and non-humanoids alike. |
| **Mortality** | Health, damage intake, death and its consequences (loot drop from inventory/equipment, corpse handling, despawn rules). |
| **Enemy/ally classification** | A faction/relationship system: characters belong to factions; factions have stances toward each other (ally, neutral, enemy) that AI, targeting, and UI read. Per-character overrides allowed. |
| **Inventory** | Container of item stacks with capacity rules. On every character (P9). |
| **Equipment slots** | Named slots on the body: wearable slots plus the tool/weapon slot. The *slot set* is defined by the character's body definition — a humanoid has the humanoid slot set (§6), a game-defined creature declares its own (a horse can declare a saddle slot). |
| **Perception hooks** | What AI senses (sight/hearing ranges) — universal so any creature can perceive. |
| **Interaction** | Acting on the world — taking a thing, opening a container, speaking to someone. **Every character can interact** (owner ruling, Phase 11). This is a *capability of being a character*, not a behaviour and not a player privilege: the verbs live on `CharacterSystem` (`focus` / `interact` / `interactWith`), so there is nowhere to put a "can interact" flag — if you are a character, they are yours. Characters are equally valid *targets*: the player can be spoken to. What differs between the player and anyone else is only who decides to act — a tap, or whatever steers that character (P9). Deciding *when* to act is a separate question, left to behaviour, occupation and schedule. |
| **Streaming & persistence behavior** | Characters live in world chunks, stream in/out (P2), and persist their state via the save delta system (P7) — inventory, equipment, health, faction overrides included. |

**Humanoid-exclusive mechanisms** live on `HumanoidCharacter` only: the template body and its variant system, the humanoid skeleton and animation set, and the body-part wearable fitting mechanism (§4–§6). The split rule: *if a mechanism makes sense for a wolf, it belongs on `Character`; if it assumes a human-shaped body, it belongs on `HumanoidCharacter`.*

## 4. The humanoid template body

- The engine ships **one template base body**: a neutral humanoid model, **imported** through the engine's native model import path (P5) like any other asset — it is content, not code, and can be re-imported/upgraded.
- The template body defines the canonical humanoid: its mesh, its UV layout (all skin textures follow it), its **body-part segmentation** (§6), and its skeleton binding.
- Every humanoid in every game — player and NPCs — is an instance of the template body with a **variant** applied. No humanoid ever ships as a unique baked mesh unless a game explicitly opts out.

### 4.1 Body variants (data-driven)

Humanoid variety comes from **variant data files**, not from new models.

- A variant file is plain data (asset type: `humanoid_variant`) that the engine applies to the template body at load/spawn time. Variants are cheap, stackable with equipment, and streamable.
- Parameters a variant can modify (initial set, extensible):
  - **Skin texture** — swap/select the skin texture (template UV layout makes any conforming texture valid)
  - **Size** — overall uniform scale
  - **Width / Height** — global proportions
  - **Shoulders** — breadth
  - **Chest** — build/depth
  - **Legs** — length/thickness
  - **Feet** — size
  - **Face** — head shape plus **further facial features** (a nested parameter group: eyes, nose, mouth, jaw, ears, brow… — the face is its own sub-schema designed to grow)
- Implementation stance: variants are realized as a combination of **skeleton-proportion scaling** (bone lengths/offsets for height, legs, shoulders…) and **morph deltas** on the template mesh (chest, face, fine features). Both representations are compact data (P1) and both must be visible to the wearable fitting mechanism (§5) so clothes follow the body.
- Variants compose: a game can define a base variant ("villager build") and layer instance tweaks on top (this villager is taller).

### 4.2 Skeleton & default animations

- The template body carries a **skeleton** (the engine's canonical humanoid rig). All humanoid animation targets this rig; all wearables bind to it.
- The engine ships **default locomotion animations**: several walking and running animations (variations for gait/speed), plus idle, with speed-blended playback driven by the universal locomotion mechanism. Games extend the set; they never start from zero.
- Because variants change proportions via the same skeleton, **all animations work on all variants** — retargeting inside the humanoid family is free by construction.

## 5. Wearables — the fitting mechanism

*(Flagged as one of the most important systems; designed for attention.)*

A **wearable** is an asset that attaches to a body part and **dynamically fits itself to that body seamlessly** — on any body variant, without per-variant authoring.

### 5.1 How fitting works

- Wearables are **authored once against the template body**: modeled around the template's neutral shape and skinned to the canonical skeleton.
- At runtime, the engine applies to the wearable **the same variant transformations the body received** — the skeleton-proportion scaling and the morph deltas of the covered region — so the wearable deforms in lockstep with the body underneath. A jacket authored on the template fits the broad-shouldered variant and the narrow one, seamlessly, from the same asset.
- **No clipping by construction**: a wearable declares which body-part surface regions it *covers*; the engine suppresses the covered skin geometry underneath (per-region mesh masking). The body is never fighting its clothes.
- Wearables animate with the body automatically — same skeleton, same skinning path, no extra cost model beyond the mesh itself (P1: one skinning pass over body + worn meshes).

### 5.2 Wearable assets

A wearable asset declares:

- `slot` — which body-part slot it occupies (§6)
- `layer` — which layer within that slot it occupies (§5.4)
- `covers` — which skin regions it masks
- mesh + materials, authored on the template body
- optional variant-response data (how strongly it follows specific morphs — rigid items like a helmet follow bone scale but ignore soft morphs)
- gameplay data (game-defined: protection, warmth, value…the engine carries it opaquely)

Wearables are normal assets: importable natively, streamable, and **virtual-model compatible** (P5) — a wearable can exist as proportions + description with a placeholder on the body before the real asset is made.

### 5.3 Hair as a wearable

Hairstyles are wearables. A hairstyle occupies the `head_hair` slot and rides the wearable mechanism — authored against the template head, attached via the same fitting path, deforming with head/face variants seamlessly. This is deliberate: one mechanism, exhaustively good, instead of a parallel hair system. (Beards/eyebrows can follow the same route via face-region slots as the face sub-schema grows.)

### 5.4 Layering

Each wearable slot holds a **stack of layers**, not a single item. A character can wear underclothes, clothing, and armor on the same body part simultaneously.

- Standard layers (per slot, extensible in the body definition):
  - `base` (0) — underwear, undershirts
  - `mid` (1) — everyday clothing
  - `outer` (2) — armor, coats, overwear
- One wearable per (slot, layer); equipping into an occupied layer swaps the item there, leaving other layers untouched.
- **Fitting across layers**: every layer fits against the variant-transformed body, and each layer additionally receives the accumulated **thickness offsets** of the layers beneath it — the coat sits over the shirt, which sits over the skin, on any body variant. A wearable declares its thickness profile per covered region (thin cloth adds almost nothing; plate adds bulk).
- **Masking cascades outward**: the outermost wearable covering a region masks that region on every layer below it and on the skin — inner geometry that can't be seen is not skinned, not drawn, and costs nothing (P1). A save stores the full stack; rendering only pays for what's visible.
- Hairstyles interact with the cascade like anything else: a helmet in `head_top` that declares coverage of the scalp region masks (or switches to a "hat-compressed" variant of) the hairstyle beneath it.

## 6. Body parts & the humanoid slot set

The template body is segmented into named **body parts**; each body part exposes a **wearable slot** (a body part can be assigned a wearable — every body part is dressable):

| Body part | Slot | Examples |
|---|---|---|
| Head (scalp) | `head_hair` | hairstyles (§5.3) |
| Head (crown) | `head_top` | hats, helmets, hoods |
| Face | `face` | masks, glasses |
| Torso | `torso` | shirts, jackets, armor |
| Hands | `hands` | gloves, gauntlets |
| Legs | `legs` | pants, greaves |
| Feet | `feet` | shoes, boots |
| **Tool/weapon (main hand)** | `held_main` | tools & weapons — rigid attach to grip points, not the fitting mechanism (§6.1) |
| **Off hand** | `held_off` | second weapon, shield, torch, tool (§6.1) |

- The slot set is data (part of the body definition), so it can be extended (rings, back/cloak, shoulders…) without engine changes. Every wearable slot carries the layer stack of §5.4.
- Non-humanoid characters reuse the same *slot machinery* with their own body definitions (a mount's saddle slot); the humanoid *fitting* mechanism (variant-following deformation) ships for the humanoid template, and its generalization to custom bodies is a later, opt-in extension.

### 6.1 Held items

Held items (tools and weapons) attach rigidly to skeleton attachment points rather than deforming through the fitting mechanism.

- **Grip types** — a held item declares its grip: `one_handed`, `two_handed`, or `versatile` (usable either way). Two-handed items occupy both `held_main` and `held_off`; the animation set selects matching pose/locomotion overlays per grip type.
- **Dual wield** — `held_main` and `held_off` can each hold a one-handed item; the off hand can alternatively hold non-weapons (shield, torch, lantern, tool).
- **Grip points** — the item defines its own grip transform(s) (primary grip, second-hand grip for two-handers); the template skeleton provides hand attachment points. Item grip meets hand point — no per-item animation authoring.
- **Sheathing** — items have a `drawn`/`sheathed` state. The body definition provides sheath attachment points (`hip_l`, `hip_r`, `back`, extensible); the item declares which it uses. Draw/sheath transitions come with default animations; a sheathed item remains visible on the body and streams/persists with the character.
- Held items are ordinary assets: natively importable, streamable, virtual-model compatible (a placeholder sword of declared proportions works in hand and on the hip).

## 7. Basic AI (v1)

The engine ships a **basic AI now, designed to be expanded later**. The `AIController` attachment point (§2) is the stable contract; the machinery behind it will grow without touching characters or games.

v1 is a data-defined **state machine** over the universal mechanisms:

- **States**: `idle`, `wander` (roam within a home radius), `patrol` (follow a point path), `chase`, `attack`, `flee`, `return` (go back to home/path).
- **Transitions** driven by the universal systems: perception hooks detect characters in range → the faction system classifies them (§3) → enemy sighted triggers `chase`/`attack` per profile, low health triggers `flee`, target lost triggers `return`.
- **AI profiles are data files** (asset type: `ai_profile`): which states are enabled, radii/ranges, speeds, aggression and flee thresholds, patrol path reference. One profile format serves humanoids, animals, and monsters — a deer is `wander` + `flee`-on-sight; a guard is `patrol` + `chase`/`attack` hostiles.
- **Budgeted** (P1/P2): AI ticks are scheduled with LOD — near characters think every simulation step, distant ones at reduced rates, characters in cold chunks don't tick at all (their AI state persists and resumes on residency).
- **Expansion path** (later, dictated): richer behavior models (behavior trees/utility AI), schedules & daily routines, group behavior, combat depth. These replace the *internals* of `AIController`; profiles migrate, the attachment contract does not change.

## 8. Basic facial expressions

Facial features include **basic runtime expressions** — a small fixed set, not a full performance system.

- The face sub-schema (§4.1) doubles as the expression rig: expressions are **morph presets on the template face**, blendable over any face variant (the same delta machinery as variants, applied dynamically).
- Built-in set: `neutral`, `happy`, `sad`, `angry`, `surprised`, `afraid`, `pain`. Games can add presets as data.
- API: set or blend an expression with a transition time (`setExpression(happy, 0.3s)`); AI profiles and gameplay can trigger them (hit → `pain`, enemy sighted → `angry`).
- Deliberately excluded for now: lip-sync, full emote animation, procedural gaze — held for later dictation.

## 9. Open points awaiting dictation

- AI expansion (behavior model beyond the v1 state machine, schedules, group behavior)
- Non-humanoid animation authoring workflow
- Advanced facial animation (lip-sync, emotes, gaze) beyond the basic expression set

*Person NPCs — family trees, DNA heredity, status effects, occupations/schedules, and voiced text lines — were dictated as Dictation 5 and are specified in [`PEOPLE.md`](PEOPLE.md).*

---

*This document extends the foundations; it will grow as more character-related requirements are dictated.*
