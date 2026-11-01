#!/usr/bin/env python3
"""Build an isolated, ROM-free Play! Vulkan GS smoke test (Linux)."""
import argparse
import ctypes.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
LOCAL = ROOT / '.local/play-backend'
# Explicit revisions, including transitive dependencies; never follow a branch.
SOURCES = (
    ('Play', 'https://github.com/jpd002/Play-.git', '1f27232772e484bfb79364b751b84f41f4c87481', ('License.txt',)),
    ('Play/deps/Dependencies', 'https://github.com/jpd002/Play-Dependencies.git', '8a5f6b1dea0b888b5a42b821e79470142bbea4e3', ()),
    ('Play/deps/Framework', 'https://github.com/jpd002/Play--Framework.git', '587f278917acc0026bf5fc34b39f995fc26bd015', ('License.txt',)),
    ('Play/deps/Nuanceur', 'https://github.com/jpd002/Nuanceur.git', 'd96578b5a18edc67a268ed4811f0a035c56de7a0', ('License.txt',)),
    # Only headers are required through upstream's GS interrupt declarations.
    ('Play/deps/CodeGen', 'https://github.com/jpd002/Play--CodeGen.git', 'a5009f7dca062695b8e5aebbd71e67b4ddfa9251', ('License.txt',)),
    ('Play/deps/Dependencies/zstd', 'https://github.com/facebook/zstd.git', 'f8745da6ff1ad1e7bab384bd1f9d742439278e99', ('LICENSE', 'zlibWrapper/gzguts.h')),
    ('Play/deps/Dependencies/ghc_filesystem', 'https://github.com/gulrak/filesystem.git', '2a8b380f8d4e77b389c42a194ab9c70d8e3a0f1e', ('LICENSE',)),
    ('Play/deps/Dependencies/zlib', 'https://github.com/madler/zlib.git', '5a82f71ed1dfc0bec044d9702463dbdf84ea3b71', ('LICENSE',)),
    ('Vulkan-Headers', 'https://github.com/KhronosGroup/Vulkan-Headers.git', '9fbc2a782ee3f8a4db64e7d991e23da75e893f44', ('LICENSE.md', 'LICENSES/Apache-2.0.txt', 'LICENSES/MIT.txt')),
)


def run(args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)


def output(args):
    return run(args, stdout=subprocess.PIPE, text=True).stdout.strip()


def source(path, url, revision, fetch):
    if not (path / '.git').exists():
        if not fetch:
            raise RuntimeError(f'Missing {path}; rerun with --fetch to obtain pinned sources')
        if path.exists() and any(path.iterdir()):
            raise RuntimeError(f'Refusing to replace nonempty directory: {path}')
        path.mkdir(parents=True, exist_ok=True)
        run(['git', 'init', path], stdout=subprocess.DEVNULL)
        run(['git', '-C', path, 'remote', 'add', 'origin', url])
    if output(['git', '-C', path, 'remote', 'get-url', 'origin']) != url:
        raise RuntimeError(f'Unexpected upstream URL: {path}')
    if output(['git', '-C', path, 'status', '--porcelain', '--untracked-files=no', '--ignore-submodules=all']):
        raise RuntimeError(f'Local source changes; refusing to overwrite: {path}')
    head = subprocess.run(['git', '-C', str(path), 'rev-parse', '--verify', 'HEAD'],
                          capture_output=True, text=True)
    if head.returncode or head.stdout.strip() != revision:
        if not fetch:
            raise RuntimeError(f'Wrong revision in {path}; use --fetch for the pinned revision')
        run(['git', '-C', path, 'fetch', '--depth', '1', 'origin', revision])
        run(['git', '-C', path, 'checkout', '--detach', revision])
    if output(['git', '-C', path, 'rev-parse', 'HEAD']) != revision:
        raise RuntimeError(f'Revision verification failed: {path}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fetch', action='store_true', help='fetch missing/wrong pinned sources')
    parser.add_argument('--run', action='store_true', help='run synthetic checks on a hardware GPU')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--vulkan-library', type=Path, help='absolute path to the system Vulkan loader')
    args = parser.parse_args()
    if not sys.platform.startswith('linux') or args.jobs < 1:
        parser.error('Linux and a positive job count are required')
    LOCAL.mkdir(parents=True, exist_ok=True)
    try:
        for relative, url, revision, notices in SOURCES:
            path = LOCAL / relative
            source(path, url, revision, args.fetch)
            for notice in notices:
                dest = LOCAL / 'notices' / relative / notice
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path / notice, dest)
        build = LOCAL / 'public-build'
        env = os.environ.copy()
        env['CCACHE_DIR'] = str(ROOT / '.local/ccache')
        configure = ['cmake', '-S', ROOT / 'tools/play-gs-smoke', '-B', build,
                     '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                     f'-DPLAY_SOURCE={LOCAL / "Play"}',
                     f'-DVulkan_INCLUDE_DIR={LOCAL / "Vulkan-Headers/include"}']
        library = args.vulkan_library
        # Runtime-only installations often lack an unversioned development symlink.
        if library is None:
            soname = ctypes.util.find_library('vulkan')
            if soname:
                for folder in ('/usr/lib64', '/usr/lib/x86_64-linux-gnu', '/lib/x86_64-linux-gnu', '/usr/lib/aarch64-linux-gnu'):
                    candidate = Path(folder) / soname
                    if candidate.is_file():
                        library = candidate
                        break
        if library:
            if not library.is_absolute() or not library.is_file():
                raise RuntimeError('--vulkan-library must name an existing absolute library path')
            configure.append(f'-DVulkan_LIBRARY={library}')
        run(configure, env=env)
        run(['cmake', '--build', build, '--target', 'play-gs-smoke', '-j', args.jobs], env=env)
        (LOCAL / 'sources.json').write_text(json.dumps([
            {'path': p, 'url': u, 'revision': r} for p,u,r,_ in SOURCES], indent=2)+'\n')
        print(f'Built {build / "play-gs-smoke"}', flush=True)
        if args.run:
            data = LOCAL / 'smoke-data'
            data.mkdir(exist_ok=True)
            run([build / 'play-gs-smoke', data], cwd=data, env=env, timeout=90)
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
