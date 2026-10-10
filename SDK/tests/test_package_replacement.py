# Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com
"""Filesystem regression tests for repeated Linux SDK packaging; no SDK build required."""
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from BuildAndValidateLinux import replace_package


class PackageReplacementTest(unittest.TestCase):
    def setUp(self):
        logs = Path(__file__).resolve().parents[2] / 'build/logs/sdk-package-replacement'
        logs.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=logs)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.name = 'HTNSDK-2.4.0-linux-x86_64'
        self.dist = self.root / 'dist'
        self.destination = self.dist / self.name
        self.archive = self.dist / f'{self.name}.tar.gz'
        self.checksum = Path(str(self.archive) + '.sha256')

    def staged(self, revision):
        work = self.root / f'build-{revision}'
        stage = work / self.name
        stage.mkdir(parents=True)
        (stage / 'revision.txt').write_text(str(revision))
        archive = work / self.archive.name
        archive.write_bytes(f'new archive {revision}'.encode())
        return stage, archive

    def test_first_package_and_repeated_replacement_refresh_all_outputs(self):
        self.dist.mkdir()
        other = self.dist / 'HTNSDK-2.3.0-linux-x86_64'
        other.mkdir()
        (other / 'keep.txt').write_text('other version')
        other_archive = self.dist / 'HTNSDK-2.3.0-linux-x86_64.tar.gz'
        other_archive.write_bytes(b'keep archive')
        for revision in range(3):
            stage, archive = self.staged(revision)
            replace_package(stage, archive, self.destination, self.archive)
            self.assertEqual((self.destination / 'revision.txt').read_text(), str(revision))
            self.assertFalse((self.destination / 'obsolete.txt').exists())
            self.assertFalse((self.destination / self.name).exists())
            self.assertEqual(self.archive.read_bytes(), f'new archive {revision}'.encode())
            expected = hashlib.sha256(self.archive.read_bytes()).hexdigest()
            self.assertEqual(self.checksum.read_text(), f'{expected}  {self.archive.name}\n')
            self.assertEqual((other / 'keep.txt').read_text(), 'other version')
            self.assertEqual(other_archive.read_bytes(), b'keep archive')
            (self.destination / 'obsolete.txt').write_text('remove on next run')

    def test_output_outside_dist_is_rejected_before_removing_previous_package(self):
        self.destination.mkdir(parents=True)
        previous = self.destination / 'keep.txt'
        previous.write_text('previous package')
        stage, archive = self.staged(1)
        with self.assertRaises(RuntimeError):
            replace_package(stage, archive, self.destination, self.root / self.archive.name)
        self.assertEqual(previous.read_text(), 'previous package')
        self.assertTrue(stage.exists())
        self.assertTrue(archive.exists())

    @unittest.skipUnless(sys.platform == 'linux', 'Symlink regression runs on Linux')
    def test_symlink_output_is_rejected_before_removing_previous_package(self):
        self.destination.mkdir(parents=True)
        previous = self.destination / 'keep.txt'
        previous.write_text('previous package')
        outside = self.root / 'outside.txt'
        outside.write_text('outside')
        self.checksum.symlink_to(outside)
        stage, archive = self.staged(1)
        with self.assertRaises(RuntimeError):
            replace_package(stage, archive, self.destination, self.archive)
        self.assertEqual(previous.read_text(), 'previous package')
        self.assertEqual(outside.read_text(), 'outside')


if __name__ == '__main__':
    unittest.main()
