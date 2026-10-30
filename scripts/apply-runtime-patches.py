#!/usr/bin/env python3
"""Validate an overlapping runtime patch stack privately before updating sources."""
from pathlib import Path, PurePosixPath
import os
import shutil
import stat
import subprocess
import sys
import tempfile


def git(directory, *args, check=True):
    result = subprocess.run(['git', '-C', str(directory), *map(str, args)],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if check and result.returncode:
        raise RuntimeError(result.stderr.decode(errors='replace').strip())
    return result


def apply_series(upstream, patches):
    upstream = Path(upstream).resolve()
    patches = [Path(patch).resolve() for patch in patches]
    if not patches:
        raise ValueError('At least one patch is required.')
    paths = set()
    for patch in patches:
        for row in git(upstream, 'apply', '--numstat', '-z', patch).stdout.split(b'\0'):
            if not row:
                continue
            name = row.decode().split('\t', 2)[2]
            path = PurePosixPath(name)
            if path.is_absolute() or any(part in ('..', '.git') for part in path.parts):
                raise ValueError(f'Unsafe patch path: {name}')
            source = upstream / name
            if source.is_symlink() or not source.is_file() or not source.resolve().is_relative_to(upstream):
                raise ValueError(f'Expected an existing runtime source: {name}')
            paths.add(name)
    paths = sorted(paths)
    original = {name: (upstream / name).read_bytes() for name in paths}
    with tempfile.TemporaryDirectory(prefix='runtime-patches-', dir=upstream.parent) as folder:
        staging = Path(folder)
        git(staging, 'init', '-q')
        for name in paths:
            target = staging / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(upstream / name, target)
        # Later patches can change earlier patches' context. Peel off the stack
        # in reverse order here, without modifying the user's checkout.
        for patch in reversed(patches):
            if git(staging, 'apply', '--reverse', '--check', patch, check=False).returncode == 0:
                git(staging, 'apply', '--reverse', patch)
        for patch in patches:
            git(staging, 'apply', '--check', patch)
            git(staging, 'apply', patch)
        changed = {name: (staging / name).read_bytes() for name in paths
                   if (staging / name).read_bytes() != original[name]}
        if any((upstream / name).read_bytes() != data for name, data in original.items()):
            raise RuntimeError('Runtime sources changed during validation; retry after editing finishes.')
        for name, data in changed.items():
            target = upstream / name
            temporary = None
            try:
                with tempfile.NamedTemporaryFile(dir=target.parent, prefix='.' + target.name + '-', delete=False) as output:
                    temporary = Path(output.name)
                    output.write(data)
                temporary.chmod(stat.S_IMODE(target.stat().st_mode))
                os.replace(temporary, target)
            finally:
                if temporary is not None:
                    temporary.unlink(missing_ok=True)
    return len(changed)


if __name__ == '__main__':
    if len(sys.argv) < 3:
        sys.exit('Usage: apply-runtime-patches.py UPSTREAM PATCH [PATCH ...]')
    try:
        changed = apply_series(sys.argv[1], sys.argv[2:])
    except (OSError, ValueError, RuntimeError) as error:
        sys.exit(f'Runtime patch validation failed: {error}')
    print(f'Verified {len(sys.argv) - 2} runtime patches; updated {changed} source files.')
