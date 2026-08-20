# MobileGamesEngine — session guide

Android-native 3D open-world game engine. Design is dictated top-down by the
owner; read `docs/PRINCIPLES.md`, `docs/ARCHITECTURE.md`, `docs/CHARACTERS.md`,
then `docs/TASKS.md` (live task status) before changing anything. 3D models
(style, budgets, topology rules, definition of done) follow `docs/MODELING.md`;
textures (map set, formats, budgets, texel density, the UV chart) follow
`docs/TEXTURING.md`. Principles are ranked — P1 (memory efficiency) wins
conflicts.

## Build & verify (no device needed)

```sh
scripts/verify.sh                 # everything the environment supports
```

Individually:

- Host build + unit tests + headless runner:
  `cmake -B build -G Ninja && cmake --build build && ctest --test-dir build`
  then `./build/tools/host_runner/mge_host_runner` (must print
  `steady-state heap allocations: 0` — this is the P1 gate).
- Vulkan work is testable headlessly: `apt-get install libvulkan-dev
  mesa-vulkan-drivers` (llvmpipe), then `./build/tools/vk_smoke/mge_vk_smoke`
  executes real GPU commands and verifies pixels on the CPU.
- Android SDK/NDK not installed? `scripts/setup-android-sdk.sh` provisions
  `~/android-sdk` (~2.5 GB download; dl.google.com must be reachable).
- APK: `ANDROID_HOME=~/android-sdk gradle :app:assembleDebug` (or `./gradlew`).
- arm64 verification without a device: NDK static build + qemu-user
  (`apt-get install qemu-user-static`), see `scripts/verify.sh` step 2 —
  runs the shipped ABI's actual instructions.

## Layout

- `engine/` — portable C++17 core (never includes Android headers except via
  the platform boundary; JNI appears only in `app/src/main/cpp/jni_bridge.cpp`)
- `app/` — Kotlin shell + JNI glue; `tools/host_runner/` — headless prototype;
  `tests/` — dependency-free unit tests; `docs/adr/` — decisions

## Showcase protocol (owner-mandated, default)

The owner reviews progress on a living artifact page — the **dictation
review board**: https://claude.ai/code/artifact/b259a23e-ebc6-4ada-a234-4a5c89c63fec

- After every milestone (and whenever the owner asks "where do things
  stand"), **update that artifact in place** (pass its URL as `url` when
  publishing from a new session — do NOT create a new artifact/link).
- Format: one card per dictated requirement, status chip
  (running & verified / partly running / designed / not started), and
  evidence. Evidence is always labeled: ● real output (produced by the
  engine in this environment) vs ○ design proposal (mockup awaiting the
  owner's verdict).
- Everything visual is shown as a proposal *before* it is built and as a
  real engine capture *after* — never present a mockup as engine output.
- Read the artifact's comments (`action: "comments"`) at session start and
  when asked; owner comments on cards are verdicts — docs update first,
  then code.

## Rules of the codebase

- Every allocation goes through a registered `BudgetRegistry` budget; caps
  refuse, never grow. Steady-state frame loop must stay allocation-free
  (the host runner enforces it — keep it passing).
- Fixed-step simulation; render interpolates. Streaming/decode work belongs
  on job lanes, never the frame path.
- Update `docs/TASKS.md` statuses honestly with code changes ([~] partial,
  with what's missing noted).
