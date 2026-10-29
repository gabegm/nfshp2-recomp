#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root/.local/ps2-native-recomp"
export LD_LIBRARY_PATH="$root/.local/deps/lib64:$root/.local/deps/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
program=$(sed -n 's/^BOOT2 *= *//p' games/nfshp2/disc/SYSTEM.CNF | tr -d '\r')
program=${program#*\\}
program=${program%;*}
exec build/native-nfshp2/ps2run "games/nfshp2/disc/$program" \
  --disc games/nfshp2/disc.iso --window --memcard games/nfshp2/player.ps2 "$@"
