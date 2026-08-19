#!/usr/bin/env bash
# Full engine verification, no device needed. Runs every check the current
# environment supports and reports what it covered:
#   1. Host build + unit tests + headless runner        (always)
#   2. arm64 build via NDK + same tests/runner in QEMU  (needs NDK + qemu-user)
#   3. Android APK assembly                             (needs Android SDK)
# Environment: ANDROID_HOME (default ~/android-sdk); scripts/setup-android-sdk.sh
# provisions it.
set -euo pipefail
cd "$(dirname "$0")/.."

ANDROID_HOME="${ANDROID_HOME:-$HOME/android-sdk}"
NDK_DIR="$(ls -d "$ANDROID_HOME"/ndk/* 2>/dev/null | sort -V | tail -1 || true)"
QEMU="$(command -v qemu-aarch64-static || command -v qemu-aarch64 || true)"
GRADLE="${GRADLE:-$(command -v gradle || true)}"
[ -x ./gradlew ] && GRADLE=./gradlew

echo "=== 1/3 host: build + tests + runner"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build >/dev/null
ctest --test-dir build --output-on-failure >/dev/null
./build/tools/host_runner/mge_host_runner | grep -E "steady-state|OK"
./build/tools/audio_demo/mge_audio_demo build | grep -E "voice line|wrote|bell pan|OK"
./build/tools/stream_test/mge_stream_test bake build/ci.mgeworld 12 33 | tail -1
./build/tools/stream_test/mge_stream_test run build/ci.mgeworld 30 48 | grep -E "peak|flow|update time|OK"
if [ -x build/tools/vk_smoke/mge_vk_smoke ]; then
    ./build/tools/vk_smoke/mge_vk_smoke build/vk_smoke.ppm | grep -E "device|pixels|OK"
    ./build/tools/asset_import/mge_asset_import tests/data/cube.gltf build/cube.mgemesh
    ./build/tools/vk_scene/mge_vk_scene build/vk_scene.ppm build/cube.mgemesh | grep -E "submitted|share|OK"
    ./build/tools/template_game/mge_template_game build/walk build/cube.mgemesh | grep -E "fulfilled|capture|traveled|OK"
    ./build/tools/humanoid_demo/mge_humanoid_demo build | grep -E "drawn|hamlet|OK"
    ./build/tools/people_demo/mge_people_demo build | grep -E "family tree|rendered|voices|OK"
else
    echo "vk_smoke/vk_scene: SKIPPED (no Vulkan SDK — apt-get install libvulkan-dev mesa-vulkan-drivers)"
fi

if [ -n "$NDK_DIR" ] && [ -n "$QEMU" ]; then
    echo "=== 2/3 arm64 (NDK $(basename "$NDK_DIR")): tests + runner under QEMU"
    cmake -B build-arm64 -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$NDK_DIR/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
        -DCMAKE_BUILD_TYPE=Release -DANDROID_STL=c++_static \
        -DCMAKE_EXE_LINKER_FLAGS="-static" -DMGE_EMULATED_TESTS=ON >/dev/null
    cmake --build build-arm64 >/dev/null
    "$QEMU" build-arm64/tests/mge_tests | tail -1
    "$QEMU" build-arm64/tools/host_runner/mge_host_runner | grep -E "steady-state|OK"
else
    echo "=== 2/3 arm64: SKIPPED (need NDK in \$ANDROID_HOME and qemu-user-static)"
fi

if [ -d "$ANDROID_HOME/platforms" ] && [ -n "$GRADLE" ]; then
    echo "=== 3/3 android: assembleDebug"
    ANDROID_HOME="$ANDROID_HOME" "$GRADLE" :app:assembleDebug -q
    ls -la app/build/outputs/apk/debug/app-debug.apk
else
    echo "=== 3/3 android: SKIPPED (need Android SDK + gradle; run scripts/setup-android-sdk.sh)"
fi

echo "=== verification complete"
