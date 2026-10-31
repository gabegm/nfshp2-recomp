"""Synthetic ELF/archive fixtures; no game or Ghidra installation required."""
import importlib.util
import os
from pathlib import Path
import stat
import struct
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, ROOT/'scripts'/file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


analysis = load('renderer_analysis', 'analyze-renderer.py')
setup = load('ghidra_setup', 'setup-ghidra.py')


def synthetic_elf():
    data = bytearray(212)
    data[:7] = b'\x7fELF\x01\x01\x01'
    struct.pack_into('<H', data, 18, 8)
    struct.pack_into('<I', data, 32, 52)
    struct.pack_into('<3H', data, 46, 40, 4, 1)
    names = b'\0.shstrtab\0.reginfo\0.vutext\0'
    name_offset = len(data)
    data += names
    reg_offset = len(data)
    data += b'\0'*20 + struct.pack('<I', 0x12345678)
    vu_offset = len(data)
    data += b'\0'*16
    rows = [(0, 0, 0, 0, 0, 0),
            (names.index(b'.shstrtab'), 3, 0, 0, name_offset, len(names)),
            (names.index(b'.reginfo'), 0x70000006, 2, 0x2000, reg_offset, 24),
            (names.index(b'.vutext'), 1, 6, 0x3000, vu_offset, 16)]
    for i, row in enumerate(rows):
        struct.pack_into('<10I', data, 52+i*40, *row, 0, 0, 0, 0)
    return bytes(data)


class GhidraToolTests(unittest.TestCase):
    def test_elf_global_pointer_and_vu_section(self):
        self.assertEqual(analysis.elf_metadata(synthetic_elf()), (0x12345678, (0x3000, 0x300F)))

    def test_malformed_elf_tables_and_sections_are_rejected(self):
        fixtures = [b'', synthetic_elf()[:100]]
        for offset, value, code in ((18, 62, '<H'), (52+3*40+20, 0xFFFFFFF0, '<I'),
                                    (52+3*40, 0xFFFFFF, '<I'), (52+2*40+4, 1, '<I')):
            data = bytearray(synthetic_elf())
            struct.pack_into(code, data, offset, value)
            fixtures.append(data)
        for data in fixtures:
            with self.subTest(size=len(data)), self.assertRaises(ValueError):
                analysis.elf_metadata(data)

    def test_verified_archive_extraction_restores_executable_permissions(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            archive = root/'tools.zip'
            info = zipfile.ZipInfo('tool/support/run.sh')
            info.external_attr = (stat.S_IFREG | 0o755) << 16
            with zipfile.ZipFile(archive, 'w') as target:
                target.writestr(info, '#!/bin/sh\n')
            setup.unpack(archive, root/'out')
            result = root/'out/tool/support/run.sh'
            self.assertEqual(result.read_text(), '#!/bin/sh\n')
            if os.name != 'nt':self.assertTrue(result.stat().st_mode & stat.S_IXUSR)

    def test_archive_traversal_and_symlink_entries_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for name, mode in (('../escape', stat.S_IFREG), ('/absolute', stat.S_IFREG), ('link', stat.S_IFLNK)):
                archive = root/'bad.zip'
                info = zipfile.ZipInfo(name)
                info.external_attr = (mode | 0o777) << 16
                with zipfile.ZipFile(archive, 'w') as target:target.writestr(info, 'data')
                with self.subTest(name=name), self.assertRaises(ValueError):setup.unpack(archive, root/'out')
            self.assertFalse((root/'escape').exists())
