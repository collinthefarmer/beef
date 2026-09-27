"""Require the first-party permission, its per-file references, and its package and source entries."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]


class LicenseTests(unittest.TestCase):
    def test_first_party_permission_references_and_package_notice(self):
        notice = (ROOT / 'COPYING.md').read_text()
        self.assertIn('GPL-3.0-only', notice)
        self.assertIn('additional permission under section 7', notice)
        for directory in ('src', 'tests'):
            for path in (ROOT / directory).rglob('*'):
                if path.suffix not in ('.h', '.cpp') or any(
                        part in ('extern', 'cs') for part in path.relative_to(ROOT).parts):
                    continue
                self.assertIn('COPYING.md', path.read_text().splitlines()[0], str(path))
        derived = (ROOT / 'src/render/PBRMaterial.h').read_text()
        self.assertIn('Community Shaders-derived portions:', derived)
        self.assertIn('COPYING.md', (ROOT / 'cmake/Plugin.cpp.in').read_text().splitlines()[0])
        stage = (ROOT / 'cmake/Stage.cmake').read_text()
        self.assertIn('package_file("${CMAKE_SOURCE_DIR}/COPYING.md" "COPYING.md")', stage)
        if not (ROOT / '.git').exists():
            return
        tracked = subprocess.run(['git', 'ls-files', '--error-unmatch', 'COPYING.md', 'LICENSE'],
                                 cwd=ROOT, capture_output=True)
        self.assertEqual(tracked.returncode, 0, 'the source archive holds tracked files only')
        self.assertNotIn('COPYING.md export-ignore', (ROOT / '.gitattributes').read_text())


if __name__ == '__main__':
    unittest.main()
