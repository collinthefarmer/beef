"""Require a fresh attribution review when vendored bytes or dependency pins change."""
import hashlib
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class LicenseTests(unittest.TestCase):
    def test_notice_inventory_and_vendored_provenance(self):
        inventory = json.loads((ROOT / 'licenses/inventory.json').read_text())
        listed = {entry['path'] for entry in inventory['files']}
        self.assertEqual(len(listed), len(inventory['files']))
        self.assertEqual(listed | {'inventory.json'},
                         {path.name for path in (ROOT / 'licenses').iterdir()})
        for group, base in (('files', ROOT / 'licenses'), ('vendored', ROOT)):
            for entry in inventory[group]:
                with self.subTest(path=entry['path']):
                    path = base / entry['path']
                    self.assertTrue(path.resolve().is_relative_to(base.resolve()))
                    self.assertFalse(path.is_symlink())
                    self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), entry['sha256'])
                    self.assertTrue(entry['origin'].startswith('https://'))

    def test_dependency_pins_match_reviewed_inventory(self):
        inventory = json.loads((ROOT / 'licenses/inventory.json').read_text())
        cmake = (ROOT / 'cmake/Windows.cmake').read_text()
        self.assertIn('include("${CMAKE_CURRENT_LIST_DIR}/Compatibility.cmake")', cmake)
        self.assertCountEqual(re.findall(r'GIT_TAG\s+(\S+)', cmake),
                              ['${BEEF_SPDLOG_REVISION}', '${BEEF_RAPIDCSV_REVISION}', '${BEEF_COMMONLIB_REVISION}'])
        for path in (ROOT / 'cmake/compatibility').glob('*.json'):
            profile = json.loads(path.read_text())
            self.assertCountEqual(list(profile['dependencies'].values()), inventory['cmake_pins'])


if __name__ == '__main__':
    unittest.main()
