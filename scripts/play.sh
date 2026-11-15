#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
if [ "${1:-}" = "--ra-login" ]; then
  exec python3 "$root/scripts/ra-login.py" "$root/.local/ra-credentials"
fi
performance=0
achievements=1
wide=0
multilanguage=0
modern_controls=0
nointro=0
resume=0
settings="$root/.local/play.conf"
config_args=()
cli_args=()
line_number=0
if [ -f "$settings" ]; then
  while IFS= read -r line || [ -n "$line" ]; do
    line_number=$((line_number + 1))
    line=${line%$'\r'}
    [[ "$line" =~ ^[[:space:]]*(#|$) ]] && continue
    if [[ "$line" != *=* ]]; then
      echo "$settings:$line_number: expected key=value" >&2
      exit 2
    fi
    key=${line%%=*}
    value=${line#*=}
    key=${key#"${key%%[![:space:]]*}"}
    key=${key%"${key##*[![:space:]]}"}
    value=${value#"${value%%[![:space:]]*}"}
    value=${value%"${value##*[![:space:]]}"}
    case "$key" in
      performance|widescreen|multilanguage|nointro|modern_controls|achievements)
        case "$value" in
          1|true|yes|on) enabled=1 ;;
          0|false|no|off) enabled=0 ;;
          *) echo "$settings:$line_number: $key must be true or false" >&2; exit 2 ;;
        esac
        case "$key" in
          performance) performance=$enabled ;;
          achievements) achievements=$enabled ;;
          widescreen) wide=$enabled ;;
          multilanguage) multilanguage=$enabled ;;
          nointro) nointro=$enabled ;;
          modern_controls) modern_controls=$enabled ;;
        esac ;;
      arg)
        [ -n "$value" ] || { echo "$settings:$line_number: arg cannot be empty" >&2; exit 2; }
        config_args+=("$value") ;;
      PS2_*)
        if [[ ! "$key" =~ ^PS2_[A-Z0-9_]+$ ]]; then
          echo "$settings:$line_number: invalid environment variable $key" >&2
          exit 2
        fi
        if [ -z "${!key+x}" ]; then export "$key=$value"; fi ;;
      *) echo "$settings:$line_number: unknown setting $key" >&2; exit 2 ;;
    esac
  done < "$settings"
fi
while [ $# -gt 0 ]; do
  case "$1" in
    --performance) performance=1; shift ;;
    --no-performance) performance=0; shift ;;
    --achievements) achievements=1; shift ;;
    --no-achievements) achievements=0; shift ;;
    --widescreen) wide=1; shift ;;
    --no-widescreen) wide=0; shift ;;
    --multilanguage) multilanguage=1; shift ;;
    --no-multilanguage) multilanguage=0; shift ;;
    --modern-controls) modern_controls=1; shift ;;
    --no-modern-controls) modern_controls=0; shift ;;
    --nointro) nointro=1; shift ;;
    --no-nointro) nointro=0; shift ;;
    --) shift; cli_args+=("$@"); break ;;
    *) cli_args+=("$1"); shift ;;
  esac
done
runner_args=("${config_args[@]}" "${cli_args[@]}")
for arg in "${runner_args[@]}"; do
  case "$arg" in --load-state|--replay) resume=1 ;; esac
done
if [ "$performance" = 1 ]; then
  # These settings are supported by the current upstream renderer.
  export PS2_DECODE="${PS2_DECODE:-0}"
  export PS2_TILE="${PS2_TILE:-4}"
fi
if [ "$achievements" = 1 ] && [ -f "$root/.local/ra-credentials" ]; then
  export NFSHP2_RA_CREDENTIALS="$root/.local/ra-credentials"
else
  unset NFSHP2_RA_CREDENTIALS
  if [ "$achievements" = 1 ]; then
    echo 'RetroAchievements: sign in once with bash scripts/play.sh --ra-login to enable casual achievements.' >&2
  fi
fi
cd "${NFSHP2_UPSTREAM:-$root/.local/ps2-native-recomp}"
export LD_LIBRARY_PATH="$root/.local/deps/lib64:$root/.local/deps/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
game=nfshp2
extra=()
if [ "$wide" = 1 ]; then game+=-wide; fi
if [ "$multilanguage" = 1 ]; then game+=-lang; fi
if [ "$modern_controls" = 1 ]; then game+=-modern; fi
if [ "$wide" = 1 ] || [ "$multilanguage" = 1 ] || [ "$modern_controls" = 1 ]; then
  if [ ! -x "build/native-$game/ps2run" ] || [ ! -f "games/$game/patches.txt" ]; then
    build=(bash scripts/build.sh /path/to/disc.iso)
    if [ "$wide" = 1 ]; then build+=(--widescreen); fi
    if [ "$multilanguage" = 1 ]; then build+=(--multilanguage); fi
    if [ "$modern_controls" = 1 ]; then build+=(--modern-controls); fi
    echo "Build this profile first: ${build[*]}" >&2
    exit 1
  fi
  mkdir -p games/nfshp2
  export PS2_KEYS="${PS2_KEYS:-$PWD/games/nfshp2/keys.ini}"
  if [ "$wide" = 1 ]; then extra+=(--widescreen); fi
  extra+=(--patches "games/$game/patches.txt" --quick-dir "games/$game")
fi
program=$(sed -n 's/^BOOT2 *= *//p' "games/$game/disc/SYSTEM.CNF" | tr -d '\r')
program=${program#*\\}
program=${program%;*}
elf="games/$game/disc/$program"
if [ "$nointro" = 1 ] && [ "$resume" = 0 ]; then
  intro_patch=$(python3 "$root/scripts/nointro.py" "$elf")
  extra+=(--patches "$intro_patch")
fi
if [ "$wide" = 1 ] || [ "$multilanguage" = 1 ] || [ "$modern_controls" = 1 ]; then elf="games/$game/patched/$program"; fi
if [ "$modern_controls" = 1 ]; then
  echo 'Trigger layouts: Options > Controller > Configuration, press left/right to choose Setup E-H; then Options > Save/Load > Save. Setups A-D use face buttons.' >&2
fi
exec "build/native-$game/ps2run" "$elf" \
  --disc "games/$game/disc.iso" --window --memcard games/nfshp2/player.ps2 "${extra[@]}" "${runner_args[@]}"
