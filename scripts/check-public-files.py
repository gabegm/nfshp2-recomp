#!/usr/bin/env python3
"""Fail closed if the Git index contains anything outside the reviewed setup."""
import subprocess
import sys

allowed = {
    '.gitignore', 'README.md', 'LICENSE', 'scripts/build.sh',
    'scripts/check-public-files.py',
    'scripts/play.sh', 'patches/runtime-compatibility.patch',
    'patches/LICENSE.upstream',
}
result = subprocess.run(
    ['git', 'ls-files', '-z'], check=True, stdout=subprocess.PIPE,
)
files = set(result.stdout.decode().rstrip('\0').split('\0')) - {''}
unexpected = sorted(files - allowed)
if unexpected:
    sys.exit('Refusing publication; unexpected tracked files:\n' + '\n'.join(unexpected))
print(f'Index contains only {len(files)} allowlisted setup files.')
print('Review their contents and all commit history before publishing; filenames alone do not prove safety.')
