#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
wgpu="$root/.local/ps2-native-recomp/third_party/wgpu-native"
if [ ! -f "$wgpu/include/webgpu/webgpu.h" ] || [ ! -f "$wgpu/lib/libwgpu_native.so" ]; then
  echo 'Set up the private runtime first; this preview uses its wgpu-native dependency.' >&2
  exit 1
fi
mkdir -p "$root/.local/native-preview"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -I "$wgpu/include" \
  "$root/tools/native_mesh_preview.cpp" -L "$wgpu/lib" -Wl,-rpath,"$wgpu/lib" \
  -lwgpu_native -ldl -lpthread -lm -o "$root/.local/native-preview/native-mesh-preview"
echo "Built $root/.local/native-preview/native-mesh-preview"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -I "$wgpu/include" \
  "$root/tools/native_target_bridge_test.cpp" -L "$wgpu/lib" -Wl,-rpath,"$wgpu/lib" \
  -lwgpu_native -ldl -lpthread -lm -o "$root/.local/native-preview/native-target-bridge-test"
echo "Built $root/.local/native-preview/native-target-bridge-test"
