#!/usr/bin/env python3
"""Fetch a pinned community patch privately after validating the input disc."""
import hashlib
import importlib.util
from pathlib import Path
import sys
import urllib.request

REVISION = '8afa626253d5fdcb23e660314779c91e6b8cbb8a'
NAME = 'SLUS-20362_1D2818AF.pnach'
PATCH_SHA256 = '44e4db575bf821702fc7f015fd3ba7333c58bfff8e273e4879a3ac56a2c36f46'
ELF_SHA256 = '7335b8ed479481082c648958f4b5be4da65b74d042d0ab2f5237df801eafacc2'
URL = f'https://raw.githubusercontent.com/PCSX2/pcsx2_patches/{REVISION}/patches/{NAME}'


def main():
    if len(sys.argv) != 2:
        sys.exit('Usage: python3 scripts/fetch-widescreen.py /path/to/disc.iso')
    root = Path(__file__).resolve().parents[1]
    source = root / '.local/ps2-native-recomp/scripts/pnach.py'
    spec = importlib.util.spec_from_file_location('upstream_pnach', source)
    pnach = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pnach)
    serial, _, data = pnach.read_program(sys.argv[1])
    if serial != 'SLUS-20362' or hashlib.sha256(data).hexdigest() != ELF_SHA256:
        sys.exit('Widescreen currently supports only the verified US SLUS-20362 executable. '
                 'The PAL release can share its CRC, so CRC alone is insufficient.')
    target = root / '.local/widescreen' / NAME
    if target.exists():
        patch = target.read_bytes()
    else:
        print(f'Downloading PCSX2 patch revision {REVISION} into .local/', file=sys.stderr)
        with urllib.request.urlopen(URL, timeout=30) as response:
            patch = response.read(1 << 20)
    if hashlib.sha256(patch).hexdigest() != PATCH_SHA256:
        sys.exit('Widescreen patch checksum mismatch; refusing to apply it.')
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists():
        temporary = target.with_suffix('.download')
        temporary.write_bytes(patch)
        temporary.replace(target)
    print(target)


if __name__ == '__main__':
    main()
