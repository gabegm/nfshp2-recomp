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
patch="$root/patches/runtime-compatibility.patch"
if git -C "$upstream" apply --reverse --check "$patch" 2>/dev/null; then
  : # Compatibility changes already installed.
else
  git -C "$upstream" apply --check "$patch"
  git -C "$upstream" apply "$patch"
fi
# Upstream reuses a game's previous image. Refuse an accidental disc switch.
if [ -e "$upstream/games/nfshp2/disc.iso" ] && ! [ "$image" -ef "$upstream/games/nfshp2/disc.iso" ]; then
  echo 'A different image is already configured; use a separate checkout for another disc.' >&2
  exit 1
fi
# Run from private storage: runtime caches, reports and saves also stay here.
cd "$upstream"
export PS2_JOBS="${PS2_JOBS:-4}"
export CCACHE_DIR="$root/.local/ccache"
bash scripts/setup.sh --native-only
bash scripts/port.sh nfshp2 "$image" --native-only "$@"
