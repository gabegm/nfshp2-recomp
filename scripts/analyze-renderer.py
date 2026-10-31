#!/usr/bin/env python3
"""Create private Ghidra notes for the original US executable's VU setup code."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ELF_SHA256 = '7335b8ed479481082c648958f4b5be4da65b74d042d0ab2f5237df801eafacc2'


def elf_metadata(data):
    if len(data) < 52 or data[:7] != b'\x7fELF\x01\x01\x01' or struct.unpack_from('<H', data, 18)[0] != 8:
        raise ValueError('Expected a little-endian ELF32 MIPS executable.')
    offset = struct.unpack_from('<I', data, 32)[0]
    size, count, strings_index = struct.unpack_from('<3H', data, 46)
    if size != 40 or strings_index >= count or offset + size*count > len(data):
        raise ValueError('Invalid section header table.')
    sections = [struct.unpack_from('<10I', data, offset + i*size) for i in range(count)]
    strings = sections[strings_index]
    if strings[4] + strings[5] > len(data):
        raise ValueError('Invalid section string table.')
    names = data[strings[4]:strings[4]+strings[5]]
    gp = None
    vu = None
    for section in sections:
        if section[0] >= len(names):
            raise ValueError('Invalid section name.')
        name = names[section[0]:].split(b'\0', 1)[0]
        if section[1] != 8 and section[4]+section[5] > len(data):
            raise ValueError('Section exceeds file size.')
        if section[1] == 0x70000006 and section[5] >= 24:
            gp = struct.unpack_from('<I', data, section[4]+20)[0]
        if name == b'.vutext' and section[5]:
            vu = (section[3], section[3]+section[5]-1)
    if gp is None or vu is None:
        raise ValueError('The executable needs .reginfo and .vutext metadata.')
    return gp, vu


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, default=ROOT / '.local/ps2-native-recomp/games/nfshp2/disc/SLUS_203.62')
    parser.add_argument('--reference-range', nargs=2, action='append', default=[], metavar=('START_HEX', 'END_HEX'))
    args = parser.parse_args()
    data = args.elf.read_bytes()
    if hashlib.sha256(data).hexdigest() != ELF_SHA256:
        raise ValueError('Expected the verified original US executable used by this setup.')
    gp, vu = elf_metadata(data)
    spec = importlib.util.spec_from_file_location('setup_ghidra', ROOT/'scripts/setup-ghidra.py')
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    ghidra = helper.install()
    notes = ROOT / '.local/renderer-analysis'
    notes.mkdir(exist_ok=True)
    # Ghidra rejects project paths containing a component starting with '.'.
    project = Path(tempfile.mkdtemp(prefix='nfshp2-ghidra-', dir='/tmp'))
    (notes/'project-path.txt').write_text(str(project)+'\n')
    environment = os.environ.copy()
    home = ROOT / '.local/ghidra-home'
    home.mkdir(exist_ok=True)
    environment['JAVA_TOOL_OPTIONS'] = environment.get('JAVA_TOOL_OPTIONS', '') + f' -Duser.home={home}'
    command = [str(ghidra/'support/analyzeHeadless'), str(project), 'NFSHP2',
               '-import', str(args.elf.resolve()), '-processor', 'r5900:LE:32:default', '-cspec', 'default',
               '-analysisTimeoutPerFile', '300', '-max-cpu', '1', '-scriptPath', str(ROOT/'scripts'),
               '-preScript', 'SetPs2Gp.java', f'{gp:08x}', '-postScript', 'ExportVuReferences.java',
               str(notes/'vu-references'), f'{vu[0]:08x}', f'{vu[1]:08x}', '-log', str(notes/'analysis.log')]
    for first, last in args.reference_range:
        start, end = int(first, 16), int(last, 16)
        if not 0 <= start <= end <= 0xFFFFFFFF:
            raise ValueError('Invalid reference range.')
        command += ['-postScript', 'ExportVuReferences.java', str(notes/f'refs-{start:08x}-{end:08x}'), f'{start:08x}', f'{end:08x}']
    with (notes/'console.log').open('w') as log:
        subprocess.run(command, env=environment, stdout=log, stderr=subprocess.STDOUT, check=True)
    (notes/'metadata.json').write_text(json.dumps(dict(sha256=ELF_SHA256, gp=gp, vu_range=vu, project=str(project)), indent=2)+'\n')
    print(f'Private Ghidra project: {project}/NFSHP2.gpr\nPrivate analysis notes: {notes}')


if __name__ == '__main__':
    main()
