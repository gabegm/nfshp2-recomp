#!/usr/bin/env python3
"""Fail closed if the Git index contains anything outside the reviewed setup."""
import subprocess
import sys

allowed = {
    '.gitignore', '.gitattributes', 'README.md', 'LICENSE', 'scripts/build.sh',
    'scripts/check-public-files.py',
    'scripts/play.sh', 'patches/runtime-compatibility.patch',
    'patches/LICENSE.upstream',
    'patches/presentation-performance.patch',
    'patches/graphics-profiling.patch',
    'patches/raster-specialization.patch',
    'patches/racing-performance.patch',
    'patches/psmt4-sampling.patch',
    'patches/render-cpu-profile.patch',
    'patches/palette-reuse.patch',
    'patches/upload-cache-eviction.patch',
    'scripts/apply-runtime-patches.py',
    'tests/test_runtime_patches.py',
    'tests/test_upload_cache.py',
    'scripts/fetch-widescreen.py',
    'OPTIMIZATION.md',
    'NATIVE_RENDERER.md',
    'scripts/analyze-vu-census.py',
    'tests/test_vu_census.py',
    'tests/test_ghidra_tools.py',
    'scripts/setup-ghidra.py',
    'scripts/analyze-renderer.py',
    'scripts/SetPs2Gp.java',
    'scripts/ExportVuReferences.java',
    'scripts/build-native-preview.sh',
    'scripts/test-native-preview.py',
    'tools/native_mesh_preview.cpp',
    'patches/vu-census.patch',
    'tests/test_widescreen.py',
    'scripts/nointro.py',
    'tests/test_nointro.py',
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
