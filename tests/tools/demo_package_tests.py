"""Prevent a demo DLL-only bundle from omitting required runtime assets."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('demo_package', ROOT / 'tools/demo-package.py')
TOOL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TOOL)


class DemoPackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.manifest = {'files': [], 'notices': []}
        names = ['SKSE/Plugins/BetterEnchantmentEffects.dll',
                 'textures/BetterEnchantmentEffects/slots/slot_00.dds',
                 'textures/BetterEnchantmentEffects/slots/slot_01.dds',
                 'SKSE/Plugins/BetterEnchantmentEffects/templates/fill.json',
                 'SKSE/Plugins/BetterEnchantmentEffects/templates/bare.json',
                 'SKSE/Plugins/BetterEnchantmentEffects/presets.json',
                 'SKSE/Plugins/BetterEnchantmentEffects.ini', 'README.md']
        for i, name in enumerate(names + ['LICENSE']):
            source = self.root / f'input-{i}'
            source.write_text(name)
            self.manifest['notices' if name == 'LICENSE' else 'files'].append(
                {'source': str(source), 'destination': name})
        for name in ['BetterEnchantmentEffectsDemo.esp', 'records.json',
                     'recipe.json', 'ward.json', 'demo-readme.md']:
            (self.root / name).write_text(name)

    def collect(self):
        return TOOL.inputs(self.manifest, self.root, [self.root / 'recipe.json', self.root / 'ward.json'],
                           self.root / 'demo-readme.md')

    def test_complete_manifest_and_fixture_are_in_archive(self):
        files = self.collect()
        archive = self.root / 'demo.zip'
        TOOL.package(files, archive)
        with zipfile.ZipFile(archive) as result:
            self.assertIsNone(result.testzip())
            self.assertEqual(set(result.namelist()), set(files) | {'CONTENTS.json'})
            self.assertEqual(set(json.loads(result.read('CONTENTS.json'))), set(files))
            for name, path in files.items():
                self.assertEqual(result.read(name), path.read_bytes())
            self.assertEqual(result.read('FRAMEWORK_README.md'), b'README.md')
            self.assertEqual(result.read('README.md'), b'demo-readme.md')
            for name in ('recipe.json', 'ward.json'):
                self.assertEqual(result.read(f'SKSE/Plugins/BetterEnchantmentEffects/recipes/examples/{name}'), name.encode())

    def test_missing_presenter_or_template_is_rejected(self):
        for entry in list(self.manifest['files']):
            if 'slot_00' not in entry['destination'] and 'fill.json' not in entry['destination']:
                continue
            with self.subTest(destination=entry['destination']):
                self.manifest['files'].remove(entry)
                with self.assertRaisesRegex(ValueError, 'required rendering inputs'):
                    self.collect()
                self.manifest['files'].append(entry)

    def test_missing_file_is_rejected_before_packaging(self):
        Path(self.manifest['files'][1]['source']).unlink()
        with self.assertRaisesRegex(ValueError, 'Missing input'):
            self.collect()

    def test_unsafe_and_duplicate_destinations_are_rejected(self):
        original = self.manifest['files'][0]['destination']
        for name in ['../escape', '/absolute', 'bad\\path', 'README.md']:
            with self.subTest(name=name):
                self.manifest['files'][0]['destination'] = name
                with self.assertRaises(ValueError):
                    self.collect()
        self.manifest['files'][0]['destination'] = original


if __name__ == '__main__':
    unittest.main()
