"""No-intro patch tests with a synthetic ELF, without any game files."""
import contextlib
import hashlib
import importlib.util
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch


def synthetic_elf(payload):
    data = bytearray(84)
    data[:6] = b'\x7fELF\x01\x01'
    struct.pack_into('<I', data, 28, 52)
    struct.pack_into('<HH', data, 42, 32, 1)
    struct.pack_into('<8I', data, 52, 1, 84, 0x100000, 0, len(payload), len(payload), 6, 4)
    return bytes(data) + payload


class NoIntroTests(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location(
            'nointro', Path(__file__).resolve().parents[1] / 'scripts/nointro.py')
        self.helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.helper)

    def test_only_startup_movies_are_selected(self):
        names = [b'MOVIES\\INTRO\\EALOGO_EN.AVI', b'MOVIES\\INTRO\\NFSINTRO_FR.AVI',
                 b'MOVIES\\INTRO\\BBGLOGO_EN.AVI', b'MOVIES\\INTRO\\REWARD_EN.AVI',
                 b'MOVIES\\FRONTEND\\BACKGROUND.AVI']
        addresses = self.helper.startup_paths(synthetic_elf(b'\0'.join(names) + b'\0'))
        self.assertEqual(addresses, [0x100000, 0x100000 + len(names[0]) + 1,
                                    0x100000 + len(names[0]) + len(names[1]) + 2])

    def test_incomplete_names_and_substrings_are_ignored(self):
        payload = b'xMOVIES\\INTRO\\EALOGO_EN.AVI\0MOVIES\\INTRO\\NFSINTRO_EN.AVI'
        self.assertEqual(self.helper.startup_paths(synthetic_elf(payload)), [])

    def test_invalid_segment_is_rejected(self):
        data = bytearray(synthetic_elf(b'example'))
        struct.pack_into('<I', data, 56, len(data) + 1)
        with self.assertRaises(ValueError):
            self.helper.startup_paths(bytes(data))

    def test_unknown_executable_is_rejected_without_output(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source = root / 'BOOT.ELF'
            source.write_bytes(synthetic_elf(b'example'))
            self.helper.__file__ = str(root / 'scripts/nointro.py')
            with patch.object(sys, 'argv', ['nointro.py', str(source)]), self.assertRaises(SystemExit):
                self.helper.main()
            self.assertFalse((root / '.local').exists())

    def test_generated_patches_are_boot_only_and_leave_the_elf_untouched(self):
        data = synthetic_elf((b'MOVIES\\INTRO\\EALOGO_EN.AVI\0' * 11) + b'MOVIES\\INTRO\\REWARD_EN.AVI\0')
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source = root / 'BOOT.ELF'
            source.write_bytes(data)
            self.helper.__file__ = str(root / 'scripts/nointro.py')
            self.helper.ELF_SHA256 = hashlib.sha256(data).hexdigest()
            with patch.object(sys, 'argv', ['nointro.py', str(source)]), contextlib.redirect_stdout(io.StringIO()) as output:
                self.helper.main()
            rows = Path(output.getvalue().strip()).read_text().splitlines()
            self.assertEqual(len(rows), 11)
            self.assertTrue(all(row.split()[0] == '0' and row.split()[2:] == ['1', '0'] for row in rows))
            self.assertEqual(source.read_bytes(), data)


if __name__ == '__main__':
    unittest.main()
