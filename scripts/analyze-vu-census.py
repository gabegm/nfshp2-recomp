#!/usr/bin/env python3
"""Summarize private VU captures and decode packed GIF geometry for inspection."""
import argparse
from collections import Counter
import json
from pathlib import Path
import struct


class PacketError(ValueError):
    pass


def decode_packet(data, state=None):
    """Decode bounded GIF packets; missing inherited GS state stays unknown.

    Vertices retain GS fixed-point coordinates and raw STQ bits. This is a
    geometry inspection tool, not a GS emulator or a world-space mesh decoder.
    """
    if len(data) % 16 or len(data) > 16384:
        raise PacketError('A VU packet must contain at most 1024 complete qwords.')
    state = {} if state is None else state
    tags, vertices, triangles = [], [], []
    queue = []
    offset = 0
    unknown_geometry = False
    assembly_known = False
    writes = Counter()

    def register(reg, value):
        nonlocal assembly_known
        writes[f'{reg:02X}'] += 1
        state[reg] = value
        if reg == 0:
            assembly_known = True
            queue.clear()

    def vertex(words, reg):
        nonlocal unknown_geometry
        x, y = words[0] & 65535, words[1] & 65535
        fogged = reg in (4, 12)
        z = (words[2] >> 4) & 0xFFFFFF if fogged else words[2]
        adc = bool(words[3] & 0x8000) or reg in (12, 13)
        v = dict(x=x, y=y, z=z, adc=adc, prim=state.get(0),
                 rgba=state.get('rgba'), stq_bits=state.get('stq'), uv=state.get('uv'))
        vertices.append(v)
        queue.append(len(vertices) - 1)
        if not assembly_known:
            unknown_geometry = True
            queue.clear()
            return
        kind = state[0] & 7
        needed = 1 if kind in (0, 7) else 2 if kind in (1, 2, 6) else 3
        if len(queue) < needed:
            return
        if not adc and kind in (3, 4, 5):
            triangles.append(queue[-3:].copy())
        if kind == 2:
            queue[:] = queue[-1:]
        elif kind == 4:
            queue[:] = queue[-2:]
        elif kind == 5:
            queue[:] = [queue[0], queue[-1]]
        else:
            queue.clear()

    while offset < len(data):
        lo, regs = struct.unpack_from('<QQ', data, offset)
        offset += 16
        loops, mode, nreg = lo & 32767, (lo >> 58) & 3, (lo >> 60) & 15 or 16
        count = loops * nreg
        qwords = count if mode == 0 else (count + 1) // 2 if mode == 1 else loops
        if offset + qwords * 16 > len(data):
            raise PacketError('Truncated GIF tag body.')
        pre = bool((lo >> 46) & 1) and mode == 0
        if pre:
            register(0, (lo >> 47) & 2047)
        tags.append(dict(mode=mode, loops=loops, nreg=nreg, pre=pre,
                         prim=state.get(0), eop=bool(lo & 32768)))
        if mode == 0:
            for i in range(count):
                reg = (regs >> ((i % nreg) * 4)) & 15
                words = struct.unpack_from('<4I', data, offset + i * 16)
                if reg == 0:
                    register(0, words[0] & 2047)
                elif reg == 1:
                    state['rgba'] = [w & 255 for w in words]
                elif reg == 2:
                    state['stq'] = list(words[:3])
                elif reg == 3:
                    state['uv'] = [words[0] & 16383, words[1] & 16383]
                elif reg in (4, 5, 12, 13):
                    vertex(words, reg)
                elif reg == 14:
                    target = words[2] & 255
                    if target in (4, 5, 12, 13):
                        unknown_geometry = True
                    register(target, words[0] | (words[1] << 32))
        # REGLIST and IMAGE are counted but deliberately not reconstructed.
        # Their presence makes the mesh incomplete and is reported explicitly.
        offset += qwords * 16
    return dict(tags=tags, writes=dict(writes), vertices=vertices, triangles=triangles,
                mesh_complete=not unknown_geometry and all(t['mode'] == 0 for t in tags) and
                              all(v['prim'] is not None for v in vertices))


def capture_file(root, name):
    if not name or Path(name).name != name or name in ('.', '..'):
        raise PacketError('Capture filenames must be local basenames.')
    path = root / name
    if path.is_symlink() or path.resolve().parent != root.resolve():
        raise PacketError('Capture files must stay inside the capture directory.')
    return path


def summarize(root, meshes=False):
    programs, calls, samples = {}, {}, []
    seen_frames = set()
    with (root / 'calls.jsonl').open() as source:
        for line in source:
            row = json.loads(line)
            if row['type'] == 'header':
                if row.get('version') != 1:
                    raise PacketError('Unsupported census version.')
            elif row['type'] == 'summary':
                ident = (row['key'], row['entry'])
                totals = programs.setdefault(ident, Counter())
                totals.update({k: row[k] for k in ('calls', 'kicks', 'qwords')})
                seen_frames.add(row['frame'])
            elif row['type'] == 'call':
                calls[row['id']] = dict(row, state={})
            elif row['type'] == 'kick' and row['packet']:
                call = calls[row['id']]
                path = capture_file(root, row['packet'])
                data = path.read_bytes()
                if len(data) != row['qwords'] * 16:
                    raise PacketError('Packet length does not match its manifest.')
                packet = decode_packet(data, call['state'])
                result = dict(key=call['key'], entry=call['entry'], packet=path.name,
                              tags=packet['tags'], vertices=len(packet['vertices']),
                              triangles=len(packet['triangles']), writes=packet['writes'],
                              mesh_complete=packet['mesh_complete'])
                if meshes and packet['triangles']:
                    destination = path.with_suffix('.obj')
                    with destination.open('w') as obj:
                        obj.write('# Projected GS coordinates, not world-space geometry.\n')
                        for v in packet['vertices']:
                            obj.write(f"v {v['x']/16} {v['y']/16} {v['z']}\n")
                        for triangle in packet['triangles']:
                            obj.write('f ' + ' '.join(str(i + 1) for i in triangle) + '\n')
                    result['mesh'] = destination.name
                samples.append(result)
    return dict(version=1, frames=len(seen_frames),
                programs=[dict(key=k, entry=entry, **totals) for (k, entry), totals in
                          sorted(programs.items(), key=lambda item: -item[1]['qwords'])],
                samples=samples,
                limitations=['Frame labels are sampled from the emulation clock, not GS fences.',
                             'Samples are bounded; they do not cover all invocations or inherited GS state.',
                             'OBJ meshes contain projected coordinates, not original model vertices.',
                             'REGLIST/IMAGE geometry is not reconstructed.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--meshes', action='store_true', help='Write private projected OBJ meshes beside captured packets.')
    args = parser.parse_args()
    result = summarize(args.capture, args.meshes)
    output = args.capture / 'summary.json'
    output.write_text(json.dumps(result, indent=2) + '\n')
    print(f"{len(result['programs'])} program entries across {result['frames']} frame labels; {len(result['samples'])} packets. Private report: {output}")


if __name__ == '__main__':
    main()
