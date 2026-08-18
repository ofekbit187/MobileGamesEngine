# Engine Principles

These are the ground rules of the engine. Every design decision, subsystem, and line of code is measured against them. When two principles conflict, the one listed first wins unless a design document explicitly says otherwise.

---

## P1 — Memory efficiency above all

Performance efficiency, and **specifically memory use**, is the engine's top runtime priority.

- The working set in RAM must stay small and bounded regardless of how large the game's content is on disk. A game may ship gigabytes of world; the engine must run it inside a strict, configurable memory budget.
- Every subsystem must declare its memory budget and be accountable to it. There are no "unbounded caches."
- Prefer streaming, pooling, and reuse over allocation. Steady-state gameplay should approach **zero per-frame allocations** on hot paths (avoiding garbage-collector pressure on Android).
- Data is designed for locality and compactness first (structure-of-arrays, packed formats, quantization where precision allows), convenience second.
- "Giant games, minimal runtime burden" is the test: content size on storage may grow without bound; runtime memory and load cost must not grow with it.

## P2 — Open world is the default, streaming is foundational

The engine assumes an open, continuous world unless a game opts out.

- The world is **streamed from storage as the player moves**. Loading screens are for booting, not for traversal.
- All world content — geometry, models, textures, entities, gameplay data — is stored in a chunked, seek-friendly on-disk format designed for partial reads.
- Every engine system that touches world content (rendering, physics-adjacent queries, gameplay logic, saving) must tolerate content that is *not currently resident* and must degrade gracefully (LOD, placeholder, deferral) rather than stall the frame.
- Streaming has priorities: what the player is about to see and touch loads first.

## P3 — Android-native, not ported

The engine is built *for* Android, embracing its lifecycle and constraints rather than abstracting them away as an afterthought.

- First-class handling of the Android activity lifecycle: pause/resume, surface loss, process death, configuration change. A game must survive all of these without corruption.
- Storage access uses Android's real storage model (app-private storage, asset packs, scoped storage) — the streaming system is designed around Android I/O characteristics.
- Input is touch-first. Gameplay controls are designed for touchscreens (with room for controllers later), not translated from keyboard/mouse assumptions.
- Battery and thermal behavior are performance metrics, not externalities.

## P4 — 3D only

All games built on the engine are 3D.

- There is exactly one rendering path: the 3D pipeline. No parallel 2D world renderer to build, test, and maintain.
- The engine's UI is drawn by the same graphics engine (as an overlay pass), not by a separate technology stack.
- Tooling, formats, and the scene model all assume 3D space.

## P5 — Author the world before the assets exist

Scene building must never be blocked on asset production.

- **Virtual models** are a first-class engine concept: a model defined only by its *proportions* (bounding dimensions), *placement*, and a *description* of what the asset should be.
- The engine renders a placeholder of the exact declared size, at the exact declared position and orientation, so a world can be composed comfortably and accurately with zero finished assets.
- A virtual model's description is structured so that an **external agent** (human artist or generative tool) can later produce the real asset from it. When the real model arrives, it replaces the placeholder **without any change to the scene** — same ID, same transform, same gameplay behavior.
- Native 3D model import is built in: bringing a finished model into the engine's runtime format is an engine feature, not an external toolchain requirement.

## P6 — The engine brings its own UI

Games get a real, styled UI out of the box.

- The engine ships an original, built-in UI system with its **own original designs** — widgets, screens, and visual style authored for this engine, not a copy of a platform toolkit and not a bare "bring your own skin" framework.
- The built-in designs cover what games actually need: HUD, menus, dialogs, inventory-style grids, settings, virtual gameplay controls (sticks/buttons).
- UI obeys the same performance rules as everything else (P1): retained, batched, low-allocation rendering.

## P7 — Saving and loading are engine services

Persistence is a core service, not per-game boilerplate.

- The engine provides saving and loading of game state: player state, world-change deltas, and progression — versioned and forward-compatible.
- Saves record *changes relative to shipped content* (deltas), so save size does not scale with world size. This is the persistence mirror of P2.
- Saves must be atomic and crash-safe: a save interrupted by process death (which Android will inflict) never corrupts the previous save.

## P8 — Simple to build a game with

The engine's user is a game programmer; their comfort is a feature.

- Composing a world, placing models (real or virtual), wiring controls, and shipping a save-enabled game must require little ceremony.
- Sensible defaults everywhere: a new game starts from a working template (world + streaming + controls + UI + saves) and the developer changes what they need.
- Engine internals may be complex; the API surface must not be. Complexity is allowed only below deck.

## P9 — The player is just a character

There is no difference between the player and NPCs except how they are controlled.

- One `Character` entity serves every acting being. The player is a `Character` with a `PlayerController` attached; an NPC is the same `Character` with an `AIController`. Controllers are swappable at runtime.
- Every character carries the full feature set — including inventory, wearable slots, and a tool/weapon slot. Nothing is player-only.
- Universal mechanisms (locomotion, AI attachment, mortality, enemy/ally classification, inventory/equipment, persistence) live on the character base; only genuinely body-specific mechanisms (the humanoid template body, variants, wearable fitting) live on specializations like `HumanoidCharacter`.
- Code that special-cases "the player" is a design smell; systems ask "which controller?" or "which faction?" instead.

See [`CHARACTERS.md`](CHARACTERS.md) for the full character system design.

---

*These principles are the foundation. Further principles will be added as more of the engine's direction is dictated; existing ones are refined, not silently changed.*
