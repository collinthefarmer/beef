"""Configure cmake/Compatibility.cmake in a small project and check what it derives."""
import copy
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PROFILE = json.loads((ROOT / 'cmake/compatibility/steam-1.6.1170.json').read_text())


class CompatibilityTests(unittest.TestCase):
    def setUp(self) -> None:
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name)
        for path in ('cmake/Compatibility.cmake', 'cmake/Plugin.cpp.in',
                     'src/cs/BSLightingShaderMaterialPBR.h', 'src/extern/SKSEMenuFramework.h'):
            (self.root / path).parent.mkdir(parents=True, exist_ok=True)
            (self.root / path).write_bytes((ROOT / path).read_bytes())
        (self.root / 'cmake/compatibility').mkdir()
        (self.root / 'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.24)\nproject(Example VERSION 0.1.0 LANGUAGES NONE)\n'
            'include(cmake/Compatibility.cmake)\n'
            'message(STATUS "pins ${BEEF_COMMONLIB_REVISION} ${BEEF_SPDLOG_REVISION} '
            '${BEEF_COMPILE_SE}${BEEF_COMPILE_AE}${BEEF_COMPILE_VR}")\n')

    def configure(self, profile: dict) -> subprocess.CompletedProcess[str]:
        (self.root / f'cmake/compatibility/{profile["id"]}.json').write_text(json.dumps(profile))
        return subprocess.run(['cmake', '-S', str(self.root), '-B', str(self.root / profile['id']),
                               f'-DBEEF_COMPATIBILITY_PROFILE={profile["id"]}'],
                              text=True, capture_output=True)

    def test_profile_sets_pins_declaration_and_runtime_guard(self) -> None:
        result = self.configure(PROFILE)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(f'pins {PROFILE["dependencies"]["commonlib"]} v1.15.3 ONONOFF', result.stdout)
        generated = self.root / PROFILE['id'] / 'generated'
        declaration = (generated / 'Plugin.cpp').read_text()
        self.assertIn('.RuntimeCompatibility = { REL::Version{ 1, 6, 1170, 0 } },', declaration)
        self.assertIn('.MinimumSKSEVersion = REL::Version{ 2, 2, 6, 0 }', declaration)
        test = generated / 'test.cpp'
        test.write_text('#include "BuildCompatibility.h"\n'
                        'using namespace BetterEnchantmentEffects::BuildCompatibility;\n'
                        'static_assert(SupportsRuntime(0x01064920));\n'
                        'static_assert(!SupportsRuntime(0x01064921));\n'
                        'static_assert(minimumSKSE == 0x02020060);\n')
        subprocess.run([os.environ.get('NATIVE_CXX', 'c++'), '-std=c++17', '-fsyntax-only', str(test)],
                       check=True)
        header = generated / 'BuildCompatibility.h'
        written = header.stat().st_mtime_ns
        self.assertEqual(self.configure(PROFILE).returncode, 0)
        self.assertEqual(header.stat().st_mtime_ns, written)

    def test_second_profile_lists_every_runtime(self) -> None:
        alternate = copy.deepcopy(PROFILE)
        alternate.update(id='test-two', runtimes=[{'version': [1, 5, 97, 0]}, {'version': [1, 6, 640, 0]}])
        self.assertEqual(self.configure(alternate).returncode, 0)
        header = (self.root / 'test-two/generated/BuildCompatibility.h').read_text()
        self.assertIn('a_runtime == 17106448u || a_runtime == 17180672u', header)

    def test_missing_field_or_changed_peer_header_stops_configuration(self) -> None:
        missing = copy.deepcopy(PROFILE)
        del missing['minimum_skse']
        missing['id'] = 'missing'
        self.assertIn('minimum_skse', self.configure(missing).stderr)
        (self.root / 'src/cs/BSLightingShaderMaterialPBR.h').write_text('changed layout\n')
        result = self.configure(PROFILE)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('BSLightingShaderMaterialPBR.h differs', result.stderr)


if __name__ == '__main__':
    unittest.main()
