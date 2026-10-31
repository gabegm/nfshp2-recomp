#!/usr/bin/env python3
"""Benchmark private checkpoints in real windowed play; never capture game assets."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT/'.local/ps2-native-recomp'
COLUMNS = 'frame wall_ms cpu_ms wait_window_ms wait_sound_ms shapes interpreted audio_ms'.split()
PRESET = dict(PS2_NO_VU_THREAD='1', PS2_DECODE='0', PS2_SCALE='1', PS2_TILE='4',
              PS2_LOOK='crisp', PS2_RASTER_SPECIALIZE='1', PS2_RACE_SPECIALIZE='1', PS2_CLUT_REUSE='1')


def percentile(values, p):
    values = sorted(values)
    index = (len(values)-1)*p/100
    lo, hi = math.floor(index), math.ceil(index)
    return values[lo]+(values[hi]-values[lo])*(index-lo)


def summarize(content, start, frames, warmup, log):
    lines = content.splitlines()
    if not lines or lines[0].split() != COLUMNS:
        raise ValueError('Unrecognized frame-log header.')
    rows = []
    previous = start
    for line in lines[1:]:
        row = list(map(float, line.split()))
        if len(row) != len(COLUMNS) or not all(math.isfinite(v) and v >= 0 for v in row):
            raise ValueError('Invalid frame-log row.')
        if row[0] != int(row[0]) or row[0] <= previous or row[0] > start+frames:
            raise ValueError('Frame numbers are duplicated, unordered or out of range.')
        if rows and row[0] != previous+1:
            raise ValueError('Missing frame intervals.')
        previous = row[0]
        rows.append(row)
    selected = [r for r in rows if r[0] >= start+warmup]
    if len(selected) < frames-warmup or previous < start+frames-1 or 'frame limit reached' not in log:
        raise ValueError('Incomplete benchmark; no performance result accepted.')
    wall = sum(r[1] for r in selected)
    if wall <= 0:
        raise ValueError('Invalid wall duration.')
    gpu = re.search(r'^\[gpu\] (.+) on Vulkan$', log, re.M)
    if not gpu or any(word in gpu[1].lower() for word in ('llvmpipe', 'lavapipe', 'software')):
        raise ValueError('Expected a hardware Vulkan adapter.')
    audio = re.search(r'^\[audio\] sound device .+ \(([^)]+)\):', log, re.M)
    if not audio or audio[1] == 'dummy':
        raise ValueError('A real audio device is required for playback benchmarks.')
    if '[gpu] error' in log:
        raise ValueError('GPU errors invalidate the benchmark.')
    if re.search(r'^\[(native-triangle|native-batch|native-target|native-capture)\]', log, re.M):
        raise ValueError('Experimental rendering/captures invalidate a baseline run.')
    rate = 1000*len(selected)/wall
    # Two adjacent guest vblanks avoid treating the game's alternating render/
    # non-render ticks as independent complete game-image frames. No unique-
    # image rate is inferred from these pairs.
    pairs = [selected[i][1]+selected[i+1][1] for i in range(0, len(selected)-1, 2)]
    underruns = [int(frame) for frame in re.findall(r'\[audio\] the sound ran dry at frame (\d+)', log)]
    audio_summary = re.search(r'\[audio\] the device got ([\d.]+) s of sound in ([\d.]+) s; the queue ran dry (\d+) times', log)
    return dict(intervals=len(selected), guest_vblanks_per_second=rate,
                guest_speed_percent=rate/60*100, wall_ms_per_guest_vblank=wall/len(selected),
                guest_seconds=len(selected)/60, measured_wall_seconds=wall/1000,
                vblank_ms_p50=percentile([r[1] for r in selected],50),
                vblank_ms_p95=percentile([r[1] for r in selected],95),
                vblank_ms_p99=percentile([r[1] for r in selected],99),
                adjacent_pair_ms_p95=percentile(pairs,95), adjacent_pair_ms_p99=percentile(pairs,99),
                cpu_ms_per_vblank=statistics.mean(r[2] for r in selected),
                wait_window_ms=statistics.mean(r[3] for r in selected),
                wait_sound_ms=statistics.mean(r[4] for r in selected),
                interpreted_vu_calls=sum(int(r[6]) for r in selected),
                zero_audio_queue_intervals=sum(r[7] == 0 for r in selected),
                logged_audio_starvation_events_after_warmup=sum(f >= start+warmup for f in underruns),
                whole_run_audio_starvations=int(audio_summary[3]) if audio_summary else None,
                gpu=gpu[1])


def machine_state():
    def read(path):
        try:return Path(path).read_text().strip()
        except OSError:return None
    power = {}
    for device in Path('/sys/class/power_supply').glob('*'):
        power[device.name] = {field:read(device/field) for field in ('type','online','status','capacity') if (device/field).exists()}
    thermal = {p.parent.name:dict(type=read(p.parent/'type'),millidegrees_c=read(p))
               for p in Path('/sys/class/thermal').glob('thermal_zone*/temp')}
    return dict(power=power, thermal=thermal,
                cpu_energy_preference=read('/sys/devices/system/cpu/cpufreq/policy0/energy_performance_preference'),
                cpu_governor=read('/sys/devices/system/cpu/cpufreq/policy0/scaling_governor'),
                load_average=read('/proc/loadavg'))


def digest(path):
    result=hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda:stream.read(1<<20),b''):result.update(block)
    return result.hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--checkpoint',type=Path,required=True)
    parser.add_argument('--start',type=int,required=True,help='Guest frame stored in the checkpoint.')
    parser.add_argument('--scene',choices=('race','menu'),default='race')
    parser.add_argument('--frames',type=int,default=600)
    parser.add_argument('--warmup',type=int,default=40)
    parser.add_argument('--rounds',type=int,default=2)
    parser.add_argument('--timeout',type=float,default=60)
    parser.add_argument('--widescreen',action='store_true')
    parser.add_argument('--baseline',type=Path,help='Optional archived binary for alternating A/B runs.')
    parser.add_argument('--candidate',type=Path)
    parser.add_argument('--output',type=Path,required=True,help='New private directory under .local.')
    args=parser.parse_args()
    if args.start<0 or args.frames<4 or not 1<=args.warmup<args.frames-1 or args.rounds<1 or args.timeout<=0:
        parser.error('Invalid frame range, repetitions or timeout.')
    output=args.output.resolve()
    if not output.is_relative_to((ROOT/'.local').resolve()):parser.error('Benchmark output must stay under ignored .local storage.')
    game='nfshp2-wide' if args.widescreen else 'nfshp2'
    checkpoint=args.checkpoint.resolve()
    candidate=(args.candidate or RUNTIME/f'build/native-{game}/ps2run').resolve()
    variants=[('baseline',args.baseline.resolve()),('candidate',candidate)] if args.baseline else [('current',candidate)]
    for path in [checkpoint]+[p for _,p in variants]:
        if not path.is_file():parser.error(f'Missing input: {path}')
    output.mkdir(parents=True,exist_ok=False)
    report=dict(version=1,scene=args.scene,widescreen=args.widescreen,start=args.start,
                frames=args.frames,warmup=args.warmup,guest_vblank_hz=60,
                checkpoint_sha256=digest(checkpoint),preset=PRESET,
                binaries={name:dict(path=str(p),sha256=digest(p)) for name,p in variants},runs=[])
    for repeat in range(args.rounds):
        for label,executable in (variants if repeat%2==0 else list(reversed(variants))):
            name=f'{repeat+1}-{label}';folder=output/name;folder.mkdir()
            env={k:v for k,v in os.environ.items() if not k.startswith('PS2_')}
            env.update(PRESET,PS2_FRAME_LOG=str(folder/'frames.txt'))
            env['LD_LIBRARY_PATH']=str(ROOT/'.local/deps/lib64')+':'+str(ROOT/'.local/deps/lib')+(':'+env['LD_LIBRARY_PATH'] if env.get('LD_LIBRARY_PATH') else '')
            elf=RUNTIME/f'games/{game}/{"patched" if args.widescreen else "disc"}/SLUS_203.62'
            command=[str(executable),str(elf),'--disc',str(RUNTIME/f'games/{game}/disc.iso'),
                     '--load-state',str(checkpoint),'--frames',str(args.start+args.frames),'--rtc','1700000000','--window','--no-record']
            if args.scene=='race':command+=['--press',f'{args.start+1}:cross:{args.frames}']
            if args.widescreen:command+=['--widescreen','--patches',str(RUNTIME/f'games/{game}/patches.txt')]
            entry=dict(name=name,before=machine_state(),command=command)
            with (folder/'run.log').open('w') as log:
                then=time.monotonic()
                try:
                    process=subprocess.run(command,cwd=RUNTIME,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=args.timeout)
                    if process.returncode:raise ValueError(f'Game exited with code {process.returncode}.')
                    entry['metrics']=summarize((folder/'frames.txt').read_text(),args.start,args.frames,args.warmup,(folder/'run.log').read_text())
                    entry['status']='ok'
                except (OSError,ValueError,subprocess.TimeoutExpired) as error:
                    entry.update(status='failed',error=str(error))
                entry.update(process_wall_seconds=time.monotonic()-then,after=machine_state())
            report['runs'].append(entry)
            (output/'results.json').write_text(json.dumps(report,indent=2)+'\n')
            shown={k:v for k,v in entry.get('metrics',{}).items() if k in ('guest_vblanks_per_second','guest_speed_percent','adjacent_pair_ms_p95','adjacent_pair_ms_p99','whole_run_audio_starvations')}
            print(json.dumps(dict(run=name,status=entry['status'],**shown)),flush=True)
            if entry['status']!='ok':raise SystemExit(f'Benchmark failed; see private logs in {folder}')

if __name__=='__main__':main()
