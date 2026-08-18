# ADR 0001 — Technology stack

**Status:** accepted (task 0.5)

## Decision

- **Engine core: C++17 via the Android NDK.** Explicit memory control is P1's hard requirement; a GC runtime in the core would make the zero-steady-state-allocation and strict-budget rules unenforceable.
- **Application shell: Kotlin.** A thin activity/surface/lifecycle layer only; no engine logic lives in Kotlin (P3 boundary — engine code never sees Android types except at the JNI glue file).
- **Graphics API: Vulkan first.** Explicit GPU memory management fits P1; tile-based mobile GPUs are the target. A GLES fallback is deferred until device-coverage data demands it.
- **Model interchange: glTF 2.0** as the canonical import format for the native import path (P5).
- **Build: Gradle (app) + CMake (native).** One CMake tree (`engine/`) serves both the Android build (through the app module's external native build) and the host build.

## Consequence: host-testable core

The engine core is portable C++ with platform specifics behind the Application layer. It builds and unit-tests on a desktop host (`cmake -B build && ctest`), plus a headless `mge_host_runner` that exercises the loop, lifecycle, and memory budgets without a device. Device-only layers (surface, Vulkan, touch) stay thin so the maximum surface of the engine is testable in CI without emulators.
