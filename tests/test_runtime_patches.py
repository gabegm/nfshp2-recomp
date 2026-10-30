import difflib
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('runtime_patches', Path(__file__).resolve().parents[1] / 'scripts/apply-runtime-patches.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)


class RuntimePatchTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.upstream = self.root / 'upstream'
        self.upstream.mkdir()
        subprocess.run(['git', '-C', str(self.upstream), 'init', '-q'], check=True)
        self.source = self.upstream / 'runtime.cpp'
        self.base = 'alpha\nbeta\ngamma\ndelta\nepsilon\nzeta\neta\ntheta\n'
        self.source.write_text(self.base)
        self.first = self.base.replace('alpha', 'ALPHA')
        self.second = self.first.replace('beta', 'BETA')
        self.p1 = self.patch('first', self.base, self.first)
        self.p2 = self.patch('second', self.first, self.second)

    def patch(self, label, before, after, name='runtime.cpp'):
        path = self.root / (label + '.patch')
        path.write_text(''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                                                    fromfile='a/' + name, tofile='b/' + name, n=2)))
        return path

    def test_clean_install_and_repeat_preserve_mtime(self):
        self.assertEqual(patcher.apply_series(self.upstream, [self.p1, self.p2]), 1)
        self.assertEqual(self.source.read_text(), self.second)
        before = self.source.stat().st_mtime_ns
        self.assertEqual(patcher.apply_series(self.upstream, [self.p1, self.p2]), 0)
        self.assertEqual(self.source.stat().st_mtime_ns, before)

    def test_upgrade_with_overlapping_context(self):
        patcher.apply_series(self.upstream, [self.p1])
        patcher.apply_series(self.upstream, [self.p1, self.p2])
        self.assertEqual(self.source.read_text(), self.second)

    def test_unrelated_local_edit_is_preserved(self):
        patcher.apply_series(self.upstream, [self.p1])
        self.source.write_text(self.source.read_text().replace('theta', 'local edit'))
        patcher.apply_series(self.upstream, [self.p1, self.p2])
        self.assertEqual(self.source.read_text(), self.second.replace('theta', 'local edit'))

    def test_conflict_leaves_checkout_untouched(self):
        patcher.apply_series(self.upstream, [self.p1])
        self.source.write_text(self.source.read_text().replace('beta', 'conflicting edit'))
        before = self.source.read_bytes()
        with self.assertRaises(RuntimeError):
            patcher.apply_series(self.upstream, [self.p1, self.p2])
        self.assertEqual(self.source.read_bytes(), before)

    def test_later_failure_does_not_apply_earlier_patch(self):
        bad = self.patch('bad', 'missing context\n', 'replacement\n')
        with self.assertRaises(RuntimeError):
            patcher.apply_series(self.upstream, [self.p1, bad])
        self.assertEqual(self.source.read_text(), self.base)

    def test_symlink_source_is_rejected(self):
        self.source.unlink()
        outside = self.root / 'outside'
        outside.write_text(self.base)
        self.source.symlink_to(outside)
        with self.assertRaises(ValueError):
            patcher.apply_series(self.upstream, [self.p1])
        self.assertEqual(outside.read_text(), self.base)


if __name__ == '__main__':
    unittest.main()
