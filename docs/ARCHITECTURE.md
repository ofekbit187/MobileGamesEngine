# Engine Architecture

The structure of the engine, derived from the [principles](PRINCIPLES.md). Three foundation pillars carry everything: the **Application**, the **Game Framework**, and the **Graphics Engine**. The cross-cutting systems — world streaming, UI, controls, persistence, assets/virtual models — are built on those pillars.

---

## 1. Layer view

```
┌──────────────────────────────────────────────────────────────┐
│                          GAME CODE                           │
│        (the game built by the engine's user/programmer)      │
├──────────────────────────────────────────────────────────────┤
│                       GAME FRAMEWORK                         │
│  Game loop · World/Scene model · Entities & components       │
│  Gameplay controls · Save/Load service · Game templates      │
├──────────────┬───────────────────────────┬───────────────────┤
│  UI SYSTEM   │      WORLD STREAMING      │   ASSET SYSTEM    │
│  built-in    │  chunks · residency ·     │  import · runtime │
│  designs ·   │  priorities · budgets     │  formats ·        │
│  HUD · menus │                           │  virtual models   │
├──────────────┴───────────────────────────┴───────────────────┤
│                       GRAPHICS ENGINE                        │
│  3D renderer · materials · LOD · GPU resource management     │
│  UI overlay pass · placeholder rendering                     │
├──────────────────────────────────────────────────────────────┤
│                    CORE / FOUNDATION                         │
│  memory (budgets, pools, arenas) · jobs/threading · math     │
│  I/O · containers · profiling · logging                      │
├──────────────────────────────────────────────────────────────┤
│                        APPLICATION                           │
│  Android entry & lifecycle · surface/window · input events   │
│  storage access · permissions · platform services            │
└──────────────────────────────────────────────────────────────┘
```

Rules of the layer diagram:

- A layer may depend only on layers **below** it. Game code never talks to the Application layer directly; it goes through the Game Framework.
- The three middle systems (UI, World Streaming, Assets) are siblings: they do not depend on each other except through interfaces defined in Core/Framework.
- **Core/Foundation is where P1 lives.** Memory budgets, pools, and arenas are provided here and every layer above is required to allocate through them.

---

## 2. The three pillars

### 2.1 Application (platform layer)

The engine's contact surface with Android. Nothing above it knows Android exists.

Responsibilities:

- **Entry point & lifecycle** — hosts the game inside an Android activity; translates the Android lifecycle (create/pause/resume/surface lost/process death/config change) into engine events that the layers above must handle (P3).
- **Surface & display** — owns the rendering surface, its lifecycle, resolution/density, and frame pacing with the display.
- **Raw input** — receives touch (and later controller/sensor) events, timestamps them, and forwards them to the input pipeline. No gameplay meaning at this layer.
- **Storage access** — exposes the app-private storage, packaged assets, and asset-pack locations through an engine I/O interface tuned for streaming reads (P2).
- **Platform services** — audio device, vibration, permissions, system UI insets (cutouts, gesture bars).

### 2.2 Game Framework

The programmer's home. Defines what a "game" is on this engine.

Responsibilities:

- **Game loop** — fixed-step simulation with interpolated rendering; owns frame orchestration: input → simulation → streaming tick → UI → render submission.
- **World & scene model** — the world is a single continuous 3D space, partitioned into streamable regions (see §3). Scenes (title screen, world, interiors) are worlds too — small ones.
- **Entities & components** — data-oriented entity model (compact, poolable, cache-friendly — P1). Entities reference assets by stable ID, never by pointer, so residency can change beneath them (P2).
- **Character system** — the `Character` base entity (universal mechanisms: locomotion, AI attachment, mortality, factions, inventory, equipment) and `HumanoidCharacter` (template body, data-driven variants, canonical skeleton, wearable fitting). Player vs NPC is a controller choice, nothing more (P9). Full design: [`CHARACTERS.md`](CHARACTERS.md).
- **Gameplay controls** — maps the input pipeline to gameplay intents (move, look, act) through control schemes; ships with touch-native defaults (virtual stick + camera drag + action buttons) rendered by the UI system.
- **Save/Load service** — engine-level persistence per P7: versioned save slots, world-delta recording, atomic crash-safe writes.
- **Game template** — the "new game" starting point: working world, streaming enabled, default controls, default UI, saves wired (P8).

### 2.3 Graphics Engine

The single 3D rendering path (P4).

Responsibilities:

- **Renderer** — forward 3D pipeline targeting mobile GPUs (tile-based architectures); draws only what residency and visibility allow.
- **Materials & shading** — a compact material model with engine-provided shaders; mobile-friendly lighting.
- **LOD & visibility** — level-of-detail selection and culling integrated with streaming: distance decides both *what to draw* and *what to load*.
- **GPU resource management** — GPU memory is a budgeted resource like CPU memory (P1); textures/meshes upload and evict in cooperation with the streaming system.
- **Placeholder rendering** — renders virtual-model placeholders (correctly sized primitive volumes with a distinct visual treatment and optional label) with the same transform/LOD path as real models (P5).
- **UI overlay pass** — renders the UI system's batched output on top of the 3D frame (P6).

---

## 3. Cross-cutting systems

### 3.1 World Streaming

The realization of P2, and the primary enforcement point of P1.

- **Chunked world format** — the world is divided into spatial chunks stored in a seek-friendly container on disk. Each chunk holds its terrain/static geometry, entity placements, and references (by ID) to shared assets. Designed for partial, sequential-friendly reads on Android storage.
- **Residency management** — a chunk is *resident* (in memory), *loading*, or *cold* (on disk). Movement, camera, and gameplay hints drive a priority queue; the memory budget drives eviction. The player's surroundings are always resident; distant content exists only as lightweight metadata.
- **Asset residency** — shared assets (models, textures) are reference-counted across chunks and streamed with mip/LOD granularity: a distant object may be resident only at its lowest LOD.
- **Guarantees** — the frame never blocks on I/O. Non-resident content renders as lower LOD or not at all; gameplay queries against cold chunks return "not loaded" and systems must handle it (P2).

### 3.2 Asset System & Virtual Models

The realization of P5.

- **Import (native)** — imports standard 3D interchange formats (glTF first) into the engine's compact runtime format: quantized vertex streams, compressed textures, precomputed LOD chains. Import runs as an engine tool step, producing streaming-ready data.
- **Runtime format** — designed for `mmap`/direct-read into GPU-uploadable layouts with minimal parsing (P1, P2).
- **Stable asset IDs** — everything references assets by stable ID. This is what makes virtual→real replacement seamless.
- **Virtual models** — an asset type with no geometry payload. A virtual model declares:
  - `id` — stable asset ID, identical to what the real asset will use
  - `proportions` — bounding dimensions (width × height × depth), optional shape hint (box, cylinder, capsule, composite)
  - `description` — structured text describing the intended asset (what it is, style, materials, distinguishing features) — written to be consumed by an external agent that will produce the real model
  - optional gameplay metadata (collidable, interactable) so gameplay works against the placeholder
- **Placeholders** — the graphics engine renders a virtual model as a placeholder of exactly the declared proportions at the declared transform, visually distinct (e.g., stylized volume + label), so world composition is accurate before assets exist.
- **Fulfillment pipeline** — the engine can export a *manifest of unfulfilled virtual models* (IDs + proportions + descriptions). An external agent produces assets; importing an asset under a virtual model's ID *fulfills* it — every placement in every chunk now streams the real model, with zero scene changes.

### 3.3 UI System

The realization of P6.

- **Widget & screen model** — retained widget tree with layout, styling, and navigation; rendered as batched geometry through the graphics engine's overlay pass. Low-allocation updates (P1).
- **Original built-in designs** — the engine ships its own designed widget library and screen kit: HUD elements, main menu, pause menu, settings, dialogs, inventory-style grids, loading/boot screens. A game has a coherent visual identity by default and can restyle via themes.
- **Virtual gameplay controls** — on-screen stick(s), buttons, and gesture zones are UI widgets bound to the control scheme in the Game Framework; they ship as part of the built-in designs.
- **Input routing** — UI gets first claim on touches; unclaimed touches fall through to gameplay controls.

### 3.4 Persistence (Save/Load)

The realization of P7 (service lives in the Game Framework; format lives here).

- **Delta model** — shipped world content is immutable; saves record deltas (moved/spawned/destroyed entities, state changes) keyed by chunk. Loading a chunk = stream shipped data + apply its delta. Save size scales with *player impact*, not world size.
- **Slots & versioning** — multiple save slots; every save carries schema versions and the engine migrates old saves forward.
- **Atomicity** — write-new-then-swap with checksums; process death mid-save leaves the previous save intact (P3, P7).

### 3.5 Character System

The realization of P9. Design detailed in [`CHARACTERS.md`](CHARACTERS.md); summarized here for the layer view.

- **`Character` base** (Game Framework) — universal mechanisms for every acting being: locomotion, AI/player controller attachment, mortality, enemy/ally (faction) classification, inventory, equipment slots, perception hooks, streaming/persistence behavior. Controllers (`PlayerController` / `AIController`) are the only player-vs-NPC distinction and are swappable at runtime.
- **`HumanoidCharacter`** — the engine-provided specialization: one imported **template base body** (canonical mesh, UVs, body-part segmentation, skeleton), **data-driven body variants** (skin texture, size, width, height, shoulders, chest, legs, feet, face and facial features — via bone-proportion scaling + morph deltas), default walk/run/idle animations, and the **wearable fitting mechanism**.
- **Wearables** (Asset System + Graphics Engine) — assets authored once against the template body that dynamically fit any body variant (they receive the body's variant transformations), mask the skin they cover to prevent clipping, and animate through the same skinning path. Hairstyles are wearables. Wearables are virtual-model compatible (P5).
- Non-humanoid characters (animals, monsters, game-defined creatures) extend `Character` directly with their own body definitions and slot sets.

### 3.6 Core / Foundation

The substrate enforcing P1 mechanically, not by convention.

- **Memory** — global budget split into per-system budgets; allocators (arenas, pools, ring buffers) that report live usage; hot-path allocation tracking in debug builds.
- **Jobs & threading** — a job system with dedicated lanes: simulation, streaming I/O, asset decode, render submission. Streaming never runs on the frame-critical path.
- **Math** — 3D math library (vectors, quaternions, transforms, AABBs) shaped for the entity and rendering data layouts.
- **I/O** — async, priority-aware read API over Android storage; the streaming system is its main client.
- **Diagnostics** — frame profiler, memory-budget dashboards, streaming visualizer (chunk states), logging. Memory accountability (P1) requires these from day one.

---

## 4. Primary runtime flows

**Frame flow:**
`input events → gameplay controls → simulation (fixed step) → streaming tick (priorities, evictions) → UI update → visibility & LOD → render submission`

**Streaming flow:**
`player movement → chunk priorities → async I/O (Core) → decode job → residency activation → renderer & gameplay see new content`

**Virtual model flow:**
`author declares virtual model (proportions + description) → placed in world chunks → placeholder streams & renders like a real asset → manifest exported → external agent produces model → import under same ID → placeholder replaced everywhere, scene untouched`

**Save flow:**
`gameplay mutates entities → deltas recorded per chunk → save request → snapshot on simulation boundary → async atomic write → verified swap`

---

## 5. Technology stance (initial)

- **Language:** engine core in C++ (NDK) for memory control (P1); thin Kotlin application shell for the Android integration (P3). The game-facing API surface will be dictated later.
- **Graphics API:** Vulkan first (best fit for explicit memory budgeting — P1), with the door open to a GLES fallback if device coverage demands it.
- **Model interchange:** glTF 2.0 as the canonical import format.
- *These are working choices for the design phase; they harden when implementation tasks begin.*

---

*This structure covers the dictated foundations. New systems (audio, physics, networking, tooling, etc.) will be added to this document as they are dictated, respecting the layer rules above.*
