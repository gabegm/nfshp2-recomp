#!/usr/bin/env python3
"""Prepare the community language-menu patch for the verified US executable."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import sys

ELF_SHA256 = '7335b8ed479481082c648958f4b5be4da65b74d042d0ab2f5237df801eafacc2'
ADDRESSES = (0x0016F908, 0x001EEC0C)
REPLACEMENT = 0x0000102D


def file_offset(data, address):
    table, = struct.unpack_from('<I', data, 28)
    stride, count = struct.unpack_from('<HH', data, 42)
    for index in range(count):
        kind, offset, virtual, _, size, _, _, _ = struct.unpack_from('<8I', data, table + index * stride)
        if kind == 1 and virtual <= address and address + 4 <= virtual + size:
            return offset + address - virtual
    raise ValueError(f'Patch address {address:08X} is outside the executable.')


def main():
    if len(sys.argv) != 2:
        sys.exit('Usage: python3 scripts/multilanguage.py /path/to/disc.iso')
    root = Path(__file__).resolve().parents[1]
    source = root / '.local/ps2-native-recomp/scripts/pnach.py'
    spec = importlib.util.spec_from_file_location('upstream_pnach', source)
    pnach = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pnach)
    serial, _, data = pnach.read_program(sys.argv[1])
    if serial != 'SLUS-20362' or hashlib.sha256(data).hexdigest() != ELF_SHA256:
        sys.exit('The multilanguage patch supports only the verified US SLUS-20362 executable.')
    for address in ADDRESSES:
        offset = file_offset(data, address)
        # Both verified sites are calls. The full executable hash above checks
        # the exact original bytes without publishing them in this setup.
        if struct.unpack_from('<I', data, offset)[0] >> 26 != 3:
            sys.exit(f'Expected a call at {address:08X}; refusing to patch.')
    target = root / '.local/multilanguage/SLUS-20362_1D2818AF.pnach'
    target.parent.mkdir(parents=True, exist_ok=True)
    content = ('gametitle=Need for Speed - Hot Pursuit 2 (USA)\n'
               '// Language-menu unlock by Felixthecat1970, shared in issue #1.\n'
               '[Unlock multilanguage for USA]\n' +
               ''.join(f'patch=0,EE,{0x20000000 | address:08X},extended,{REPLACEMENT:08X}\n'
                       for address in ADDRESSES))
    if not target.exists() or target.read_text() != content:
        temporary = target.with_suffix('.tmp')
        temporary.write_text(content)
        temporary.replace(target)
    print(target)


if __name__ == '__main__':
    main()
