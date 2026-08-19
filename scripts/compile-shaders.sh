#!/usr/bin/env bash
# Compiles engine/shaders/*.{vert,frag} to SPIR-V and embeds them as C headers
# in engine/generated/shaders/. Generated headers are COMMITTED so that
# builds (Android, CI) don't need glslang installed — rerun this script after
# editing any shader and commit the result.
set -euo pipefail
cd "$(dirname "$0")/.."

SRC_DIR=engine/shaders
OUT_DIR=engine/generated/shaders
mkdir -p "$OUT_DIR"

for src in "$SRC_DIR"/*.vert "$SRC_DIR"/*.frag; do
    name="$(basename "$src" | tr '.' '_')"
    spv="$(mktemp)"
    glslangValidator -V --quiet -o "$spv" "$src"
    header="$OUT_DIR/${name}.h"
    {
        echo "// Generated from $src by scripts/compile-shaders.sh — do not edit."
        echo "#pragma once"
        echo "#include <cstdint>"
        echo "#include <cstddef>"
        echo "static const uint32_t k_spv_${name}[] = {"
        od -A n -t x4 -v "$spv" | tr -s ' ' '\n' | grep -v '^$' | sed 's/^/0x/; s/$/,/'
        echo "};"
        echo "static const size_t k_spv_${name}_size = sizeof(k_spv_${name});"
    } > "$header"
    rm -f "$spv"
    echo "embedded $src -> $header"
done
