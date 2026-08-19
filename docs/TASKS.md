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
- [x] **0.6** Repository scaffolding: module layout matching the layer diagram, build system (Gradle + CMake), CI that builds an APK and runs native tests — *CI: host build+tests, APK assembly, arm64 tests under QEMU; `scripts/setup-android-sdk.sh` + `scripts/verify.sh` make any environment self-sufficient*

## Phase 1 — Application layer + Core foundation

*Goal: an Android app that opens a rendering surface, survives the full lifecycle, and has the engine's memory/job/I-O substrate underneath.*

- [~] **1.1** Android application shell (Kotlin): activity, surface hosting, engine bootstrap over JNI — *APK builds (arm64, NDK 27); engine logic verified on the shipped ABI via QEMU; on-device runtime check pending real hardware*
- [~] **1.2** Lifecycle translation: pause/resume, surface loss/recreate, config change, process-death-safe shutdown hooks — *engine-side handling implemented + tested on host and arm64; device verification pending*
- [x] **1.3** Core memory system: global + per-system budgets, arena/pool/ring allocators, live usage reporting (P1 enforcement point)
- [x] **1.4** Job system with lanes (simulation / streaming I/O / decode / render) and main-thread frame orchestration — *allocation-free job structs, fixed-capacity lanes that refuse when full*
- [~] **1.5** Async priority I/O API over Android storage (app-private files + packaged assets) — *priority-ordered async reads over real paths (app-private + extracted asset packs) done + tested, pause/resume tied to lifecycle; in-APK AAsset container reads pending*
- [x] **1.6** Math library (vectors, quaternions, transforms, AABBs) shaped for later data layouts
- [~] **1.7** Diagnostics v0: logging, frame timer, memory-budget dashboard overlayed as debug text — *logging + budget report done; on-screen overlay needs Phase 2*
- [x] **1.8** Raw input capture: touch events timestamped and queued into the engine — *SPSC ring from platform thread to simulation, drained per tick, overflow counted; JNI wired*

**Exit criteria:** app runs a colored-clear frame loop at stable 60fps through every lifecycle event, with live memory-budget readout and zero steady-state allocations in the loop.

## Phase 2 — Graphics engine v1

*Goal: the single 3D pipeline, drawing imported models within explicit GPU budgets.*

- [~] **2.1** Vulkan device/swapchain bring-up integrated with the surface lifecycle — *instance/device/queue up, GPU memory budgeted through BudgetRegistry (refuse-at-cap), headless clear+readback verified pixel-exact on llvmpipe (`tools/vk_smoke`); surface/swapchain integration with the app module pending*
- [~] **2.2** Frame graph v0: forward pass + UI overlay pass slots — *forward pass with offscreen color+depth target running headlessly; formal frame-graph structure + UI overlay slot pending*
- [~] **2.3** GPU resource manager: budgeted upload/evict for meshes and textures (P1 on the GPU side) — *budgeted mesh upload/destroy running (refuse-at-cap, verified back-to-zero); eviction + textures pending*
- [~] **2.4** Engine runtime asset format v1: quantized vertex streams, compressed textures, LOD chain container — *v1 `.mgemesh` (LOD chain container, per-LOD bounds) implemented + round-trip tested; quantization/textures deferred to v2, see ADR 0002*
- [~] **2.5** glTF import tool producing the runtime format (native import — P5 prerequisite) — *`mge_asset_import` bakes glTF meshes (node transforms, normals, index merge) to `.mgemesh`, tested; materials/textures/LOD generation pending*
- [~] **2.6** Material model + engine shaders: basic lit opaque, alpha-tested, unlit — *basic lit opaque + placeholder materials running (embedded SPIR-V, GLSL→header toolchain); alpha-tested/unlit/textures pending*
- [x] **2.7** Camera, transforms, frustum culling, LOD selection by distance — *unit-tested + proven live in the scene render (behind-camera culled, far tower at LOD1)*
- [x] **2.8** Placeholder rendering path: engine-generated primitive volumes (box/cylinder/capsule) with distinct visual treatment — *hatched amber treatment rendering at declared proportions; text label deferred to the UI overlay*

**Exit criteria:** an imported glTF scene renders with LODs under a fixed GPU memory budget; a declared-size placeholder box renders through the same path. — *Met headlessly by `tools/vk_scene` (imported glTF + LOD towers + placeholders, GPU budget verified back to zero); on-device rendering still requires the 2.1 surface half.*

## Phase 3 — Game framework v1

*Goal: what a "game" is — loop, world, entities, controls — with the template project.*

- [x] **3.1** Fixed-step game loop with render interpolation, orchestrating the Phase 1 lanes — *Engine::tick: input drain → intents → player control → world step; renderAlpha() interpolates prev/current sim state*
- [~] **3.2** Entity/component model: compact data-oriented storage, stable IDs, pooling (P1) — *index+generation ids, fixed-capacity refuse-at-cap registry, parallel component arrays; archetype/pooled component growth later*
- [~] **3.3** World model: continuous 3D space partitioned into chunk regions — *components, chunk-coordinate addressing, movement step with prev-state; per-chunk entity lists arrive with streaming (Phase 4)*
- [~] **3.4** Asset registry: stable asset IDs, resolve-by-ID with "not resident" as a normal answer — *FNV-1a ids, mesh + virtual-model kinds, GPU residency cache resolving to null when absent; reference counting pending Phase 4 eviction*
- [x] **3.5** Input pipeline → control schemes → gameplay intents; touch-native default scheme (virtual stick + camera drag + tap actions, logic only — widgets arrive in Phase 5)
- [~] **3.6** Game template v0: new-game scaffold with working world, controls, camera, and a placed mix of real + placeholder models — *`tools/template_game`: scripted-touch walkthrough with captures, runs in CI; save wiring and on-device run pending*
- [x] **3.7** Third-person/first-person camera controllers for the default scheme

**Exit criteria:** the template game: walk a character around a small hand-built world of real and placeholder models on a touchscreen. — *Met headlessly: the full stack (touch events → control scheme → player → world → interpolated third-person render) walks the character through the hamlet in `tools/template_game`; the literal touchscreen needs the 2.1 surface half.*

## Phase 4 — World streaming

*Goal: P2 realized — giant world on disk, small bounded working set in memory.*

- [x] **4.1** Chunked world container format: seek-friendly, partial-read layout for terrain/static geometry, entity placements, asset references — *`.mgeworld` v1 (ADR 0003): index tables + byte-range payloads for priority AsyncIO; virtual models ship in the container; round-trip tested*
- [x] **4.2** World build tool: bake an authored world into the chunk container — *WorldBaker API (assets, virtuals, placements, interior cells); GUI authoring is later tooling*
- [x] **4.3** Residency manager: resident/loading/cold states, movement-driven priority queue (Critical/High/Normal rings), budget-driven refuse + eviction, amortized instantiation
- [~] **4.4** Asset streaming with LOD/mip granularity tied into the GPU resource manager — *whole-asset streaming with cross-chunk refcounts + registry unload running; per-LOD byte ranges are format v2 (ADR 0003)*
- [x] **4.5** Frame-never-blocks guarantee: update() is poll-and-issue only (max 2.0 ms observed on the giant-world run); renderer + render collection already skip non-resident assets
- [~] **4.6** Streaming diagnostics: stats + ASCII chunk-state map around the player; on-screen overlay needs the UI phase
- [~] **4.7** Interior cells (P10): interior cells with door anchors, approach-prediction prefetch, evict-on-leave — *running and proven in the scale test; exterior shells, district-granularity cities, and doorway-delay instrumentation pending*
- [~] **4.8** Scale test: synthetic multi-GB world traversed continuously within a fixed memory budget — *passed in the dev environment: 1.77 GiB world, 1.9 km traversal, 6.9 MiB peak streaming memory (64 MiB cap), 0 flow breaks, interior ready before its door; runs (small) in CI on every push; mid-range device run pending*

**Exit criteria:** the scale test passes — traversal with building and city entries, no loading screens, no frame stalls, memory flat at the configured budget. — *Met headlessly at 1.77 GiB scale; device confirmation pending.*

## Phase 5 — UI system with built-in designs

*Goal: P6 realized — the engine's own original UI, rendered by the engine.*

- [~] **5.1** Widget tree: layout, styling/theming, navigation, direction-aware (RTL mirroring free per-widget, P11) — *immediate-build widgets over retained interaction state, fixed-capacity/allocation-free per frame; full retained tree + navigation stack pending*
- [~] **5.2** UI renderer: batched geometry through the overlay pass; font atlas + text with bidi (Hebrew first-class) — *quad batch + alpha-blended overlay pipeline + atlas texture running; v1 bidi run reordering (full UAX#9 later); per-script font fallback pending*
- [~] **5.3** Codex design language (medieval book-and-paper, owner verdict) — *v1 tokens + styled widget set rendering (parchment/double-rule/wax seals); full spec doc + owner approval of the rendered look pending*
- [~] **5.4** Built-in widget library: buttons, sliders, toggles, grids running; lists and dialogs pending
- [~] **5.5** Built-in screen kit — *main menu, HUD (health/compass/item slot), inventory running; boot, pause, settings, save-slot picker pending*
- [~] **5.6** Virtual gameplay control widgets — *stick ring + wax-seal buttons drawn over the Phase 3 scheme state; gesture zones + full widget/scheme binding pending*
- [x] **5.7** Input routing: UI-first claim with fall-through to gameplay — tested (tap on button consumed, tap on world falls through)
- [~] **5.8** Localization: string keys default, runtime language switch incl. RTL relayout, ships English + Hebrew — *language packs from data files pending*
- [~] **5.9** Data binding — collection views: registered collections by id rendering as live grids with selection — *drag/move/context interactions pending*
- [~] **5.10** Data binding — live-object views: 3D object rendered into a UI rect with its own camera (player character in the inventory) — *worn-equipment composition arrives with Phase 8*
- [~] **5.11** Wire the kit into the template — *ui_demo composes menu→HUD→inventory over the live scene with bindings; merge into template_game pending*

**Exit criteria:** the template game boots into an engine-designed menu, plays with on-screen controls, and pauses into an engine-designed pause screen.

## Phase 6 — Saving & loading

*Goal: P7 realized — delta-based, atomic, versioned persistence as an engine service.*

- [x] **6.1** Save format design doc: per-chunk deltas, schema versioning, checksums — *ADR 0004; `.mgesave` v1 implemented to it*
- [~] **6.2** Delta recording (mutations tracked against shipped content) — *WorldDeltaLog + streaming mutation APIs (remove/move/spawnDynamic) recording removed/moved placements and dynamic spawns; automatic tracking of arbitrary component mutations arrives with Phase 8 gameplay state*
- [~] **6.3** Atomic save writer: snapshot serialize, tmp + fsync + rename swap, checksum — *kill-test verified at four injected death points; async write on the I/O lane deferred (saves are delta-small, ADR 0004)*
- [x] **6.4** Load path: chunk stream + delta application — *deltas apply lazily at chunk instantiation (skip removed, apply moved, add spawns); full cycle tested through a real save file across two streaming sessions*
- [~] **6.5** Save slots + metadata surfaced in the save-slot screen — *slots, timestamps, playtime, validity + Codex "Chronicles" picker rendering from real files; thumbnails pending*
- [x] **6.6** Version migration framework + kill-test suite — *v1→v2 migration chain worked example, future-version rejection, checksum gate, process-death injection: previous save always intact*

**Exit criteria:** kill-test suite passes; template game saves/loads world changes across app restarts and process death. — *Kill-test suite passes; the save→fresh-session→reapply cycle is proven in tests; wiring save/load into the template game's flow is the remaining step.*

## Phase 7 — Virtual models & fulfillment pipeline

*Goal: P5 fully realized — author worlds asset-free, fulfill assets later via external agents.*

- [x] **7.1** Virtual model asset type: ID, proportions, shape hint, structured description, gameplay metadata — *description + style + materials + features + collidable; richer gameplay hooks (interactable) arrive with Phase 8*
- [x] **7.2** Authoring API: declare + place virtual models in the world exactly like real models — *registry + world-baker paths both working*
- [~] **7.3** Placeholder integration: correct size/position/orientation via the placeholder path — *rendering verified; collision pending physics queries*
- [x] **7.4** Description schema for external-agent consumption (what/style/materials/features) with validation — *validateVirtualModelDesc gates authoring*
- [x] **7.5** Manifest export: JSON manifest of unfulfilled models (id, delivery filename, proportions, shape, structured description) — *re-export shrinks as models are fulfilled*
- [x] **7.6** Fulfillment: baked model delivered under a virtual ID replaces the placeholder everywhere, zero scene edits; proportion-mismatch warnings — *fulfillFromDirectory ingests `<id>.mgemesh` deliveries; live GPU-cache invalidation is app-side; shipped-world patch packs remain a format-v2 item (ADR 0003)*
- [x] **7.7** Round-trip demo: author placeholder world → export manifest → fulfill with agent-built models → same world, real assets — *`tools/fulfill_demo`, before/after renders, runs in CI*

**Exit criteria:** the round-trip demo works end-to-end with no scene changes between placeholder and fulfilled states. — *Met: same placement list renders both states; only the resolved assets differ.*

## Phase 8 — Character system

*Goal: P9 realized — one character entity for players and NPCs, the humanoid template body with data-driven variants, and the wearable fitting mechanism. Design: [`CHARACTERS.md`](CHARACTERS.md).*

**Universal character base:**

- [x] **8.1** `Character` entity on the Phase 3 entity model: identity, body definition reference, compact state (P1) — *`CharacterComponent` in a fixed-capacity `CharacterSystem` (caps refuse)*
- [~] **8.2** Controller abstraction: `PlayerController` (input intents) and `AIController` (AI intents) driving one intent interface; runtime controller swap (P9) — *`ControllerKind` on the component: swap is one assignment, AI attach/detach sets it; a formal shared intent struct between player controls and AI is still to come*
- [x] **8.3** Universal locomotion: movement intents → world-resolved motion with per-character parameters — *AI and player both steer the same `MovementComponent`; per-profile speeds*
- [x] **8.4** Mortality: health, damage intake, death consequences (loot drop, corpse, despawn) — *death drops inventory + equipment as loot, halts the body; corpse visuals/despawn timers with gameplay layer*
- [x] **8.5** Faction system: enemy/ally/neutral stances between factions, per-character overrides; queried by AI, targeting, UI — *symmetric `FactionTable`, same-faction = ally; per-character overrides still open*
- [x] **8.6** Inventory on every character: item stacks, capacity rules, pickup/drop — *the Phase 5 `ItemCollection` on every `CharacterComponent` (P9: player and NPC identical)*
- [x] **8.7** Equipment slot machinery: data-defined slot sets per body definition; layered wearable slots + `held_main`/`held_off` item slots — *7 slots incl. `HeldMain`/`HeldOff`, equip-swap, layer field, sheathed flag*
- [x] **8.8** Basic AI v1: data-defined state machine (idle/wander/patrol/chase/attack/flee/return) driven by perception + factions; `ai_profile` asset; LOD-scheduled ticking (expansion held for dictation) — *all 7 states, deterministic per-agent RNG, tested chase→kill→return; LOD ticking hooks in when streaming-driven activity lands*
- [ ] **8.9** Character persistence: inventory/equipment/health/faction state in the Phase 6 save deltas; character streaming with chunks

**Skeletal animation (prerequisite, graphics engine):**

- [~] **8.10** Skinned mesh rendering + skeleton runtime (pose evaluation, GPU skinning) within Phase 2 budgets — *17-joint canonical rig + `evaluatePose` live; v1 renders rigid parts per joint (no vertex skinning needed for the parametric body); smooth GPU skinning arrives with the imported artist template*
- [~] **8.11** Animation playback: clips, speed-blended locomotion blending, import via glTF path — *procedural idle/walk/run with speed blending and distance-driven phase (no foot slide); authored clip import via glTF still open*

**Humanoid:**

- [~] **8.12** Template base body: import the canonical humanoid (mesh, UV layout, body-part segmentation, skeleton binding) — *v1 template is PARAMETRIC: body parts generated from variant proportions, rigidly bound to the canonical rig; the imported artist body replaces the generator behind the same interfaces*
- [~] **8.13** Variant data format (`humanoid_variant`) + applier: skin texture, size, width, height, shoulders, chest, legs, feet, face + facial-feature sub-schema — *height/shoulders/hips/leg+arm ratios/bulk/head scale/skin color drive rig AND body; face + skin-texture sub-schema awaits real face geometry and the texture pipeline*
- [x] **8.14** Default animation set on the canonical rig: idle + several walk/run variations, speed-blended; valid across all variants by construction — *`LocomotionAnimator`: idle sway ↔ walk ↔ run, distance-driven phase; retargets to every variant because all share the rig*
- [~] **8.15** Wearable asset type: slot, layer, covered regions, thickness profile, template-authored mesh, variant-response data, opaque gameplay data — *kind/layer/cover-bits/color live; template-authored meshes replace the parametric generators with the artist body*
- [x] **8.16** Wearable fitting mechanism: apply body's variant transformations to worn meshes; covered-skin masking (no clipping); single skinning path for body + wearables — *garments generated from the SAME variant proportions + layer thickness: fit holds for every variant by construction; covered body parts are not emitted*
- [x] **8.17** Wearable layering: base/mid/outer stack per slot, thickness-offset fitting across layers, outward-cascading masking (hidden inner geometry costs nothing) — *per-layer thickness offsets; armor-over-tunic tested to enclose*
- [~] **8.18** Hairstyles as wearables through the `head_hair` slot, including helmet-coverage interaction — *short + long hair as wearables on the head; helmet coverage (scalp masking) wired but no helmet wearable yet*
- [~] **8.19** Held items: grip types (one-handed/two-handed/versatile), dual wield via `held_main`+`held_off`, item-defined grip points, drawn/sheathed states with sheath attachment points and default draw/sheath animations — *sword renders in `held_main` or sheathed on the back; grip types, dual wield, and draw/sheath animations still open*
- [ ] **8.20** Basic facial expressions: built-in morph-preset set (neutral/happy/sad/angry/surprised/afraid/pain), blendable over any face variant, expression API + AI/gameplay triggers — *deferred until the face has real geometry (parametric v1 head is featureless); lands with the imported template body*
- [ ] **8.21** Virtual-model wearables & held items: placeholder on the body/in hand from proportions + description (P5)
- [~] **8.22** Template-game update: player and NPCs as the same humanoid character with different controllers; NPC with layered outfit + sheathed weapon + basic AI; variant showcase — *`tools/humanoid_demo`: variant lineup, walk-cycle strip, dressed lineup (layered armor, hair, drawn + sheathed swords), and an AI hamlet with wanderers/patroller/fleeing villager animated by their real velocities; integrating humanoid visuals into `template_game`'s streaming walkthrough still open*

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
