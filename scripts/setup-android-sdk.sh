#!/usr/bin/env bash
# Installs the Android SDK pieces the app build needs into $ANDROID_HOME
# (default: ~/android-sdk). Idempotent — safe to re-run; skips what's present.
# Used by remote dev sessions and CI so the Android build is verifiable
# without a developer machine.
set -euo pipefail

ANDROID_HOME="${ANDROID_HOME:-$HOME/android-sdk}"
CMDLINE_TOOLS_ZIP="commandlinetools-linux-11076708_latest.zip"
NDK_VERSION="27.0.12077973"
PLATFORM="android-35"
BUILD_TOOLS="35.0.0"
CMAKE_VERSION="3.22.1"

export ANDROID_HOME

if [ ! -x "$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" ]; then
    echo "== Installing Android command-line tools to $ANDROID_HOME"
    mkdir -p "$ANDROID_HOME/cmdline-tools"
    tmp="$(mktemp -d)"
    curl -fsSL -o "$tmp/tools.zip" "https://dl.google.com/android/repository/$CMDLINE_TOOLS_ZIP"
    unzip -q "$tmp/tools.zip" -d "$tmp"
    rm -rf "$ANDROID_HOME/cmdline-tools/latest"
    mv "$tmp/cmdline-tools" "$ANDROID_HOME/cmdline-tools/latest"
    rm -rf "$tmp"
fi

SDKMANAGER="$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager"

echo "== Accepting licenses"
yes | "$SDKMANAGER" --licenses >/dev/null || true

echo "== Installing platform=$PLATFORM build-tools=$BUILD_TOOLS ndk=$NDK_VERSION cmake=$CMAKE_VERSION"
"$SDKMANAGER" --install \
    "platforms;$PLATFORM" \
    "build-tools;$BUILD_TOOLS" \
    "platform-tools" \
    "ndk;$NDK_VERSION" \
    "cmake;$CMAKE_VERSION" >/dev/null

echo "== Done. Set for builds:"
echo "   export ANDROID_HOME=$ANDROID_HOME"
