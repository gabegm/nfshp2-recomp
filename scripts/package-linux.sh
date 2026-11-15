#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
if [ $# -ne 2 ]; then
  echo 'Usage: bash scripts/package-linux.sh VERSION OUTPUT.tar.gz' >&2
  exit 2
fi
version=$1
output=$2
if [[ ! "$version" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo 'VERSION must look like v0.2.0' >&2
  exit 2
fi
case "$output" in
  *"$version"-Linux-x64.tar.gz) ;;
  *) echo 'OUTPUT must end in VERSION-Linux-x64.tar.gz' >&2; exit 2 ;;
esac
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
chmod 0755 "$stage"
mkdir -p "$stage/scripts" "$stage/patches" "$stage/src"
install -m 0755 "$root/scripts/nfshp2-launcher" "$stage/nfshp2-launcher"
cp "$root/README.md" "$root/LICENSE" "$root/recomplauncher.json" "$stage/"
for file in build.sh play.sh ra-login.py fetch-widescreen.py multilanguage.py nointro.py apply-runtime-patches.py; do
  cp "$root/scripts/$file" "$stage/scripts/"
done
for file in runtime-compatibility.patch controller-input.patch controller-menu.patch controller-menu-input.patch controller-disconnect.patch retroachievements.patch retroachievements-state.patch; do
  cp "$root/patches/$file" "$stage/patches/"
done
cp "$root/src/retroachievements.cpp" "$root/src/retroachievements.hpp" "$stage/src/"
mkdir -p "$(dirname "$output")"
tar -C "$stage" -czf "$output" .
echo "Created $output"
