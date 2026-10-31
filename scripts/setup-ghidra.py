#!/usr/bin/env python3
"""Install pinned research tools privately; requires an existing 64-bit JDK 21."""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import stat
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / '.local/tools'
ASSETS = [
    ('ghidra_12.1.4_PUBLIC_20260921.zip',
     'https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.4_build/ghidra_12.1.4_PUBLIC_20260921.zip',
     'ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db'),
    ('ghidra_12.1.4_PUBLIC_20261029_ghidra-emotionengine-reloaded.zip',
     'https://github.com/chaoticgd/ghidra-emotionengine-reloaded/releases/download/v2.1.39/ghidra_12.1.4_PUBLIC_20261029_ghidra-emotionengine-reloaded.zip',
     '088369c4d7de43cc95dbf254936dfed45eae5eddfcaf8b928189344bcc6a4dac'),
]


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def unpack(archive, destination):
    destination.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as source:
        for entry in source.infolist():
            name = PurePosixPath(entry.filename)
            mode = entry.external_attr >> 16
            target = destination.joinpath(*name.parts)
            if (name.is_absolute() or '..' in name.parts or stat.S_ISLNK(mode) or
                    target.is_symlink() or not target.resolve().is_relative_to(destination.resolve())):
                raise ValueError(f'Unsafe archive member: {entry.filename}')
        source.extractall(destination)
        if os.name != 'nt':
            for entry in source.infolist():
                mode = (entry.external_attr >> 16) & 0o777
                if mode:
                    destination.joinpath(*PurePosixPath(entry.filename).parts).chmod(mode)


def install():
    TOOLS.mkdir(parents=True, exist_ok=True)
    ghidra = TOOLS / 'ghidra_12.1.4_PUBLIC'
    for index, (name, url, expected) in enumerate(ASSETS):
        archive = TOOLS / name
        if not archive.exists():
            temporary = archive.with_suffix('.part')
            try:
                with urllib.request.urlopen(url, timeout=60) as response, temporary.open('wb') as target:
                    while block := response.read(4 << 20):
                        target.write(block)
                if digest(temporary) != expected:
                    raise ValueError(f'Unexpected SHA256 for {name}')
                temporary.replace(archive)
            finally:
                temporary.unlink(missing_ok=True)
        if digest(archive) != expected:
            raise ValueError(f'Cached archive failed SHA256 verification: {name}')
        marker = TOOLS / (name + '.installed.json')
        if not marker.exists() or not (ghidra / 'support/analyzeHeadless').exists():
            destination = TOOLS if index == 0 else ghidra / 'Ghidra/Extensions'
            unpack(archive, destination)
            marker.write_text(json.dumps(dict(url=url, sha256=expected)) + '\n')
    return ghidra


if __name__ == '__main__':
    print(install())
