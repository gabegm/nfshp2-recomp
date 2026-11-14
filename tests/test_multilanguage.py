"""Synthetic executable checks for the optional language-menu profile."""
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


class MultilanguageTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        source = Path(__file__).resolve().parents[1] / 'scripts/multilanguage.py'
        spec = importlib.util.spec_from_file_location('multilanguage', source)
        self.helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.helper)
        self.helper.__file__ = str(self.root / 'scripts/multilanguage.py')
        self.data = bytearray(256)
        self.data[:4] = b'\x7fELF'
        struct.pack_into('<I', self.data, 28, 52)
        struct.pack_into('<HH', self.data, 42, 32, 1)
        struct.pack_into('<8I', self.data, 52, 1, 128, 0x0016F900, 0, 128, 128, 5, 16)
        # Put the second instruction in a separate load segment.
        struct.pack_into('<HH', self.data, 42, 32, 2)
        struct.pack_into('<8I', self.data, 84, 1, 192, 0x001EEC00, 0, 64, 64, 5, 16)
        struct.pack_into('<I', self.data, 136, 0x0C000000)
        struct.pack_into('<I', self.data, 204, 0x0C000000)
        self.source = self.root / '.local/ps2-native-recomp/scripts/pnach.py'
        self.source.parent.mkdir(parents=True)

    def invoke(self, serial='SLUS-20362', data=None, trust_changed=False):
        data = bytes(self.data) if data is None else data
        self.source.write_text(f'def read_program(path):\n    return {serial!r}, "BOOT", {data!r}\n')
        for cached in self.source.parent.glob('__pycache__/*'):
            cached.unlink()
        self.helper.ELF_SHA256 = hashlib.sha256(data if trust_changed else bytes(self.data)).hexdigest()
        with patch.object(sys, 'argv', ['multilanguage.py', 'synthetic.iso']), \
                contextlib.redirect_stdout(io.StringIO()) as output:
            self.helper.main()
        return Path(output.getvalue().strip())

    def test_generates_exact_pnach(self):
        target = self.invoke()
        text = target.read_text()
        self.assertIn('[Unlock multilanguage for USA]', text)
        self.assertIn('patch=0,EE,2016F908,extended,0000102D', text)
        self.assertIn('patch=0,EE,201EEC0C,extended,0000102D', text)
        self.assertEqual(text.count('patch='), 2)

    def test_rejects_wrong_region_and_changed_instruction(self):
        with self.assertRaises(SystemExit):
            self.invoke(serial='SLES-50731')
        changed = bytearray(self.data)
        struct.pack_into('<I', changed, 136, 0)
        with self.assertRaises(SystemExit):
            self.invoke(data=bytes(changed), trust_changed=True)
        self.assertFalse((self.root / '.local/multilanguage').exists())


if __name__ == '__main__':
    unittest.main()
