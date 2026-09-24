"""Check profile generation and target identity without downloading dependencies."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('compatibility', ROOT / 'tools/compatibility.py')
TOOL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TOOL)
PROFILE = ROOT / 'cmake/compatibility/steam-1.6.1170.json'


class CompatibilityTests(unittest.TestCase):
    def test_baseline_and_rejections(self):
        profile = json.loads(PROFILE.read_text())
        TOOL.validate(profile, ROOT)
        changes = [({'runtime_verified': True}), ({'minimum_skse': [0, 0, 0, 0]}),
                   ({'id': '../escape'}), ({'compiled': {'se': True, 'ae': True, 'vr': True}}),
                   ({'runtimes': [{'version': [1, 6, 1170, 1], 'storefront': 'steam'}]}),
                   ({'runtimes': [{'version': [1, 7, 104, 0], 'storefront': 'steam'}]})]
        for change in changes:
            with self.subTest(change=change), self.assertRaises(ValueError):
                TOOL.validate(dict(profile, **change), ROOT)
        profile['peers']['community_shaders']['sha256'] = '0' * 64
        with self.assertRaises(ValueError):
            TOOL.validate(profile, ROOT)

    def test_generated_guard_and_stable_output(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            TOOL.generate(PROFILE, output, ROOT)
            before = {p.name: p.stat().st_mtime_ns for p in output.iterdir()}
            TOOL.generate(PROFILE, output, ROOT)
            self.assertEqual(before, {p.name: p.stat().st_mtime_ns for p in output.iterdir()})
            source = output / 'test.cpp'
            source.write_text('#include "BuildCompatibility.h"\n'
                              'using namespace BetterEnchantmentEffects::BuildCompatibility;\n'
                              'static_assert(SupportsRuntime(0x01064920));\n'
                              'static_assert(!SupportsRuntime(0x01064921));\n'
                              'static_assert(!SupportsRuntime(0x01070680));\n'
                              'static_assert(minimumSKSE == 0x02020060);\n')
            subprocess.run([os.environ.get('NATIVE_CXX', 'c++'), '-std=c++17', '-fsyntax-only', str(source)], check=True)

    def test_cmake_and_identity_follow_selected_profile(self):
        profile = json.loads(PROFILE.read_text())
        alternate = copy.deepcopy(profile)
        alternate.update(id='test-se', compiled={'se': True, 'ae': False, 'vr': False},
                         runtimes=[{'version': [1, 5, 97, 0], 'storefront': 'steam'}],
                         minimum_skse=[2, 0, 20, 0])
        alternate['address_library']['edition'] = 'SE'
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for path in ('cmake/Compatibility.cmake', 'cmake/Plugin.cpp.in', 'tools/compatibility.py',
                         'src/cs/BSLightingShaderMaterialPBR.h', 'src/extern/SKSEMenuFramework.h'):
                target = root / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes((ROOT / path).read_bytes())
            (root / 'cmake/compatibility').mkdir()
            for item in (profile, alternate):
                (root / 'cmake/compatibility' / (item['id'] + '.json')).write_text(json.dumps(item))
            (root / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.24)\n'
                'project(Example VERSION 0.1.0 LANGUAGES NONE)\nfind_package(Python3 REQUIRED COMPONENTS Interpreter)\n'
                'include(cmake/Compatibility.cmake)\nconfigure_file(cmake/Plugin.cpp.in Plugin.cpp @ONLY)\n')
            identities = []
            for item in (profile, alternate):
                output = root / item['id']
                subprocess.run(['cmake', '-S', str(root), '-B', str(output),
                                '-DBEEF_COMPATIBILITY_PROFILE=' + item['id']], check=True, capture_output=True)
                self.assertEqual((output / 'Plugin.cpp').read_text(), TOOL.declaration(item, 'Example', '0.1.0'))
                subprocess.run(['python3', str(ROOT / 'tools/build-identity.py'), '--root', str(ROOT),
                                '--output', str(output), '--config', 'Release', '--compatibility',
                                str(output / 'generated/compatibility.json')], check=True)
                identities.append(json.loads((output / 'build-identity.json').read_text()))
            self.assertEqual(identities[0]['source_sha256'], identities[1]['source_sha256'])
            self.assertNotEqual(identities[0]['build'], identities[1]['build'])
            self.assertNotEqual(identities[0]['configuration_sha256'], identities[1]['configuration_sha256'])


if __name__ == '__main__':
    unittest.main()
