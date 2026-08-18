# MobileGamesEngine

An Android-native 3D game engine built for **open-world games with giant content and a minimal runtime footprint**.

This repository is in the **design phase**. The engine's direction is dictated top-down: first principles, then structure, then tasks. Read the documents in this order:

1. [`docs/PRINCIPLES.md`](docs/PRINCIPLES.md) — the non-negotiable rules every subsystem must obey.
2. [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — the structure: layers, subsystems, and how they relate.
3. [`docs/CHARACTERS.md`](docs/CHARACTERS.md) — the character system: universal characters, player/NPC parity, the humanoid template body, variants, and wearables.
4. [`docs/TASKS.md`](docs/TASKS.md) — the phased task breakdown derived from the architecture.

## What this engine is

- **Android-first.** The application layer, lifecycle handling, input, and storage model are designed for Android — not ported to it.
- **3D only.** Every game built on the engine is 3D. There is no 2D rendering path to maintain.
- **Open-world by default.** World streaming from storage is a foundational engine feature, not an add-on. Content on disk can be enormous; content in memory at any moment is small.
- **Memory efficiency is a ranked priority.** When memory use conflicts with another concern, memory wins unless explicitly overruled in the design docs.
- **Original built-in UI.** The engine ships its own UI system with its own original designs — games get a coherent, styled UI out of the box.
- **Virtual models.** A scene can be authored before its 3D assets exist. A *virtual model* carries proportions and a description of the intended asset; the engine renders a correctly sized placeholder in its place, and an external agent can produce the real model later without touching the scene.

## Foundation pillars

| Pillar | Responsibility |
|---|---|
| **Application** | Android entry point, lifecycle, windowing, threading, permissions, storage access |
| **Game Framework** | Game loop, scenes/worlds, entities, gameplay controls, saving & loading |
| **Graphics Engine** | 3D rendering, model import, materials, streaming-aware resource management |

Everything else — UI, world streaming, virtual models, input — is built on these three pillars, as detailed in the architecture document.

## Repository layout

```
engine/     Native engine core (C++17): Core/Foundation + Framework layers
app/        Android application shell (Kotlin + JNI glue) — needs Android SDK/NDK
tools/      Host tooling: mge_host_runner (headless prototype runner)
tests/      Native unit tests (no external dependencies)
docs/       Design documents and ADRs
```

## Building

**Engine core on a desktop host** (no Android SDK needed):

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure   # unit tests
./build/tools/host_runner/mge_host_runner    # headless prototype
```

The host runner boots the engine, drives the fixed-step loop through
pause/resume and surface-loss lifecycle events, and verifies the P1 claim:
**zero heap allocations in the steady-state frame loop**, with a live
memory-budget dashboard.

**Android app**: `./gradlew :app:assembleDebug` with an Android SDK + NDK
installed. The app module hosts the engine behind a `SurfaceView` and drives
one engine tick per display frame; rendering arrives with Phase 2 (Vulkan).

## Status

Prototype phase. Design documents are established and the Phase 1 core
(memory budgets/allocators, math, job lanes, fixed-step engine loop,
lifecycle handling) is implemented and unit-tested on the host. Further
engine requirements are still being dictated; documents and code evolve
together — see [`docs/TASKS.md`](docs/TASKS.md) for live task status.
