#!/usr/bin/env python3
"""Generate private, boot-only patches for the verified US startup movies."""
import hashlib
from pathlib import Path
import struct
import sys

ELF_SHA256 = '7335b8ed479481082c648958f4b5be4da65b74d042d0ab2f5237df801eafacc2'
PREFIXES = tuple(b'MOVIES\\INTRO\\' + name for name in (b'EALOGO_', b'NFSINTRO_', b'BBGLOGO_'))


def startup_paths(data):
    if len(data) < 52 or data[:6] != b'\x7fELF\x01\x01':
        raise ValueError('Expected a little-endian ELF32 executable.')
    table, = struct.unpack_from('<I', data, 28)
    stride, count = struct.unpack_from('<HH', data, 42)
    if stride < 32 or table + stride * count > len(data):
        raise ValueError('Invalid ELF program-header table.')
    addresses = set()
    for index in range(count):
        kind, offset, address, _, size, _, _, _ = struct.unpack_from('<8I', data, table + index * stride)
        if kind != 1:
            continue
        address &= 0x1FFFFFFF
        if offset + size > len(data) or address + size > (32 << 20):
            raise ValueError('Invalid ELF load segment.')
        section = data[offset:offset + size]
        for prefix in PREFIXES:
            start = 0
            while (at := section.find(prefix, start)) >= 0:
                start = at + 1
                if at and section[at - 1] != 0:
                    continue
                end = section.find(b'\0', at)
                suffix = section[at + len(prefix):end] if end >= 0 else b''
                if len(suffix) == 6 and suffix[:2].isalpha() and suffix[2:] == b'.AVI':
                    addresses.add(address + at)
    return sorted(addresses)


def main():
    if len(sys.argv) != 2:
        sys.exit('Usage: python3 scripts/nointro.py /path/to/original/game-executable')
    source = Path(sys.argv[1])
    if source.stat().st_size > (64 << 20):
        sys.exit('Expected the extracted game executable, not a disc image.')
    data = source.read_bytes()
    if hashlib.sha256(data).hexdigest() != ELF_SHA256:
        sys.exit('--nointro currently supports only the verified US SLUS-20362 executable.')
    addresses = startup_paths(data)
    if len(addresses) != 11:
        sys.exit('Unexpected startup movie layout; refusing to generate patches.')
    target = Path(__file__).resolve().parents[1] / '.local/nointro/SLUS-20362.txt'
    target.parent.mkdir(parents=True, exist_ok=True)
    # Empty only these paths at boot. No game instructions or movie data are copied.
    content = ''.join(f'0 {address:08X} 1 0\n' for address in addresses)
    temporary = target.with_suffix('.tmp')
    temporary.write_text(content)
    temporary.replace(target)
    print(target)


if __name__ == '__main__':
    main()
