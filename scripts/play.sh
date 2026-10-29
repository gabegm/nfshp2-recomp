#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
if [ "${1:-}" = "--performance" ]; then
  shift
  # On the tested Intel laptop, queue overhead outweighs the extra VU thread,
  # and sampling textures directly is cheaper than the predecode pass.
  export PS2_NO_VU_THREAD=1
  export PS2_DECODE="${PS2_DECODE:-0}"
  export PS2_SCALE="${PS2_SCALE:-1}"
  # The default cubic sharpening filter costs substantial time on the UHD 620.
  export PS2_LOOK="${PS2_LOOK:-crisp}"
fi
cd "$root/.local/ps2-native-recomp"
export LD_LIBRARY_PATH="$root/.local/deps/lib64:$root/.local/deps/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
program=$(sed -n 's/^BOOT2 *= *//p' games/nfshp2/disc/SYSTEM.CNF | tr -d '\r')
program=${program#*\\}
program=${program%;*}
exec build/native-nfshp2/ps2run "games/nfshp2/disc/$program" \
  --disc games/nfshp2/disc.iso --window --memcard games/nfshp2/player.ps2 "$@"
