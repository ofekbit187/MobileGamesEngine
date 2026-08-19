# MobileGamesEngine — session guide

Android-native 3D open-world game engine. Design is dictated top-down by the
owner; read `docs/PRINCIPLES.md`, `docs/ARCHITECTURE.md`, `docs/CHARACTERS.md`,
then `docs/TASKS.md` (live task status) before changing anything. Principles
are ranked — P1 (memory efficiency) wins conflicts.

## Build & verify (no device needed)

```sh
scripts/verify.sh                 # everything the environment supports
```

Individually:

- Host build + 17 unit tests + headless runner:
  `cmake -B build -G Ninja && cmake --build build && ctest --test-dir build`
  then `./build/tools/host_runner/mge_host_runner` (must print
  `steady-state heap allocations: 0` — this is the P1 gate).
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

## Rules of the codebase

- Every allocation goes through a registered `BudgetRegistry` budget; caps
  refuse, never grow. Steady-state frame loop must stay allocation-free
  (the host runner enforces it — keep it passing).
- Fixed-step simulation; render interpolates. Streaming/decode work belongs
  on job lanes, never the frame path.
- Update `docs/TASKS.md` statuses honestly with code changes ([~] partial,
  with what's missing noted).
