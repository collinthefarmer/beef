"""Verify generated build identity and presenter dependencies with the real rules."""
import json
import os
from pathlib import Path
import shutil
import subprocess
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
            for name in ('CMakePresets.json', 'flake.nix', 'flake.lock'):
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


if __name__ == '__main__':
    unittest.main()
