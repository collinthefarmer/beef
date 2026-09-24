"""Check incremental native builds using a tiny source tree and the real CMake files."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BuildTests(unittest.TestCase):
    def test_native_graph_rebuilds_dependencies_and_skips_unchanged_links(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for name in ('cmake', 'src/recipe', 'src/engine', 'src/validator', 'tests'):
                (root / name).mkdir(parents=True)
            shutil.copy2(ROOT / 'CMakeLists.txt', root / 'CMakeLists.txt')
            shutil.copy2(ROOT / 'cmake/Native.cmake', root / 'cmake/Native.cmake')
            (root / 'src/Settings.cpp').write_text('int settings() { return 0; }\n')
            header = root / 'src/recipe/Shared.h'
            header.write_text('inline constexpr int answer = 0;\n')
            source = root / 'src/recipe/Value.cpp'
            source.write_text('#include "recipe/Shared.h"\nint value() { return answer; }\n')
            for name in ('SessionQueue', 'ApplicationService', 'TextFile', 'PluginEvents',
                         'RecipeFiles', 'InstanceTime', 'MenuDependency'):
                (root / f'src/engine/{name}.cpp').write_text(f'int {name}() {{ return 0; }}\n')
            main = 'int value(); int main() { return value(); }\n'
            (root / 'src/validator/Main.cpp').write_text(main)
            (root / 'tests/value_tests.cpp').write_text(main)
            build = root / 'out'
            def call(*args):
                result = subprocess.run(args, cwd=root, text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                return result.stdout
            call('cmake', '-S', str(root), '-B', str(build), '-G', 'Ninja',
                 '-DCMAKE_CXX_COMPILER=' + os.environ.get('NATIVE_CXX', 'clang++'),
                 '-DCHECK_JSONSCHEMA=' + shutil.which('true'))
            call('cmake', '--build', str(build), '--parallel', '2')
            executable = build / 'value'
            first = executable.stat().st_mtime_ns
            call('cmake', '--build', str(build))
            self.assertEqual(executable.stat().st_mtime_ns, first)
            call('ctest', '--test-dir', str(build), '-R', '^value$', '--output-on-failure')
            header.write_text('inline constexpr int answer = 1;\n')
            call('cmake', '--build', str(build))
            self.assertNotEqual(executable.stat().st_mtime_ns, first)
            self.assertEqual(subprocess.run([str(executable)]).returncode, 1)
            source.rename(source.with_name('Renamed.cpp'))
            header.write_text('inline constexpr int answer = 0;\n')
            call('cmake', '--build', str(build))
            self.assertEqual(subprocess.run([str(executable)]).returncode, 0)
            before = executable.stat().st_mtime_ns
            call('cmake', '-S', str(root), '-B', str(build), '-DCMAKE_CXX_FLAGS=-DOPTION_CHANGED=1')
            call('cmake', '--build', str(build))
            self.assertNotEqual(executable.stat().st_mtime_ns, before)
            (root / 'tests/value_tests.cpp').unlink()
            call('cmake', '--build', str(build))
            listing = call('ctest', '--test-dir', str(build), '-N')
            self.assertNotIn(': value', listing)


if __name__ == '__main__':
    unittest.main()
