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

    def run_test(self, source: Path) -> None:
        program = source.with_suffix('')
        subprocess.run([os.environ.get('NATIVE_CXX', 'c++'), '-std=c++17', '-o', str(program), str(source)],
                       check=True)
        subprocess.run([str(program)], check=True)

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
                        'static_assert(SupportsRuntime(0x01064920, 0x01070680));\n'
                        'static_assert(!SupportsRuntime(0x01064921, 0x01070680));\n'
                        'static_assert(!SupportsRuntime(0x01070680, 0x01070680));\n'
                        'static_assert(minimumSKSE == 0x02020060);\n'
                        'int main() { return SupportedRuntimes("1.7.104") == "Skyrim 1.6.1170" ? 0 : 1; }\n')
        self.run_test(test)
        header = generated / 'BuildCompatibility.h'
        written = header.stat().st_mtime_ns
        self.assertEqual(self.configure(PROFILE).returncode, 0)
        self.assertEqual(header.stat().st_mtime_ns, written)

    def test_address_library_profile_accepts_every_runtime_of_a_compiled_family(self) -> None:
        library = json.loads((ROOT / 'cmake/compatibility/address-library.json').read_text())
        self.assertEqual(library['runtimes'], 'address_library')
        result = self.configure(library)
        self.assertEqual(result.returncode, 0, result.stderr)
        generated = self.root / library['id'] / 'generated'
        declaration = (generated / 'Plugin.cpp').read_text()
        self.assertIn('.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary,',
                      declaration)
        test = generated / 'test.cpp'
        test.write_text('#include "BuildCompatibility.h"\n'
                        'using namespace BetterEnchantmentEffects::BuildCompatibility;\n'
                        'constexpr std::uint32_t newest = 0x01070680;\n'
                        'static_assert(SupportsRuntime(0x01050610, newest));\n'
                        'static_assert(SupportsRuntime(0x010613D0, newest));\n'
                        'static_assert(SupportsRuntime(0x01064920, newest));\n'
                        'static_assert(SupportsRuntime(0x01064931, newest));\n'
                        'static_assert(SupportsRuntime(0x01070630, newest));\n'
                        'static_assert(SupportsRuntime(0x01070681, newest));\n'
                        'static_assert(!SupportsRuntime(0x01070690, newest));\n'
                        'static_assert(!SupportsRuntime(0x01080000, newest));\n'
                        'static_assert(!SupportsRuntime(0x010613C0, newest));\n'
                        'static_assert(!SupportsRuntime(0x010400F0, newest));\n'
                        'static_assert(!SupportsRuntime(0x01070630, 0x01064920));\n'
                        'int main() {\n'
                        '  return SupportedRuntimes("1.7.104") ==\n'
                        '      "Skyrim SE 1.5 or Skyrim AE 1.6.317 to 1.7.104" ? 0 : 1;\n'
                        '}\n')
        self.run_test(test)

    def test_address_library_profile_needs_a_compiled_family(self) -> None:
        library = json.loads((ROOT / 'cmake/compatibility/address-library.json').read_text())
        library.update(id='no-family', compiled={'se': False, 'ae': False, 'vr': True})
        result = self.configure(library)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('address_library needs the SE or AE family compiled', result.stderr)

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
