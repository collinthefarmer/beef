"""Require the first-party permission, its per-file references, and its package and source entries."""
import importlib.util
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('gate', ROOT / 'tools/gate.py')
gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gate)


class LicenseTests(unittest.TestCase):
    def test_first_party_permission_references_and_package_notice(self):
        notice = (ROOT / 'COPYING.md').read_text()
        self.assertIn('GPL-3.0-only', notice)
        self.assertIn('additional permission under section 7', notice)
        for directory in ('src', 'tests'):
            for path in (ROOT / directory).rglob('*'):
                if path.is_file() and gate.first_party(path.relative_to(ROOT).as_posix()):
                    self.assertIn('COPYING.md', path.read_text().splitlines()[0], str(path))
        derived = (ROOT / 'src/render/PBRMaterial.h').read_text()
        self.assertIn('Community Shaders-derived portions:', derived)
        self.assertIn('COPYING.md', (ROOT / 'cmake/Plugin.cpp.in').read_text().splitlines()[0])
        stage = (ROOT / 'cmake/Stage.cmake').read_text()
        self.assertIn('package_file("${notice_dir}/${shipped_copying}" "${shipped_copying}")', stage)
        self.assertIn('set(notice_prefix "${PROJECT_NAME}-")', stage)
        if not (ROOT / '.git').exists():
            return
        tracked = subprocess.run(['git', 'ls-files', '--error-unmatch', 'COPYING.md', 'LICENSE'],
                                 cwd=ROOT, capture_output=True)
        self.assertEqual(tracked.returncode, 0, 'the source archive holds tracked files only')
        self.assertNotIn('COPYING.md export-ignore', (ROOT / '.gitattributes').read_text())


if __name__ == '__main__':
    unittest.main()
