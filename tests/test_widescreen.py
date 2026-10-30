"""Patch validation tests using synthetic data only; no disc image is required."""
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


class WidescreenFetchTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        source = Path(__file__).resolve().parents[1] / 'scripts/fetch-widescreen.py'
        spec = importlib.util.spec_from_file_location('widescreen_fetch', source)
        self.helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.helper)
        self.helper.__file__ = str(self.root / 'scripts/fetch-widescreen.py')
        self.elf = b'synthetic executable'
        self.payload = b'synthetic community patch'
        self.helper.ELF_SHA256 = hashlib.sha256(self.elf).hexdigest()
        self.helper.PATCH_SHA256 = hashlib.sha256(self.payload).hexdigest()
        self.program('SLUS-20362', self.elf)

    def program(self, serial, data):
        source = self.root / '.local/ps2-native-recomp/scripts/pnach.py'
        source.parent.mkdir(parents=True, exist_ok=True)
        source.write_text(f'def read_program(path):\n    return {serial!r}, "BOOT", {data!r}\n')
        # Ensure importlib cannot reuse a same-size, same-second fixture cache.
        for cached in source.parent.glob('__pycache__/*'):
            cached.unlink()

    def invoke(self, data=None):
        response = io.BytesIO(self.payload if data is None else data)
        with patch.object(sys, 'argv', ['fetch-widescreen.py', 'synthetic.iso']), \
                patch.object(self.helper.urllib.request, 'urlopen', return_value=response) as request, \
                contextlib.redirect_stdout(io.StringIO()) as output, \
                contextlib.redirect_stderr(io.StringIO()):
            self.helper.main()
        return Path(output.getvalue().strip()), request

    def test_validated_download_is_pinned_and_cached(self):
        target, request = self.invoke()
        self.assertEqual(target.read_bytes(), self.payload)
        request.assert_called_once_with(self.helper.URL, timeout=30)
        self.assertIn(self.helper.REVISION, self.helper.URL)
        _, request = self.invoke()
        request.assert_not_called()

    def test_wrong_region_is_rejected_even_with_identical_program_bytes(self):
        self.program('SLES-50731', self.elf)
        with self.assertRaises(SystemExit), patch.object(self.helper.urllib.request, 'urlopen') as request:
            self.invoke()
        request.assert_not_called()

    def test_different_executable_is_rejected(self):
        self.program('SLUS-20362', b'another revision')
        with self.assertRaises(SystemExit):
            self.invoke()
        self.assertFalse((self.root / '.local/widescreen').exists())

    def test_bad_download_is_not_saved(self):
        with self.assertRaises(SystemExit):
            self.invoke(b'unexpected download')
        self.assertFalse((self.root / '.local/widescreen').exists())

    def test_changed_cache_is_rejected(self):
        target, _ = self.invoke()
        target.write_bytes(b'modified cache')
        with self.assertRaises(SystemExit):
            self.invoke()
        self.assertEqual(target.read_bytes(), b'modified cache')


class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        scripts = self.root / 'scripts'
        scripts.mkdir()
        shutil.copy2(Path(__file__).resolve().parents[1] / 'scripts/play.sh', scripts / 'play.sh')
        (scripts / 'nointro.py').write_text(
            'from pathlib import Path\nimport sys\n'
            'root=Path(__file__).resolve().parents[1]\n'
            '(root/"nointro-source.txt").write_text(sys.argv[1])\n'
            'print(root/".local/intro-patch.txt")\n')
        upstream = self.root / '.local/ps2-native-recomp'
        for game in ('nfshp2', 'nfshp2-wide'):
            disc = upstream / 'games' / game / 'disc'
            disc.mkdir(parents=True)
            (disc / 'SYSTEM.CNF').write_text('BOOT2 = cdrom0:\\BOOT.ELF;1\n')
            runner = upstream / 'build' / ('native-' + game) / 'ps2run'
            runner.parent.mkdir(parents=True)
            runner.write_text('#!/usr/bin/env python3\nimport json,os,sys\n'
                              'print(json.dumps({"args":sys.argv,"look":os.getenv("PS2_LOOK"),'
                              '"keys":os.getenv("PS2_KEYS"),"clut_reuse":os.getenv("PS2_CLUT_REUSE")}))\n')
            runner.chmod(0o755)
        (upstream / 'games/nfshp2-wide/patches.txt').write_text('')
        self.environment = {k: v for k, v in os.environ.items() if not k.startswith('PS2_')}

    def launch(self, *args):
        return subprocess.run(['bash', str(self.root / 'scripts/play.sh'), *args],
                              env=self.environment, text=True, capture_output=True)

    def test_standard_uses_unpatched_program(self):
        result = self.launch('--performance', '--frames', '1')
        self.assertEqual(result.returncode, 0, result.stderr)
        run = json.loads(result.stdout)
        self.assertEqual(run['args'][1], 'games/nfshp2/disc/BOOT.ELF')
        self.assertNotIn('--widescreen', run['args'])
        self.assertEqual(run['look'], 'crisp')
        self.assertEqual(run['clut_reuse'], '1')

    def test_palette_reuse_respects_explicit_disable(self):
        self.environment['PS2_CLUT_REUSE'] = '0'
        result = self.launch('--performance', '--frames', '1')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)['clut_reuse'], '0')

    def test_normal_launcher_leaves_palette_reuse_unset(self):
        result = self.launch('--frames', '1')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIsNone(json.loads(result.stdout)['clut_reuse'])

    def test_widescreen_uses_matching_program_and_runtime_patches_in_either_flag_order(self):
        for flags in [('--performance', '--widescreen'), ('--widescreen', '--performance'),
                      ('--frames', '1', '--widescreen', '--performance')]:
            with self.subTest(flags=flags):
                result = self.launch(*flags, '--frames', '1')
                self.assertEqual(result.returncode, 0, result.stderr)
                args = json.loads(result.stdout)['args']
                self.assertEqual(args[1], 'games/nfshp2-wide/patched/BOOT.ELF')
                self.assertEqual(args[args.index('--patches') + 1], 'games/nfshp2-wide/patches.txt')
                self.assertEqual(args[args.index('--memcard') + 1], 'games/nfshp2/player.ps2')
                self.assertEqual(args[args.index('--quick-dir') + 1], 'games/nfshp2-wide')
                self.assertEqual(args.count('--widescreen'), 1)

    def test_missing_widescreen_build_fails_without_stretching_the_standard_build(self):
        (self.root / '.local/ps2-native-recomp/build/native-nfshp2-wide/ps2run').unlink()
        result = self.launch('--widescreen')
        self.assertEqual(result.returncode, 1)
        self.assertIn('Build widescreen first', result.stderr)

    def test_nointro_combines_with_each_profile_without_controller_presses(self):
        for flags, game in [(('--nointro', '--performance'), 'nfshp2'),
                            (('--performance', '--widescreen', '--nointro'), 'nfshp2-wide')]:
            with self.subTest(flags=flags):
                result = self.launch(*flags)
                self.assertEqual(result.returncode, 0, result.stderr)
                args = json.loads(result.stdout)['args']
                self.assertNotIn('--nointro', args)
                self.assertNotIn('--press', args)
                self.assertIn(str(self.root / '.local/intro-patch.txt'), args)
                self.assertEqual((self.root / 'nointro-source.txt').read_text(),
                                 f'games/{game}/disc/BOOT.ELF')
                if game.endswith('-wide'):
                    self.assertIn('games/nfshp2-wide/patches.txt', args)

    def test_nointro_leaves_state_and_replay_resumes_alone(self):
        for option in ('--load-state', '--replay'):
            with self.subTest(option=option):
                result = self.launch('--nointro', '--', option, 'synthetic-save')
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertNotIn('--patches', json.loads(result.stdout)['args'])
                self.assertFalse((self.root / 'nointro-source.txt').exists())


if __name__ == '__main__':
    unittest.main()
