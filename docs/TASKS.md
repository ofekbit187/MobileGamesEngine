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

- [x] **2.1** Vulkan device/swapchain bring-up integrated with the surface lifecycle — *instance/device/queue up, GPU memory budgeted through BudgetRegistry (refuse-at-cap), headless clear+readback verified pixel-exact on llvmpipe (`tools/vk_smoke`); the on-device half is now a REAL SWAPCHAIN: `Swapchain` acquires, blits the finished pass (driver-scaled, no CPU readback) and presents, rebuilding on rotate/resize/out-of-date; the Android `VkSurfaceKHR` is created in the app module (P3). Rendering straight into swapchain images (no blit) is a later optimization, not a gap*
- [~] **2.2** Frame graph v0: forward pass + UI overlay pass slots — *forward pass with offscreen color+depth target running headlessly; formal frame-graph structure + UI overlay slot pending*
- [~] **2.3** GPU resource manager: budgeted upload/evict for meshes and textures (P1 on the GPU side) — *budgeted mesh AND texture upload/destroy running. Textures charge their **own** registered budget (`gpu.textures`, 64 MiB mid-tier default per [TEXTURING §5](TEXTURING.md)) so a texture leak cannot hide inside geometry headroom; a 512² sheet against a 64 KiB cap is refused, and both budgets return to 0 B after teardown (`mge_vk_texture`). **Eviction is still missing** — nothing drops a resident texture under pressure, so the residency half of streaming-by-mip is unbuilt*
- [~] **2.4** Engine runtime asset format v1: quantized vertex streams, compressed textures, LOD chain container — *`.mgemesh` **v2**: the vertex now carries a float UV (32 B), because static world geometry tiles past the unit square and a normalized chart cannot say so; v1 files still load, with no UV. **`.mgetex` v1 lands**: format-tagged, colour-space-tagged, each mip its own byte range so one level streams without the rest (mip 4 of a 512² sheet is 0.3% of the file), malformed chains refused not clamped. Format enum covers the ASTC and ETC2 shipping packs, BC, and an uncompressed reference pack. **Vertex quantization still deferred**, and no ASTC/ETC2 ENCODER exists yet — the container carries those formats, nothing writes them (see 2.6)*
- [~] **2.5** glTF import tool producing the runtime format (native import — P5 prerequisite) — *`mge_asset_import` bakes glTF meshes (node transforms, normals, index merge) to `.mgemesh`, tested; materials/textures/LOD generation pending*
- [~] **2.6** Material model + engine shaders: basic lit opaque, alpha-tested, unlit — *lit opaque is now **textured**: at most two fetches (albedo sRGB-typed + optional packed AO/roughness/mask in the fixed ORM order), per-material constants when a map is absent, one 1×1-white default so textured and untextured share one pipeline, bilinear+mips with REPEAT addressing, per-material UV scale over the primitives' metre-based UVs. Measured by `mge_vk_texture`: sampler addressing within 0.0020 of a UV unit, sRGB linearised on fetch (188 → 128, un-linearised would be 188), mips converge a minified checker to spread 0.0000. **Still pending: alpha-tested and unlit materials, normal maps, and the ASTC/ETC2 baker** — every shipped texture is supposed to be block-compressed and nothing encodes those yet*
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
- [~] **5.2** UI renderer: batched geometry through the overlay pass; font atlas + text with bidi (Hebrew first-class) — *quad batch + alpha-blended overlay pipeline + atlas texture running; the default face (Liberation Serif, OFL — Latin + Hebrew) is now ENGINE-EMBEDDED (`FontAtlas::bakeEmbedded`), no host font path on device or off; v1 bidi run reordering (full UAX#9 later); per-script font fallback pending*
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
- [x] **8.2** Controller abstraction: `PlayerController` (input intents) and `AIController` (AI intents) driving one intent interface; runtime controller swap (P9) — *`CharacterIntent` + `applyIntent`: the engine's player controller and the AI system produce the same intent and share one applier; swap stays one assignment*
- [x] **8.3** Universal locomotion: movement intents → world-resolved motion with per-character parameters — *AI and player both steer the same `MovementComponent`; per-profile speeds*
- [x] **8.4** Mortality: health, damage intake, death consequences (loot drop, corpse, despawn) — *death drops inventory + equipment as loot, halts the body; corpse visuals/despawn timers with gameplay layer*
- [x] **8.5** Faction system: enemy/ally/neutral stances between factions, per-character overrides; queried by AI, targeting, UI — *symmetric `FactionTable`, same-faction = ally; per-character overrides still open*
- [x] **8.6** Inventory on every character: item stacks, capacity rules, pickup/drop — *the Phase 5 `ItemCollection` on every `CharacterComponent` (P9: player and NPC identical)*
- [x] **8.7** Equipment slot machinery: data-defined slot sets per body definition; layered wearable slots + `held_main`/`held_off` item slots — *7 slots incl. `HeldMain`/`HeldOff`, equip-swap, layer field, sheathed flag*
- [x] **8.8** Basic AI v1: data-defined state machine (idle/wander/patrol/chase/attack/flee/return) driven by perception + factions; `ai_profile` asset; LOD-scheduled ticking (expansion held for dictation) — *all 7 states, deterministic per-agent RNG, tested chase→kill→return; LOD ticking hooks in when streaming-driven activity lands*
- [x] **8.9** Character persistence: inventory/equipment/health/faction state in the Phase 6 save deltas; character streaming with chunks — *save schema v3 (`SavedCharacter` records, migration chain v1→v2→v3); chunk eviction parks persistent state via `pruneDead`, the game re-associates on reload by `persistentId`*

**Skeletal animation (prerequisite, graphics engine):**

- [x] **8.10** Skinned mesh rendering + skeleton runtime (pose evaluation, GPU skinning) within Phase 2 budgets — *17-joint rig + `evaluatePose`; GPU skinning is live and pixel-verified against the CPU reference (`skinned.vert`, dynamic-offset palette UBO, `tools/skin_test`). **Shape now rides the GPU too**: the mesh's sparse morph deltas are re-indexed per vertex at upload into one shared storage buffer (set 1, 12 B per moved vertex — what the file spends), and each character's 15 weights ride the palette slot, so `skinned.vert` applies shape in bind space BEFORE the palette, the order ADR 0009 fixed. Verified with 15 NON-ZERO weights, bare and dressed: 0.000 mean channel difference against `skinMesh()`, and a same-palette weights-off frame differs by 2.277/1.400 so the shape pass cannot silently no-op. A crowd of 12 distinct faces and builds draws from ONE mesh + ONE delta buffer at 1088 B palette + 60 B shape each. Garments still follow proportions only — they carry no targets of their own (ADR 0009, task 13.4)*
- [~] **8.11** Animation playback: clips, speed-blended locomotion blending, import via glTF path — *procedural idle/walk/run with speed blending and distance-driven phase (no foot slide); authored clip import via glTF still open*

**Humanoid:**

- [x] **8.12** Template base body: import the canonical humanoid (mesh, UV layout, body-part segmentation, skeleton binding) — *v3 (ADR 0007, revised): the body is now Blender Studio's **CC0 human base mesh**, imported UNMODIFIED — `tools/model/humanoid_template.py` only places it, fits the canonical 17-joint rig to its own anatomy (fit the rig to the mesh, never the mesh to the rig), skins it with pruned bone-heat weights (≤4 influences, 2.2 average), decimates LOD0/1/2 to 2200/1200/560 triangles and exports glTF, which `mge_asset_import --skinned` bakes to `.mgeskin`. Every LOD is watertight, consistently wound and correctly proportioned (7.3 heads). ONE 85 KB mesh serves every character; `tools/body_preview` renders it through the engine (sheet/variants/walk/dressed/LODs/close-ups) and `tests/test_body_mesh.cpp` gates topology, proportions, deformation, weights, budget and fit. Known gap: `BodyRegion::Face` is empty (the imported head is one shell), which costs nothing until a mask/visor wearable exists*
- [x] **8.13** Variant data format (`humanoid_variant`) + applier: skin texture, size, width, height, shoulders, chest, legs, feet, face + facial-feature sub-schema — *the variation scope is now complete on both mechanisms CHARACTERS.md §4.1 names ([ADR 0009](adr/0009-humanoid-variation-scope.md)). **Proportions** (height, shoulders, hips, leg/arm ratio, bulk, head, feet) are skeleton scaling through the palette, each with a documented range, and `height` now means sole-to-crown for every combination of the others. **Shape** is 15 morph targets on the shared mesh — body: chest/belly/seat/muscle/neck; face: skull/brow/cheeks/jawWidth/chin/noseLength/noseWidth/mouth/eyes/ears — authored parametrically from landmarks measured on the model (`tools/model/humanoid_morphs.py`), shipped as glTF morph targets, baked into `.mgeskin` v2 sparsely at 12 B per moved vertex. Cost: **42 KB once, 15 floats per character**. Out-of-scope variant files are clamped, never rejected. `tools/body_preview` renders every parameter at both extremes; 111 tests gate it. Skin texture still awaits the texture pipeline*
- [x] **8.14** Default animation set on the canonical rig: idle + several walk/run variations, speed-blended; valid across all variants by construction — *`LocomotionAnimator`: idle sway ↔ walk ↔ run, distance-driven phase; retargets to every variant because all share the rig*
- [~] **8.15** Wearable asset type: slot, layer, covered regions, thickness profile, template-authored mesh, variant-response data, opaque gameplay data — *garments are now **cut out of the body's own surface** in Blender (tunic 848, armour 868, trousers 776, boots 532, hair 184/444 tris), pushed out by their layer's thickness (4/9/17 mm) and solidified, so they inherit the body's skin weights and enclose it by construction; cached one copy per kind. Per-item variant-response data and data-file authoring still open*
- [x] **8.16** Wearable fitting mechanism: apply body's variant transformations to worn meshes; covered-skin masking (no clipping); single skinning path for body + wearables — *body and garments ride the SAME skinning palette, so fit holds for every variant arithmetically; masking drops the covered regions' triangle ranges from the shared mesh, so a dressed body draws fewer triangles than a bare one; it cannot leave a hole because each garment was cut from the very triangles its mask removes*
- [x] **8.17** Wearable layering: base/mid/outer stack per slot, thickness-offset fitting across layers, outward-cascading masking (hidden inner geometry costs nothing) — *per-layer thickness offsets; armor-over-tunic tested to enclose*
- [~] **8.18** Hairstyles as wearables through the `head_hair` slot, including helmet-coverage interaction — *short + long hair as wearables on the head; helmet coverage (scalp masking) wired but no helmet wearable yet*
- [~] **8.19** Held items: grip types (one-handed/two-handed/versatile), dual wield via `held_main`+`held_off`, item-defined grip points, drawn/sheathed states with sheath attachment points and default draw/sheath animations — *sword renders in `held_main` or sheathed on the back, and the AI draws on contact / sheathes when combat ends; grip types, dual wield, and draw/sheath transition animations still open*
- [~] **8.20** Basic facial expressions: built-in morph-preset set (neutral/happy/sad/angry/surprised/afraid/pain), blendable over any face variant, expression API + AI/gameplay triggers — *no longer blocked: the face now has geometry parameters and the morph machinery they ride (ADR 0009) is exactly what CHARACTERS.md §8 asks expressions to use. What remains is authoring the seven expression targets and the blend API on top of `morphWeights`*
- [ ] **8.21** Virtual-model wearables & held items: placeholder on the body/in hand from proportions + description (P5)
- [x] **8.22** Template-game update: player and NPCs as the same humanoid character with different controllers; NPC with layered outfit + sheathed weapon + basic AI; variant showcase — *`template_game` v1: player + patrolling guard (tunic-under-armor, sword sheathed on the back) + wandering villager are the same humanoid character with different controllers, animated from real velocities, and their state rides a real save file; `humanoid_demo` renders the twelve-variant lineup, walk strip, dressed lineup, and AI hamlet. NOTE: the DEVICE build now draws the v2 skinned template body GPU-skinned (shared body + garment meshes, masking by index range); `humanoid_demo` and `template_game` still use the v1 rigid part-body for their headless captures*

**Exit criteria:** the template game shows player and NPCs as identical humanoid characters (controller being the only difference); a dozen visibly distinct humanoids from one template body via variant files; a layered outfit (base + clothing + armor) + one hairstyle fitting all of them seamlessly while animating; an NPC that patrols, spots an enemy by faction, draws its sheathed weapon, and reacts with a facial expression — all from data files. — *Met except the facial-expression reaction (8.20): deliberately deferred with the imported artist face — the parametric v1 head has no facial geometry to morph. Everything else is running and tested, including AI draw-on-contact/sheathe-after.*

## Phase 9 — People (dictated)

*Goal: the person NPC kind — family trees as the generation unit, hereditary looks via DNA, names with provenance, status effects (skills/education included), occupation/residence/schedule stubs, and voiced text lines on the P5 fulfillment pattern. Design: [`PEOPLE.md`](PEOPLE.md). Open questions §7 await the owner's rulings before the affected tasks start.*

- [x] **9.1** Person identity layer on the humanoid character: `PersonId`, name refs, tree ref, relation links (mother/father/siblings/children/spouse) — *`PersonRecord`: relations by index, `personId` doubles as the save `persistentId`; children/sibling queries on the tree*
- [x] **9.2** Family-tree data format (`.mgetree`) + ADR 0005: compact person records (names, genomes, relations, identity), deterministic by seed, checksummed like every engine format — *~200 B/person cold*
- [x] **9.3** Names: culture name pools (`anglo` + `hebrew`, P11), first-name assignment, family-name derivation — *rulings applied: first names unique within the family (refuse on pool exhaustion, never duplicate); children always take the father's name; a woman takes her husband's name at marriage, birth name preserved*
- [x] **9.4** DNA: genome over the `HumanoidVariant` trait set (two haplotypes, blend + darker-dominant skin, bounded mutation), phenotype resolver → variant; children resemble parents by construction — *tested: child phenotype inside the parents' allele span; siblings differ; sex offsets*
- [x] **9.5** Tree generator: multi-generation trees (couples, married-in spouses, children, record-only dead ancestors) from a seed; sibling variety via recombination — *deterministic: same seed, same family, byte-identical*
- [x] **9.6** Status effects: storage, tags, magnitude, duration, stat-modifier hook queries (`sumMagnitude` over speed/health/skill tags); skills/education as permanent ranked effects; persisted in save schema v4 (migration chain v1→…→v4) — *ruled split: engine owns mechanics, games own meaning*
- [x] **9.7** Occupation, residence (interior-cell ref), schedule (time-of-day → place id) data stubs — mechanisms deepen in a later dictation
- [x] **9.8** Text lines: folder-per-line layout, `line.txt` tone header + text with `[sigh]`/`[laugh]`/`[pause]` marks, per-person voice description (auto-composed brief), at-least-one-line validation
- [~] **9.9** Voice fulfillment pipeline (P5 for audio): manifest export of unrecorded lines (text + directions + voice brief), delivered-take pickup under the same line id, random take selection, languages in fully separate folder trees per ruling — *running and tested end-to-end; the subtitle fallback returns the line text, but a dedicated subtitle UI widget isn't wired into a screen yet*
- [~] **9.10** Minimal wav support: PCM16 load/validate/write for delivered takes — *an actual playback sink joins with the audio pillar (Phase 10 candidate)*
- [x] **9.11** People showcase: a generated three-generation family rendered grouped by household (heredity visible), voice pipeline round trip (manifest → takes → random pick) in the demo; runs in CI and verify.sh

**Exit criteria:** a seed generates a family tree whose members have derived names, visibly hereditary bodies, relations you can query, skills as status effects, and at least one text line each; the whole tree costs bytes while cold; one person's line plays as a subtitle before fulfillment and as a delivered wav take after, chosen at random among takes. — *Met, with two honest notes: "plays" today means the text/take is selected and validated (the audio output device joins with the audio pillar), and the subtitle path returns text without a dedicated on-screen widget yet.*

## Phase 10 — Audio pillar

*Goal: the engine hears — a budgeted, allocation-free mixer core proven headlessly (this environment has no sound device, so correctness is samples-on-buffers), a thin AAudio sink at the platform boundary, and the voice-line pipeline made audible. Design: [`adr/0006-audio-architecture.md`](adr/0006-audio-architecture.md).*

- [x] **10.1** Audio clips: budgeted PCM16 loading ("audio" budget owned by the mixer, refuse on cap), unload/release — *over-cap clip refused in tests*
- [x] **10.2** Mixer core: fixed voice slots (refuse when full), per-voice gain/loop, linear resampling to the output rate, allocation-free `mix()` pulled by the device — *generation-checked voice ids; 22.05 kHz takes verified to play correct wall-clock length at 48 kHz*
- [x] **10.3** Buses: music/sfx/voice under master, per-bus gains; music ducking (glide, no clicks) while the voice bus speaks — *duck depth and recovery asserted on samples*
- [x] **10.4** Positional audio v1: listener position/forward, linear distance attenuation, constant-power stereo pan — *left/right energy asymmetry, near/far falloff, and beyond-max silence tested*
- [x] **10.5** P1 gate: the host runner pumps the mixer (a looping positional voice) every steady-state frame — zero allocations enforced on host and arm64
- [x] **10.6** Voice lines audible: delivered take → budgeted clip → Voice bus at the speaker's position, music ducking under it; subtitle text remains the no-take fallback
- [~] **10.7** Android AAudio sink in the app module (P3 boundary): low-latency stereo stream, callback pulls `mix()`, error-flag + restart on resume; compiles and links into the APK — *on-device listening awaits a physical device session*
- [x] **10.8** Audio showcase: `tools/audio_demo` walks a listener past a bell tower (pan sweeps right→center→left), wind + music beds, a Phase 9 voice take spoken mid-walk with audible ducking — mixed by the engine into a wav, verified on the samples, listenable on the review board; runs in CI/verify
- Later (deliberately deferred): disk-streamed music on job lanes, reverb/occlusion, HRTF, lock-free command ring

## Phase 11 — Basic gameplay mechanisms

*Goal: the two things whose absence stops the engine from being usable for a game — you cannot walk through walls, and you can act on the world. Everything here is engine machinery a game defines in data; the game-logic authoring model itself awaits dictation.*

- [x] **11.1** Collider representation + `CollisionWorld`: fixed-capacity static boxes with owning entity ids, refuse-at-cap, release-by-owner (P1)
- [x] **11.2** Character movement resolution: per-axis slide against blockers, step-up over low ledges, feet following the supporting surface — the world is solid *(v1 characters are walkers: there is no airborne state; gravity, jumping and falling arrive with dynamics, and `moveCharacter` is the seam they replace)*
- [x] **11.3** Queries: raycast (nearest hit, surface normal, owning entity) and box overlap
- [x] **11.4** World integration: the player AND the NPCs resolve their movement through the same call every step; placement registers a collider so what you see is what you bump into. *Chunk-scoped registration for streamed worlds is the remaining half — the API (`removeByEntity`) is the hook*
- [x] **11.5** `InteractableComponent` + `InteractionSystem`: kind, localized prompt key, range, facing cone; fixed capacity. The system is actor-agnostic from the first line — see 11.9
- [x] **11.6** Focus selection: in range, inside the facing cone, and not behind a wall (raycast line of sight) — one answer shared by the HUD prompt and the tap
- [~] **11.7** Interaction verbs v1: pick up (engine moves the item into the inventory and despawns it; a full pack refuses and leaves it in the world), open container (reports the bound collection; the device build opens it as a real panel), talk (reports the line; the device build plays the voice take with its subtitle) — doors/interior transitions land with the streaming interiors wiring
- [x] **11.8** Prompt in the HUD (localized keys, so Hebrew reads RTL for free) + the tap wired to interact; 8 unit tests, and `template_game` runs the same loop headlessly in CI — walks into a house and is stopped, then focuses and takes an apple
- [x] **11.9** **Every character can interact** (owner ruling, P9) — a *capability*, not a behaviour. The verbs sit on `CharacterSystem` (`focus`, `interact`, `interactWith`), so every character has them by existing and no flag can grant or withhold them; `InteractionSystem` stays actor-agnostic underneath and characters are equally valid targets (the player can be spoken to); a dead character acts on nothing. The device build routes the player's tap through the character verbs like anyone else would. Covered by `interaction_is_a_character_capability`. *When* a character chooses to act is deliberately not answered here — that belongs to behaviour/occupation/schedule

**Exit criteria:** on the phone, the world is solid, a prompt appears when you face something usable, and a tap picks up an item, opens a chest's contents, or makes a villager speak. — *Built and headlessly verified; the on-device confirmation is the owner's next device test.*

## Phase 12 — Actions (Dictation 6)

*Goal: a character's vocabulary. Actions are what a character CAN do, granted by what it is — universal for every character, body-level for a humanoid. Jump makes characters leave the ground for the first time; use-held makes one action mean whatever the item in your hands means.*

- [x] **12.1** Action model: `ActionId` name space, the per-character granted set (seeded with the universal actions at creation, so "every character can interact" needs no flag), `can` / `grant` / `revoke`, and `perform(actor, request) -> ActionResult`. Not-granted and refused-right-now are distinct answers
- [x] **12.2** Body-level grants: `grantHumanoidActions()` = walk + jump + use-held, applied when a character is given a humanoid body. Non-humanoids declare their own set and inherit no humanoid assumption. **The set is enforced, not decorative**: steering goes through `CharacterSystem::steer`, so a character that never declares `action/walk` stays where it stands however hard its controller pushes (it may still turn — being unable to walk is not being unable to look)
- [x] **12.3** Airborne characters: `verticalVelocity` + `grounded` on the character, gravity in a character-level locomotion step, and real vertical resolution in `moveCharacter` — rising hits ceilings, falling lands on what supports you (the seam task 11.2 named)
- [x] **12.4** `action/jump`: from the ground only, refused in mid-air; jump strength is a per-character parameter
- [x] **12.5** Item-use descriptors: `ItemUse` (kind, cooldown, reach, power, effect, animation key, payload) in a registry keyed by asset id — a data change adds a new kind of tool
- [x] **12.6** `action/use_held`: dispatch on what is actually in the hand. Engine performs strike / consume / toggle; reports launch and custom rather than pretending. Empty hand does nothing; a sheathed weapon is drawn first; per-character cooldown refuses a too-early second use
- [x] **12.7** Controls: jump and use as first-class touch intents next to move/look/action, so the phone can reach the new actions
- [x] **12.8** Tests + `template_game` headless proof + the device build: jump over a crate, swing a sword at a guard, eat an apple, light a torch — one button, three meanings

**Exit criteria:** on the phone, the player jumps, and the same "use" button does something different depending on what is in hand — while every one of those actions is reachable by any character that has it. — *Built and headlessly verified (`jump: rose 0.55 m, refused in mid-air yes, landed yes` / `use held: sword struck, apple eaten, torch lit`); the on-device confirmation is the owner's next device test.*

## Phase 13 — Wearables on an artist body (ADR 0008)

*Goal: the fitting guarantee survives artist-made meshes. Garments are modelled in a DCC against the published template and simply work — every variant, every pose, hair included — because fitting is baked at import, never computed per frame.*

*Sequencing: the pipeline (13.1–13.5) is wearables-session work and can start now. The body deliverables (13.6–13.9) are body-session work and gate the first authored garment, not the pipeline. 13.10 is the proof.*

*Correction (ADR 0010): the "processing corrupted a clean chart" premise was **measured and disproved** — the CC0 source is a deliberate 24-tile mirrored UDIM layout that was never single-tile. The silent import clamp remains the whole mechanism of the damage; only the attribution was wrong. The discard ruling stands and is strengthened: a repack into the tile is mandatory either way, so doing it once through the hardened importer is right. Chart policy — repack to one tile, halves **disjoint** — is ruled in ADR 0010.*

*Owner ruling (2026-08-20): the v3 processed body assets are **discarded, not repaired** — the UV corruption was introduced by the retired modeler session's processing, so the body **re-imports fresh from the pristine Blender CC0 source** through the hardened importer. One atomic contract-version event: new `.mgeskin` trio + re-cut garments + re-baked `.mgefit` bindings, hash bump, all in one push. The 13.6–13.9 deliverables land on the fresh base.*

- [x] **13.1** Import-time skin-weight transfer: confidence-gated nearest-surface copy (position + normal agreement) with weight inpainting across rejected vertices; clamp to ≤4 influences; accept authored weights when present — *`engine/src/import/garment_fit.cpp`. Inpainting diffuses over the mesh WELDED BY EXACT POSITION, not the raw index graph: a real garment splits vertices at every UV/region seam and falls into 17–29 disconnected islands, so index-graph diffusion stranded whole components (46–166 vertices per garment). Welding also makes coincident vertices share weights, without which a garment tears along its seams when the joint under it bends. Expect ~50 % of a closed garment to be inpainted rather than matched — the inner wall faces the body, so it fails the orientation gate by construction; `the_normal_gate_is_what_keeps_a_sleeve_off_the_ribcage` locks that reason down so nobody 'fixes' it by disabling the gate*
- [x] **13.2** Surface binding bake: per garment vertex, body triangle + barycentric + offset, constrained to the region's permitted vertex group so a sleeve cannot bind to the torso — *16-byte `SurfaceBind`; the offset lives in the triangle's tangent frame so cloth thickness is preserved as the surface moves and rotates. Constraint currently reads the body's shipped region parts (`permittedBindRegions`); the explicit vertex groups of B-25 replace that source when task 13.8 delivers them. Round-trip proven: at rest the binding reproduces the authored garment to 0.013 mm on all six shipped garments*
- [x] **13.3** Contract hash: bindings record the body asset's content hash; the pipeline REFUSES a binding whose body hash does not match (ADR 0008 — the committed `.mgeskin` is canonical, not the generator) — *FNV-1a over bind-pose positions/normals/influences/indices/parts, deliberately NOT over morph targets: adding a shape parameter must not invalidate the catalogue, moving a vertex must. Bindings also carry `rootBodyHash` so a chained outer layer can still name the body. `garment_bindings_match_the_shipped_body` is the standing gate — when the body session lands 13.6–13.9 that test fails on purpose and `mge_garment_fit` re-bakes*
- [x] **13.4** Morph re-fit at spawn/equip on job lanes, cached per (garment, morph-set); bone-scale variants continue to ride the palette. Nothing per frame — *`GarmentFitCache`: 32 fixed slots, LRU over ready entries, refuses at cap and when the Decode lane is full rather than growing or blocking; a miss answers `Pending` and the caller draws the garment unrefitted that tick. Wired into `buildPosedCharacter`, so `a_heavy_character_pushes_its_tunic_out` now passes on the shipped assets — this closes the gap that function documented ('a heavy belly does not yet push out a tunic'). Re-fit is skipped entirely when a variant has no morph-driven shape*
- [~] **13.5** Layer chaining: layer *k* binds against layer *k−1*'s outer surface offset by its thickness, resolved offline/at equip. No runtime cages, no runtime RBF — *`fitGarmentStack` implements and tests the offline half: each layer binds to the previous garment's normal-offset surface, and the outer binding's `baseHash` is provably NOT the body's while its `rootBodyHash` still is, so enclosure is arithmetic and the chain is still tied to one body. NOT YET USED END TO END, and the reason is a real design question rather than an omission: which garments share a slot is a property of an OUTFIT, not of a garment, so a tunic worn with and without armour cannot have one binding baked against a fixed under-layer. `mge_garment_fit` therefore bakes every shipped garment against the body, and `buildPosedCharacter` re-fits each layer against the body. Nothing is wrong in the shipped catalogue (no two morph-following layers share a region), but a layered outfit on a heavy body will have the outer layer following the skin rather than the layer beneath it. Closing it means chaining at EQUIP time — re-fit inner, offset, re-fit outer against that — on the same job lane and cache. Flagged to the architect as the one seam in Phase 13 I did not close*

- [ ] **13.6** `BodyRegion` extension for the accepted elbow/knee cut lines (D-3) + ear sub-shell (D-1), with the masking tests that go with them — **owner: character asset pipeline** (ADR 0010 resolves the double assignment; the cut lines are geometry on the body it produces). **Also carries the `cut_shell` tie-break fix** (ADR 0011): `tools/model/humanoid_template.py` breaks region ties by lowest region *name* while the importer breaks them by `BodyRegion` enum index, so garment cut boundaries can disagree with the regions the engine masks. Folded in here rather than fixed separately because 13.6 is already a cut-geometry contract-version event — one re-bake, not two
- [x] **13.6a** Body UV repack + fresh re-import (ADR 0010) — *`943e072`. New body content hash `e6eae4e58a7ec271` (1916 verts, 2200 tris, 11 parts); new `.mgeskin` trio, re-cut garments and re-baked `.mgefit` bindings in one atomic contract-version push. Measured by `mge_uv_report`, before → after: triangles with no UV area 89.7% → 0%, vertices pinned to a tile edge 91.1% → 0%, regions with texture space 3/12 → 11/12, density spread across regions 3.6× → 0%, texel density 2281 px/m over 4% of the body → 505 px/m over all of it. Arms, hands, legs and feet had no texture space at all before this. Disjointness is rasterised and measured, not asserted (100.0% shared for a mirrored pack, 0.0% for this one) — that measurement caught three implementations that passed inspection, including one that arrived at the mirrored layout ADR 0010 rejects, by accident. `chart_in_unit_tile`, `chart_max_degenerate_frac`, `chart_islands_disjoint`, `chart_min_utilisation` and `density_outside_tolerance` now pass*
- [~] **13.7** Body: real `Face` shell — closes the B-8 debt ADR 0008 records against v3, where the imported head is a single shell and the region is empty. Blocks the first mask/visor/face-covering helm — *`BodyRegion::Face` now ships as real geometry on all three LODs (301 triangles at LOD0, its own UV island, 100 % usable). The rig could never answer this — one Head joint means every head vertex is Scalp by construction — so the asset labels its regions and `gltf_skin_import` reads the label, all twelve of them rather than Face alone, which is what makes the packer's regions and the engine's the same answer instead of two inferences that have to agree. `chart_regions_required` passes and `stretch_above_max` is now the LONE refusal, which is ADR 0011's named trigger. Tests: the Face exemption in `body_mesh_covers_every_region` is deleted, plus `the_face_is_a_real_region_in_front_of_the_scalp` and `a_visor_can_hide_the_face_without_hiding_the_scalp`. **MISSING: B-9's authored hairline LOOP.** The boundary is classified per face, not cut, so it steps one face wide. Cutting it was implemented and measured at ~200 triangles on a LOD0 already at its 2 200 B-5 cap, and paying for them by decimating first fails `body_mesh_has_human_proportions` — measured hip width collapses 0.347 m -> 0.146 m, discretely, between 2 200 and 2 100. Needs a budget ruling (bigger LOD0 cap, or the head's own triangles spent on it), raised in `docs/research/face-shell.md`*
- [x] **13.7a** `B-9`'s authored hairline loop — **unblocked by ADR 0012**, which raises `B-5`'s LOD0 cap from 2 200 to 2 400 while leaving LOD1 (1 300) and LOD2 (650) untouched — *delivered. The cap/face boundary is now CUT, not classified: LOD0 decimates to 2 185 and the bisect brings it to 2 388, so the loop is paid out of the 200 ADR 0012 added rather than out of the body, and LOD1/LOD2 still ship 1 200/560 against unchanged caps. Proved by topology rather than by eye, because a smooth-shaded render cannot settle it: `the_hairline_is_one_closed_loop` matches boundary edges BY POSITION (the regions are separate glTF primitives, so no edge is literally shared across the seam), checks every boundary vertex has exactly two boundary edges, and walks the cycles. Measured: 87 boundary edges in two closed rings — 77 for the hairline itself, and 10 at the mid-line at the measured mouth height, which is the rim of the source mesh's mouth opening, not a stray patch. `body_mesh_has_human_proportions` fixed with it (ADR 0012 Ruling 3): bands now come from the torso's own extent, so hip width reads 0.347-0.372 m across budgets from 1 990 to 2 400 where it previously collapsed 0.347 -> 0.146 m between 2 200 and 2 100. Both gates now print their numbers. **Note for the architect:** with the sampling defect gone, `hips > waist` holds by 4 mm (0.347 vs 0.343) — that margin is this male body's anatomy rather than a measurement artifact, but it is thin*
- [x] **13.7b** `stretch_above_max` waiver — *delivered, and `mge_uv_report --gate` is GREEN for the first time: `CONFORMS (with waivers)`, exit 0. Format is `waive <asset> <rule> <region> <measured> | <reason> | <retires when>`, parsed in `tools/uv_report`, surfacing as a third verdict `WAIV` — never `pass` — with every waiver and its retirement condition printed under the summary. `stretch_max` stays 1.50. Two safety properties, both deliberate: a waiver names one REGION, so waiving the arms says nothing about the face; and it stops applying if the measured value drifts past what is recorded, so a waiver granted for 2.08x cannot absorb a later regression. **The drift guard is not theoretical — it fired on this very change**, refusing to cover HandL when 13.7a moved it 1.93x -> 1.96x, which is how the recorded values came to be re-measured rather than assumed. Seven non-face regions waived outright; Face (2.08x) waived PROVISIONALLY with the owner's eye as its retirement condition. Built under the explicit recorded seam exception in ADR 0012 Ruling 1 — textures-area files, not extended beyond the waiver mechanism*
- [~] **13.8** Body: hem-loop table (`B-11`) and per-region vertex groups (`B-25`) — what a garment artist terminates openings on and what constrains binding — *both published and MEASURED, not asserted. `templateHemLoops()` is 19 entries (B-11's 11 canonical loops, the 8 limb ones per side), every one derived from the RIG rather than chosen by eye: trunk loops are horizontal planes through the joint that names them, limb loops are perpendicular to the BONE, because the arms hang ~21 deg out and a horizontal plane across one cuts an ellipse, not a cuff. `fitHemLoop()` slices the shell and reports what the body actually does — closed rings, circumference, radius, and what share of the ring is the loop's own region. Measured on the template: neck 0.417 m, waist 0.834 m, hip 0.983 m, elbow 0.279 m, wrist 0.266 m, mid-thigh 0.522 m, knee 0.345 m, boot-cuff 0.313 m, ankle 0.239 m; **every loop closes, zero open chains**. `bodyVertexRegions()` delivers B-25 as a total partition — 2 097 vertices across 12 regions, none shared, none orphaned (BODY_CONTRACT 9.6). No asset change, so the body hash is unchanged at `1067c74324b6e091` and no re-bake was needed. **MISSING, and both need the architect:** (1) `shoulder_*` and `mid_upper_arm_*` do not encircle the ARM — a plane perpendicular to the upper-arm bone keeps clipping the chest until 0.18 m down it (56% of its 0.324 m), so the shoulder ring measures 1.19 m and is 11% ArmL. Where those two canonical loops should sit is a B-11 question, not mine; pinned by `the_upper_arm_loops_do_not_encircle_the_arm_yet` so it cannot change silently. (2) the wearables `Hem-loop table` gate is still hard-coded BLOCKED in their file and needs one line to call this. Both raised in `docs/status/character-asset-pipeline.md`*
- [ ] **13.9** Body: published glTF authoring reference (B-29) at template proportions in bind pose, with rig, region groups, hem loops and attachment points, versioned by the same content hash
- [~] **13.10** The seven acceptance gates as tests (BODY_CONTRACT.md §9): mask integrity, +35 mm offset shell, hem-loop table, scalp-cap fallback, deterministic hash, groups & anchors, plus the existing gates — and the first authored garment set proving the contract end to end. **Gates are self-serve (P12): an artist imports and gets pass/fail with a reason — no engineer in the content loop** — *six of seven implemented and passing on the shipped body; the seventh (hem loops, B-11) is BLOCKED on task 13.8 and says so in words, naming the task. `engine/src/import/wearable_gates.cpp` holds the gate logic, `tools/wearable_gates` is the self-serve front door (one line per gate: PASS/FAIL/BLOCKED plus the reason), and `tests/test_wearable_gates.cpp` runs the identical code in CI — a gate passing in only one of the two would be worth nothing. `the_gates_actually_fail_on_a_body_that_deserves_it` breaks a body four ways and checks the matching gate notices, because a suite that has only seen a good body proves nothing. TWO CONTRACT CLAUSES WERE CORRECTED against measurement rather than implemented as written — see BODY_CONTRACT.md §9.1 and B-24, both marked. The first authored garment set is 13.11 and still waits on 13.8/13.9*

- [ ] **13.11** First catalog (D-4 ruled): tunic, trousers, boots, short hair — chosen to stress the mechanism (every masking region, both hem classes, all three layers, hair-under-headwear). Authored as garment *archetypes* whose conventions the next tunic inherits (P12)
- [ ] **13.12** Zero-code-per-garment proof: adding a wearable touches no `.cpp` and no `CMakeLists` — the build demonstrates it (P12)

**Exit criteria:** a garment authored in a DCC against the published reference, imported, and rendered correctly on the template and both variant extremes, posed — with masking leaving no hole and no clipping, and zero fitting work on the frame path.

## Phase 14 — Use-animation archetypes (P12, held for sequencing)

*Goal: a new weapon or tool never means new animation work (CHARACTERS.md §6.2). Items declare a use archetype (`swing`, `thrust`, `chop`, `work`, `draw`, `aim`, `raise`, `consume`, `gesture`); the engine animates the archetype parameterized by the item's grip, reach and weight. Bespoke clips become an opt-in for hero items.*

*Sequenced after Phase 13's fitting pipeline — same area (body & animation) and the layering work below is also what facial expressions and future overlays ride on.*

- [ ] **14.1** Layered poses with masks: locomotion drives the lower body while an action archetype drives the upper body — the enabling capability everything else depends on (a character swings *while walking*)
- [ ] **14.2** Archetype library v1: the nine archetypes as parameterized procedural motions (grip selects arms/torso involvement; reach sets arc radius and lean; weight sets wind-up/strike/recovery timing)
- [ ] **14.3** Phase-addressable timeline: wind-up / strike / recovery exposed as fractions, and the Phase 12 action model's damage moment hung on `strike` instead of a tuned delay
- [ ] **14.4** Interruption: hit/stagger/death blend out mid-action, never snap
- [ ] **14.5** Item data: archetype + reach + weight on `ItemUse`; `animKey` narrows to the bespoke-clip escape hatch
- [ ] **14.6** Proof by catalog: sword, spear, axe, hammer, torch, apple — six items, zero per-item animation authoring, visibly distinct motion

## Phase 15 and beyond — held for further dictation

Deliberately not planned yet; known candidates awaiting direction:

- Textures & materials (everything renders flat-shaded today). The **authoring
  standard** is written ahead of the work — [`TEXTURING.md`](TEXTURING.md) for
  the reasoning, `assets/standards/skin_texture.mgestd` for the operative,
  machine-checked form (map set, colour spaces, ASTC/ETC2 two-pack bake,
  per-class budgets, texel density, chart and mip rules, and an explicit refuse
  list — P12: one line of data, not a code change, tightens a rule). The
  texture **runtime** (sampling, GPU upload, materials, formats on the device)
  belongs to the renderer area, not here. Engine-side prerequisites: TEXTURING
  §13. Awaiting the owner's verdict on two proposals (textures as generators;
  virtual textures on the P5 pattern)
- **UNBLOCKED (ADR 0010/0011/0012) — a skin texture may now be authored.** `mge_uv_report --gate` passes six of its seven checkable rules; `stretch_above_max` is the lone refusal and ADR 0012 waives it — outright for the seven non-face regions, provisionally for the **Face at 2.09×, whose retirement condition is the owner's eye on the first authored face texture** rather than a threshold. Nothing has been painted yet. *Historical, for the record:* `mge_uv_report`
  (in `ctest`; `--gate` for pass/fail) measures 89.7 % of the body's triangles
  with zero UV area and 91.1 % of its vertices pinned to a tile edge: the source
  unwrap extends past `u = 1` and `gltf_skin_import.cpp` clamps rather than
  refusing. Arms, hands, legs and feet have no texture space at all. No skin
  texture can be authored until the chart is repacked into the 0–1 tile (a B-27
  contract-version event). Handoff to the body session, with root cause, fix,
  UV-vs-topology cost and a seam request on the importer:
  [`research/uv-audit.md`](research/uv-audit.md)
- UI screen flow: screen stack, pause menu, inventory reachable in-game
- AI expansion (behavior model beyond the v1 state machine, schedules, group behavior) — v1 landed in 8.8; *when* a character chooses to perform an action belongs here, not in Phase 12
- Advanced facial animation (lip-sync, emotes, gaze) — basic set specified in 8.20
- Physics beyond the v1 collision for OBJECTS (pushing, ragdolls, projectiles) — character dynamics landed in 12.3
- World-authoring/editor tooling
- Scripting / game-logic authoring model
- Networking / multiplayer
- Asset-pack delivery & app-size strategy for giant games
- Performance certification tiers across device classes

---

*This breakdown will be revised as the remaining requirements are dictated. Tasks are appended or reshaped in place; completed tasks are never deleted.*
