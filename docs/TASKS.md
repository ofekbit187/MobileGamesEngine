# Task Breakdown

Phased tasks derived from the [principles](PRINCIPLES.md) and [architecture](ARCHITECTURE.md). Phases build bottom-up through the layer diagram; each phase ends with something runnable and measurable. Order within a phase is roughly dependency order.

Status legend: `[ ]` not started · `[~]` in progress · `[x]` done

> The engine is still being dictated. Phases 0–2 are stable enough to act on; later phases will be reshaped as more requirements arrive. Do not begin a phase whose requirements are still open.

---

## Phase 0 — Project & design groundwork

- [x] **0.1** Establish principles document (`docs/PRINCIPLES.md`)
- [x] **0.2** Establish architecture document (`docs/ARCHITECTURE.md`)
- [x] **0.3** Establish task breakdown (this document)
- [ ] **0.4** Capture remaining dictated requirements (ongoing — update principles/architecture as they arrive)
- [x] **0.5** Harden technology decisions (C++/NDK core, Kotlin shell, Vulkan-first, glTF import) into an ADR (architecture decision record) folder — `docs/adr/0001`
- [~] **0.6** Repository scaffolding: module layout matching the layer diagram, build system (Gradle + CMake), CI that builds an APK and runs native tests — *host build + tests + runner in CI; Android CI job pending SDK validation*

## Phase 1 — Application layer + Core foundation

*Goal: an Android app that opens a rendering surface, survives the full lifecycle, and has the engine's memory/job/I-O substrate underneath.*

- [~] **1.1** Android application shell (Kotlin): activity, surface hosting, engine bootstrap over JNI — *source complete (`app/`), unverified on device/SDK*
- [~] **1.2** Lifecycle translation: pause/resume, surface loss/recreate, config change, process-death-safe shutdown hooks — *engine-side handling implemented + host-tested; device verification pending*
- [~] **1.3** Core memory system: global + per-system budgets, arena/pool/ring allocators, live usage reporting (P1 enforcement point) — *budgets, arena, pool done + tested; ring buffer pending*
- [~] **1.4** Job system with lanes (simulation / streaming I/O / decode / render) and main-thread frame orchestration — *v0 done + tested; allocation-free job structs pending (replaces std::function)*
- [ ] **1.5** Async priority I/O API over Android storage (app-private files + packaged assets)
- [x] **1.6** Math library (vectors, quaternions, transforms, AABBs) shaped for later data layouts
- [~] **1.7** Diagnostics v0: logging, frame timer, memory-budget dashboard overlayed as debug text — *logging + budget report done; on-screen overlay needs Phase 2*
- [~] **1.8** Raw input capture: touch events timestamped and queued into the engine — *captured at the JNI boundary; engine-side queue pending*

**Exit criteria:** app runs a colored-clear frame loop at stable 60fps through every lifecycle event, with live memory-budget readout and zero steady-state allocations in the loop.

## Phase 2 — Graphics engine v1

*Goal: the single 3D pipeline, drawing imported models within explicit GPU budgets.*

- [ ] **2.1** Vulkan device/swapchain bring-up integrated with the surface lifecycle
- [ ] **2.2** Frame graph v0: forward pass + UI overlay pass slots
- [ ] **2.3** GPU resource manager: budgeted upload/evict for meshes and textures (P1 on the GPU side)
- [ ] **2.4** Engine runtime asset format v1: quantized vertex streams, compressed textures, LOD chain container (design doc first, then implementation)
- [ ] **2.5** glTF import tool producing the runtime format (native import — P5 prerequisite)
- [ ] **2.6** Material model + engine shaders: basic lit opaque, alpha-tested, unlit
- [ ] **2.7** Camera, transforms, frustum culling, LOD selection by distance
- [ ] **2.8** Placeholder rendering path: engine-generated primitive volumes (box/cylinder/capsule) with distinct visual treatment + optional label (virtual-model prerequisite)

**Exit criteria:** an imported glTF scene renders with LODs under a fixed GPU memory budget; a declared-size placeholder box renders through the same path.

## Phase 3 — Game framework v1

*Goal: what a "game" is — loop, world, entities, controls — with the template project.*

- [ ] **3.1** Fixed-step game loop with render interpolation, orchestrating the Phase 1 lanes
- [ ] **3.2** Entity/component model: compact data-oriented storage, stable IDs, pooling (P1)
- [ ] **3.3** World model: continuous 3D space partitioned into chunk regions (streaming-ready even before streaming lands)
- [ ] **3.4** Asset registry: stable asset IDs, reference counting, resolve-by-ID with "not resident" as a normal answer (P2 prerequisite)
- [ ] **3.5** Input pipeline → control schemes → gameplay intents; touch-native default scheme (virtual stick + camera drag + action buttons, logic only — widgets arrive in Phase 5)
- [ ] **3.6** Game template v0: new-game scaffold with working world, controls, camera, and a placed mix of real + placeholder models
- [ ] **3.7** Third-person/first-person camera controllers for the default scheme

**Exit criteria:** the template game: walk a character around a small hand-built world of real and placeholder models on a touchscreen.

## Phase 4 — World streaming

*Goal: P2 realized — giant world on disk, small bounded working set in memory.*

- [ ] **4.1** Chunked world container format: seek-friendly, partial-read layout for terrain/static geometry, entity placements, asset references (design doc first)
- [ ] **4.2** World build tool: bake an authored world into the chunk container
- [ ] **4.3** Residency manager: resident/loading/cold states, movement-driven priority queue, budget-driven eviction
- [ ] **4.4** Asset streaming with LOD/mip granularity tied into the GPU resource manager
- [ ] **4.5** Frame-never-blocks guarantee: deferral/LOD fallback paths in renderer and gameplay queries against cold chunks
- [ ] **4.6** Streaming diagnostics: chunk-state visualizer, I/O and budget dashboards
- [ ] **4.7** Scale test: synthetic multi-GB world traversed continuously within a fixed memory budget on a mid-range device

**Exit criteria:** the scale test passes — traversal with no loading screens, no frame stalls, memory flat at the configured budget.

## Phase 5 — UI system with built-in designs

*Goal: P6 realized — the engine's own original UI, rendered by the engine.*

- [ ] **5.1** Widget tree: layout, styling/theming, navigation, low-allocation updates
- [ ] **5.2** UI renderer: batched geometry through the overlay pass; font atlas + text shaping
- [ ] **5.3** Original design language: define the engine's visual identity (spec + theme tokens) — an original design, not a platform clone
- [ ] **5.4** Built-in widget library in that design: buttons, lists, sliders, toggles, dialogs, grids
- [ ] **5.5** Built-in screen kit: boot screen, main menu, pause menu, settings, save-slot picker, HUD elements
- [ ] **5.6** Virtual gameplay control widgets: sticks, buttons, gesture zones bound to Phase 3 control schemes
- [ ] **5.7** Input routing: UI-first claim, fall-through to gameplay
- [ ] **5.8** Wire the full screen kit + controls into the game template

**Exit criteria:** the template game boots into an engine-designed menu, plays with on-screen controls, and pauses into an engine-designed pause screen.

## Phase 6 — Saving & loading

*Goal: P7 realized — delta-based, atomic, versioned persistence as an engine service.*

- [ ] **6.1** Save format design doc: per-chunk deltas, schema versioning, checksums
- [ ] **6.2** Delta recording in the entity/world model (mutations tracked against shipped content)
- [ ] **6.3** Atomic save writer: snapshot on simulation boundary, async write, verified swap
- [ ] **6.4** Load path: chunk stream + delta application (integrates with Phase 4 residency)
- [ ] **6.5** Save slots + metadata (thumbnail, playtime, timestamp) surfaced in the Phase 5 save-slot screen
- [ ] **6.6** Version migration framework + kill-test suite (process death injected mid-save must never corrupt)

**Exit criteria:** kill-test suite passes; template game saves/loads world changes across app restarts and process death.

## Phase 7 — Virtual models & fulfillment pipeline

*Goal: P5 fully realized — author worlds asset-free, fulfill assets later via external agents.*

- [ ] **7.1** Virtual model asset type: ID, proportions, shape hint, structured description, gameplay metadata
- [ ] **7.2** Authoring API: declare + place virtual models in the world exactly like real models
- [ ] **7.3** Placeholder integration: correct size/position/orientation via the Phase 2 placeholder path; collidable/interactable per metadata
- [ ] **7.4** Description schema designed for external-agent consumption (what/style/materials/features), with validation
- [ ] **7.5** Manifest export: list of unfulfilled virtual models (ID + proportions + description) for external agents
- [ ] **7.6** Fulfillment: import a produced model under a virtual ID → replaces placeholder everywhere, zero scene edits; proportion-mismatch warnings
- [ ] **7.7** Round-trip demo: author placeholder world → export manifest → fulfill with generated models → same world, real assets

**Exit criteria:** the round-trip demo works end-to-end with no scene changes between placeholder and fulfilled states.

## Phase 8 — Character system

*Goal: P9 realized — one character entity for players and NPCs, the humanoid template body with data-driven variants, and the wearable fitting mechanism. Design: [`CHARACTERS.md`](CHARACTERS.md).*

**Universal character base:**

- [ ] **8.1** `Character` entity on the Phase 3 entity model: identity, body definition reference, compact state (P1)
- [ ] **8.2** Controller abstraction: `PlayerController` (input intents) and `AIController` (AI intents) driving one intent interface; runtime controller swap (P9)
- [ ] **8.3** Universal locomotion: movement intents → world-resolved motion with per-character parameters
- [ ] **8.4** Mortality: health, damage intake, death consequences (loot drop, corpse, despawn)
- [ ] **8.5** Faction system: enemy/ally/neutral stances between factions, per-character overrides; queried by AI, targeting, UI
- [ ] **8.6** Inventory on every character: item stacks, capacity rules, pickup/drop
- [ ] **8.7** Equipment slot machinery: data-defined slot sets per body definition; layered wearable slots + `held_main`/`held_off` item slots
- [ ] **8.8** Basic AI v1: data-defined state machine (idle/wander/patrol/chase/attack/flee/return) driven by perception + factions; `ai_profile` asset; LOD-scheduled ticking (expansion held for dictation)
- [ ] **8.9** Character persistence: inventory/equipment/health/faction state in the Phase 6 save deltas; character streaming with chunks

**Skeletal animation (prerequisite, graphics engine):**

- [ ] **8.10** Skinned mesh rendering + skeleton runtime (pose evaluation, GPU skinning) within Phase 2 budgets
- [ ] **8.11** Animation playback: clips, speed-blended locomotion blending, import via glTF path

**Humanoid:**

- [ ] **8.12** Template base body: import the canonical humanoid (mesh, UV layout, body-part segmentation, skeleton binding)
- [ ] **8.13** Variant data format (`humanoid_variant`) + applier: skin texture, size, width, height, shoulders, chest, legs, feet, face + facial-feature sub-schema — via bone-proportion scaling + morph deltas
- [ ] **8.14** Default animation set on the canonical rig: idle + several walk/run variations, speed-blended; valid across all variants by construction
- [ ] **8.15** Wearable asset type: slot, layer, covered regions, thickness profile, template-authored mesh, variant-response data, opaque gameplay data
- [ ] **8.16** Wearable fitting mechanism: apply body's variant transformations to worn meshes; covered-skin masking (no clipping); single skinning path for body + wearables
- [ ] **8.17** Wearable layering: base/mid/outer stack per slot, thickness-offset fitting across layers, outward-cascading masking (hidden inner geometry costs nothing)
- [ ] **8.18** Hairstyles as wearables through the `head_hair` slot, including helmet-coverage interaction
- [ ] **8.19** Held items: grip types (one-handed/two-handed/versatile), dual wield via `held_main`+`held_off`, item-defined grip points, drawn/sheathed states with sheath attachment points and default draw/sheath animations
- [ ] **8.20** Basic facial expressions: built-in morph-preset set (neutral/happy/sad/angry/surprised/afraid/pain), blendable over any face variant, expression API + AI/gameplay triggers
- [ ] **8.21** Virtual-model wearables & held items: placeholder on the body/in hand from proportions + description (P5)
- [ ] **8.22** Template-game update: player and NPCs as the same humanoid character with different controllers; NPC with layered outfit + sheathed weapon + basic AI; variant showcase

**Exit criteria:** the template game shows player and NPCs as identical humanoid characters (controller being the only difference); a dozen visibly distinct humanoids from one template body via variant files; a layered outfit (base + clothing + armor) + one hairstyle fitting all of them seamlessly while animating; an NPC that patrols, spots an enemy by faction, draws its sheathed weapon, and reacts with a facial expression — all from data files.

## Phase 9 and beyond — held for further dictation

Deliberately not planned yet; known candidates awaiting direction:

- AI expansion (behavior model beyond the v1 state machine, schedules, group behavior) — v1 lands in 8.8
- Advanced facial animation (lip-sync, emotes, gaze) — basic set lands in 8.20
- Audio system
- Physics & collision beyond basic queries
- World-authoring/editor tooling
- Scripting / game-logic authoring model
- Networking / multiplayer
- Asset-pack delivery & app-size strategy for giant games
- Performance certification tiers across device classes

---

*This breakdown will be revised as the remaining requirements are dictated. Tasks are appended or reshaped in place; completed tasks are never deleted.*
