#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
revision=ba3d4712a9291abe1767c8aa0aa8dc4eeda1db71
upstream="$root/.local/ps2-native-recomp"
if [ $# -lt 1 ] || [ ! -f "$1" ]; then
  echo 'Usage: bash scripts/build.sh "/absolute/path/to/your/disc.iso" [port.sh options]' >&2
  exit 2
fi
image="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
case "${image,,}" in
  *.iso) ;;
  *) echo 'Provide an unpacked .iso disc image.' >&2; exit 2 ;;
esac
shift
game=nfshp2
wide=0
port_args=()
for arg in "$@"; do
  if [ "$arg" = "--widescreen" ]; then
    wide=1
    game=nfshp2-wide
  else
    port_args+=("$arg")
  fi
done
if [ -d "$root/.local/deps" ]; then
  export CMAKE_PREFIX_PATH="$root/.local/deps${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
  export PKG_CONFIG_PATH="$root/.local/deps/lib64/pkgconfig:$root/.local/deps/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
  export LD_LIBRARY_PATH="$root/.local/deps/lib64:$root/.local/deps/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
command -v pkg-config >/dev/null
pkg-config --exists sdl2 || {
  echo 'SDL2 development files are required (Fedora/Aurora container: SDL2-devel).' >&2
  exit 1
}
if pkg-config --exists x11; then
  export CPLUS_INCLUDE_PATH="$(pkg-config --variable=includedir x11)${CPLUS_INCLUDE_PATH:+:$CPLUS_INCLUDE_PATH}"
fi
mkdir -p "$root/.local"
if [ ! -d "$upstream/.git" ]; then
  git clone https://github.com/exoteric33/ps2-native-recomp.git "$upstream"
  git -C "$upstream" checkout --detach "$revision"
fi
if [ "$(git -C "$upstream" rev-parse HEAD)" != "$revision" ]; then
  echo "Expected upstream revision $revision; inspect the local checkout before proceeding." >&2
  exit 1
fi
python3 "$root/scripts/apply-runtime-patches.py" "$upstream" \
  "$root/patches/runtime-compatibility.patch" \
  "$root/patches/presentation-performance.patch" \
  "$root/patches/graphics-profiling.patch" \
  "$root/patches/raster-specialization.patch" \
  "$root/patches/racing-performance.patch" \
  "$root/patches/psmt4-sampling.patch" \
  "$root/patches/render-cpu-profile.patch" \
  "$root/patches/palette-reuse.patch" \
  "$root/patches/upload-cache-eviction.patch" \
  "$root/patches/vu-census.patch" \
  "$root/patches/native-draw-capture.patch" \
  "$root/patches/native-frame-proof.patch" \
  "$root/patches/native-target-bridge.patch"
# Install the hand-written shared bridge without rebuilding on unchanged copies.
bridge_header="$upstream/runtime/src/gs/gpu/native_target_bridge.hpp"
if ! cmp -s "$root/tools/native_target_bridge.hpp" "$bridge_header"; then
  cp "$root/tools/native_target_bridge.hpp" "$bridge_header"
fi
if [ "$wide" = 1 ]; then
  wide_patch=$(python3 "$root/scripts/fetch-widescreen.py" "$image")
  port_args+=(--widescreen --pnach "$wide_patch" --pnach-section 'Widescreen 16:9' --pnach-section 'HUD fixed for widescreen')
  # Reuse already observed vector programs; all remain private game data.
  mkdir -p "$upstream/games/$game/vu1"
  for program in "$upstream/games/nfshp2/vu1/"*.bin; do
    [ -f "$program" ] || continue
    [ -e "$upstream/games/$game/vu1/$(basename "$program")" ] || cp "$program" "$upstream/games/$game/vu1/"
  done
fi
# Upstream reuses a game's previous image. Refuse an accidental disc switch.
if [ -e "$upstream/games/$game/disc.iso" ] && ! [ "$image" -ef "$upstream/games/$game/disc.iso" ]; then
  echo 'A different image is already configured; use a separate checkout for another disc.' >&2
  exit 1
fi
# Run from private storage: runtime caches, reports and saves also stay here.
cd "$upstream"
export PS2_JOBS="${PS2_JOBS:-4}"
export CCACHE_DIR="$root/.local/ccache"
bash scripts/setup.sh --native-only
bash scripts/port.sh "$game" "$image" --native-only "${port_args[@]}"
if [ "$wide" = 1 ]; then
  echo "From the setup directory, play with: bash scripts/play.sh --widescreen --performance"
fi
