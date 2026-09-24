"""Verify generated build identity and presenter dependencies with the real rules."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class GeneratedTests(unittest.TestCase):
    def test_identity_and_presenter_outputs_are_incremental(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for name in ('src', 'cmake', 'tools'):
                (root / name).mkdir()
            shutil.copy2(ROOT / 'cmake/Generated.cmake', root / 'cmake/Generated.cmake')
            for name in ('build-identity.py', 'presenter-textures.py'):
                shutil.copy2(ROOT / 'tools' / name, root / 'tools' / name)
            for name in ('CMakePresets.json', 'flake.nix', 'flake.lock', 'COPYING.md'):
                (root / name).write_text('')
            source = root / 'src/Example.cpp'
            source.write_text('int example;\n')
            (root / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.24)
project(Fixture NONE)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_target(Fixture ALL)
include(cmake/Generated.cmake)
''')
            def call(*args):
                result = subprocess.run(args, cwd=root, text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                return result.stdout
            call('git', 'init', '-q')
            call('git', 'add', '.')
            call('git', '-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
                 'commit', '-qm', 'fixture')
            build = root / 'out'
            call('cmake', '-S', str(root), '-B', str(build), '-G', 'Ninja')
            def compile():
                return call('cmake', '--build', str(build), '--target', 'Fixture', 'PresenterTextures')
            compile()
            manifest = build / 'generated/build-identity.json'
            header = build / 'generated/BuildIdentity.h'
            stamp = build / 'generated/identity.stamp'
            original = manifest.read_text()
            header_time = header.stat().st_mtime_ns
            stamp_time = stamp.stat().st_mtime_ns
            compile()
            self.assertEqual(stamp.stat().st_mtime_ns, stamp_time)
            os.utime(source, None)
            compile()
            self.assertEqual(header.stat().st_mtime_ns, header_time)
            stamp_time = stamp.stat().st_mtime_ns
            compile()
            self.assertEqual(stamp.stat().st_mtime_ns, stamp_time)
            (root / 'COPYING.md').write_text('updated permission\n')
            compile()
            self.assertNotEqual(manifest.read_text(), original)
            original = manifest.read_text()
            source.write_text('int different;\n')
            compile()
            self.assertNotEqual(manifest.read_text(), original)
            changed = manifest.read_text()
            source.unlink()
            compile()
            self.assertNotEqual(manifest.read_text(), changed)
            old_revision = json.loads(manifest.read_text())['revision']
            call('git', '-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
                 'commit', '--allow-empty', '-qm', 'next revision')
            compile()
            self.assertNotEqual(json.loads(manifest.read_text())['revision'], old_revision)
            presenter = build / 'presenters/slot_00.dds'
            presenter.unlink()
            compile()
            self.assertTrue(presenter.exists())
            self.assertEqual(len(list((build / 'presenters').glob('slot_*.dds'))), 1024)

            call(sys.executable, 'tools/build-identity.py', '--root', str(root), '--write-provenance')
            archive = root / 'archive'
            archive.mkdir()
            for name in ('src', 'cmake', 'tools'):
                shutil.copytree(root / name, archive / name)
            for name in ('CMakeLists.txt', 'CMakePresets.json', 'flake.nix', 'flake.lock', 'COPYING.md',
                         'SOURCE_PROVENANCE.json'):
                shutil.copy2(root / name, archive / name)
            archive_build = root / 'archive-out'
            call('cmake', '-S', str(archive), '-B', str(archive_build), '-G', 'Ninja')
            def archive_compile(success=True):
                result = subprocess.run(['cmake', '--build', str(archive_build)],
                                        text=True, capture_output=True)
                self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
                return result.stdout + result.stderr
            archive_compile()
            archive_manifest = archive_build / 'generated/build-identity.json'
            self.assertEqual(archive_manifest.read_text(), manifest.read_text())
            self.assertEqual((archive_build / 'generated/BuildIdentity.h').read_bytes(), header.read_bytes())
            archive_stamp = archive_build / 'generated/identity.stamp'
            stamp_time = archive_stamp.stat().st_mtime_ns
            archive_compile()
            self.assertEqual(archive_stamp.stat().st_mtime_ns, stamp_time)

            (archive / 'src/IntentionalEdit.cpp').write_text('int edited;\n')
            self.assertIn('archive source differs', archive_compile(False))
            call('cmake', '-S', str(archive), '-B', str(archive_build),
                 '-DBEEF_ALLOW_MODIFIED_SOURCE=ON')
            archive_compile()
            edited = json.loads(archive_manifest.read_text())
            original = json.loads(manifest.read_text())
            self.assertEqual(edited['revision'], original['revision'])
            self.assertEqual(edited['archive_source_sha256'], original['source_sha256'])
            self.assertNotEqual(edited['build'], original['build'])
            self.assertNotEqual(edited['source_sha256'], original['source_sha256'])

            provenance = archive / 'SOURCE_PROVENANCE.json'
            saved = provenance.read_text()
            changed = json.loads(saved)
            changed['compatibility_sha256'] = '0' * 64
            provenance.write_text(json.dumps(changed))
            self.assertIn('archive compatibility profile differs', archive_compile(False))
            for invalid in ('{', '[]', '{}', saved.replace('"schema": 1', '"schema": true')):
                provenance.write_text(invalid)
                self.assertIn('invalid SOURCE_PROVENANCE.json', archive_compile(False))
            provenance.unlink()
            result = subprocess.run([sys.executable, str(archive / 'tools/build-identity.py'),
                                     '--root', str(archive), '--output', str(archive_build),
                                     '--config', 'Debug'], text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('requires SOURCE_PROVENANCE.json', result.stderr)


if __name__ == '__main__':
    unittest.main()
