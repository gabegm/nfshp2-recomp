#!/usr/bin/env python3
"""Build a separate Linux coverage runner, reusing already-built game objects."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
RUNTIME=ROOT/'.local/ps2-native-recomp'


def cache_values(path):
    result={}
    for line in path.read_text().splitlines():
        if line and not line.startswith(('#','//')) and '=' in line and ':' in line.split('=',1)[0]:
            name,value=line.split('=',1);result[name.split(':',1)[0]]=value
    return result


def link_arguments(command, runtime_library, diagnostic_library, output, compiler):
    args=shlex.split(command)
    if args[:2]==[':','&&']:args=args[2:]
    if args[-2:]==['&&',':']:args=args[:-2]
    if not args or args[0]!=compiler or any(a in ('&&','||',';',':','|') or a.startswith('@') for a in args):
        raise ValueError('Unsupported Ninja link command; no shell command will be executed.')
    if args.count(str(runtime_library))!=1 or args.count('-o')!=1:
        raise ValueError('Expected one runtime library and one executable output.')
    index=args.index('-o')+1
    if index>=len(args) or Path(args[index]).name!='ps2run':raise ValueError('Unexpected game output.')
    args[index]=str(output)
    args=[str(diagnostic_library) if a==str(runtime_library) else a for a in args]
    # Keep dependency bookkeeping out of the ordinary build, too.
    args=[f'--dependency-file={output.parent / "link.d"}' if a.startswith('--dependency-file=') else a for a in args]
    return args


def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for data in iter(lambda:f.read(1<<20),b''):h.update(data)
    return h.hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--widescreen',action='store_true')
    parser.add_argument('--jobs',type=int,default=2)
    args=parser.parse_args()
    if not sys.platform.startswith('linux'):parser.error('This diagnostic build helper currently supports Linux only.')
    if args.jobs<1:parser.error('Jobs must be positive.')
    game='nfshp2-wide' if args.widescreen else 'nfshp2'
    normal=RUNTIME/f'build/native-{game}'
    try:
        game_cache=cache_values(normal/'CMakeCache.txt')
        ordinary_library=Path(game_cache['PS2_RUNTIME_LIB'])
        runtime_cache=cache_values(ordinary_library.parent/'CMakeCache.txt')
        old_hash=digest(normal/'ps2run')
    except (OSError,KeyError) as error:parser.error(f'Build the ordinary game first: {error}')
    compiler=runtime_cache['CMAKE_CXX_COMPILER']
    options={key:runtime_cache[key] for key in ('CMAKE_CXX_COMPILER','CMAKE_CXX_FLAGS','CMAKE_BUILD_TYPE','CMAKE_CXX_COMPILER_LAUNCHER') if runtime_cache.get(key)}
    identity=hashlib.sha256(json.dumps(options,sort_keys=True).encode()).hexdigest()[:12]
    private=ROOT/'.local/native-coverage-build'
    diagnostic_runtime=private/f'runtime-{identity}'
    output=private/game/'ps2run';output.parent.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy();env['CCACHE_DIR']=str(ROOT/'.local/ccache')
    env['CMAKE_PREFIX_PATH']=str(ROOT/'.local/deps')+(':'+env['CMAKE_PREFIX_PATH'] if env.get('CMAKE_PREFIX_PATH') else '')
    configure=['cmake','-S',str(RUNTIME),'-B',str(diagnostic_runtime),'-G','Ninja',
               '-DPS2_GENERATED=','-DPS2_RUNTIME_LIB=','-DPS2_NATIVE_COVERAGE_DIAGNOSTIC=ON']
    configure += [f'-D{k}={v}' for k,v in options.items()]
    subprocess.run(configure,env=env,check=True)
    subprocess.run(['cmake','--build',str(diagnostic_runtime),'--target','ps2runtime','-j',str(args.jobs)],env=env,check=True)
    commands=subprocess.check_output(['ninja','-C',str(normal),'-t','commands','ps2run'],text=True)
    try:command=link_arguments(commands.splitlines()[-1],ordinary_library,diagnostic_runtime/'libps2runtime.a',output,compiler)
    except (ValueError,IndexError) as error:parser.error(str(error))
    subprocess.run(command,cwd=normal,env=env,check=True)
    if digest(normal/'ps2run')!=old_hash:raise SystemExit('Ordinary executable changed during diagnostic build; inspect concurrent work.')
    (output.parent/'build.json').write_text(json.dumps(dict(version=1,ordinary_executable_sha256=old_hash,
        diagnostic_executable_sha256=digest(output),runtime=str(diagnostic_runtime),link_command=command),indent=2)+'\n')
    print(f'Built separate coverage runner: {output}\nOrdinary executable hash preserved: {old_hash}')

if __name__=='__main__':main()
