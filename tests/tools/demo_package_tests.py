"""Prevent a demo DLL-only bundle from omitting required runtime assets."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
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
        entry = next(e for e in self.manifest['files'] if e['destination'].endswith('.ini'))
        unsafe = ['../escape', '/absolute', 'bad\\path', '', './a', 'a//b', 'C:/x', 'a/.git/config']
        duplicate = ['README.md', 'CONTENTS.json', 'contents.JSON', 'records.json', 'Records.JSON',
                     'skse/plugins/betterenchantmenteffects.dll']
        for names, message in ((unsafe, 'Unsafe destination'), (duplicate, 'Duplicate destination')):
            for name in names:
                with self.subTest(name=name):
                    entry['destination'] = name
                    with self.assertRaisesRegex(ValueError, message):
                        self.collect()

    def test_identical_inputs_produce_identical_archives(self):
        files = self.collect()
        first, second = self.root / 'first.zip', self.root / 'second.zip'
        TOOL.package(files, first)
        for path in files.values():
            os.utime(path, (1_000_000_000, 1_000_000_000))
            path.chmod(0o755)
        TOOL.package(files, second)
        self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_expected_failure_is_one_line(self):
        result = subprocess.run([sys.executable, str(ROOT / 'tools/demo-package.py'),
                                 '--manifest', str(self.root / 'absent.json'), '--fixture', str(self.root),
                                 '--recipe', str(self.root / 'recipe.json'),
                                 '--readme', str(self.root / 'demo-readme.md'),
                                 '--output', str(self.root / 'demo.zip')], capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(len(result.stderr.splitlines()), 1, result.stderr)
        self.assertIn('absent.json', result.stderr)


if __name__ == '__main__':
    unittest.main()
