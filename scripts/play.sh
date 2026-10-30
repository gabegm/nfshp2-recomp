#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
performance=0
wide=0
nointro=0
resume=0
runner_args=()
while [ $# -gt 0 ]; do
  case "$1" in
    --performance) performance=1; shift ;;
    --widescreen) wide=1; shift ;;
    --nointro) nointro=1; shift ;;
    --) shift; runner_args+=("$@"); break ;;
    *) runner_args+=("$1"); shift ;;
  esac
done
for arg in "${runner_args[@]}"; do
  case "$arg" in --load-state|--replay) resume=1 ;; esac
done
if [ "$performance" = 1 ]; then
  # On the tested Intel laptop, queue overhead outweighs the extra VU thread,
  # and sampling textures directly is cheaper than the predecode pass.
  export PS2_NO_VU_THREAD=1
  export PS2_DECODE="${PS2_DECODE:-0}"
  export PS2_SCALE="${PS2_SCALE:-1}"
  # Smaller raster tiles reduced GPU time in the measured UHD 620 race.
  export PS2_TILE="${PS2_TILE:-4}"
  # Specialize uniform raster batches; unsupported states use the generic path.
  export PS2_RASTER_SPECIALIZE="${PS2_RASTER_SPECIALIZE:-1}"
  # The default cubic sharpening filter costs substantial time on the UHD 620.
  export PS2_LOOK="${PS2_LOOK:-crisp}"
fi
cd "$root/.local/ps2-native-recomp"
export LD_LIBRARY_PATH="$root/.local/deps/lib64:$root/.local/deps/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
game=nfshp2
extra=()
if [ "$wide" = 1 ]; then
  game=nfshp2-wide
  if [ ! -x "build/native-$game/ps2run" ] || [ ! -f "games/$game/patches.txt" ]; then
    echo 'Build widescreen first: bash scripts/build.sh /path/to/disc.iso --widescreen' >&2
    exit 1
  fi
  mkdir -p games/nfshp2
  export PS2_KEYS="${PS2_KEYS:-$PWD/games/nfshp2/keys.ini}"
  extra=(--widescreen --patches "games/$game/patches.txt" --quick-dir "games/$game")
fi
program=$(sed -n 's/^BOOT2 *= *//p' "games/$game/disc/SYSTEM.CNF" | tr -d '\r')
program=${program#*\\}
program=${program%;*}
elf="games/$game/disc/$program"
if [ "$nointro" = 1 ] && [ "$resume" = 0 ]; then
  intro_patch=$(python3 "$root/scripts/nointro.py" "$elf")
  extra+=(--patches "$intro_patch")
fi
if [ "$wide" = 1 ]; then elf="games/$game/patched/$program"; fi
exec "build/native-$game/ps2run" "$elf" \
  --disc "games/$game/disc.iso" --window --memcard games/nfshp2/player.ps2 "${extra[@]}" "${runner_args[@]}"
