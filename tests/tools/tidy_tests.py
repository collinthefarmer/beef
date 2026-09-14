"""Exercise lint selection and cache validity without invoking clang-tidy."""
import json
import os
import pathlib
import shutil
import subprocess
import tempfile
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class TidyTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = pathlib.Path(self.folder.name)
        for directory in ('tools', 'src', 'build/clangd', 'build/tidy', 'bin'):
            (self.root / directory).mkdir(parents=True)
        shutil.copy2(ROOT / 'tools/tidy.sh', self.root / 'tools/tidy.sh')
        self.old = time.time() - 100
        for path in ('src/Selected.cpp', 'src/Shared.h', '.clang-tidy',
                     'build/clangd/compile_commands.json'):
            self.write(path, '', self.old)
        self.write('build/tidy/src_Selected.txt', '')
        self.write('build/tidy/src_Unrelated.txt',
                   '/repo/src/Unrelated.cpp:1:1: warning: cached [bugprone-example]\n')
        self.environment = dict(os.environ)
        self.environment['PATH'] = str(self.root / 'bin') + os.pathsep + os.environ['PATH']

    def write(self, path, text, timestamp=None):
        target = self.root / path
        target.write_text(text)
        if timestamp is not None:
            os.utime(target, (timestamp, timestamp))
        return target

    def run_tidy(self, *arguments):
        return subprocess.run(['bash', 'tools/tidy.sh', *arguments],
                              cwd=self.root, env=self.environment,
                              text=True, capture_output=True)

    def test_targeted_summary_excludes_unrelated_and_deleted_sources(self):
        result = self.run_tidy('--summary', 'src/Selected.cpp')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('1 of 1 files have fresh results, 0 findings', result.stdout)
        result = self.run_tidy('--summary')
        self.assertIn('1 of 1 files have fresh results, 0 findings', result.stdout)

    def test_source_header_config_and_database_changes_invalidate_results(self):
        for dependency in ('src/Selected.cpp', 'src/Shared.h', '.clang-tidy',
                           'build/clangd/compile_commands.json'):
            with self.subTest(dependency=dependency):
                os.utime(self.root / dependency, (self.old + 50, self.old + 50))
                os.utime(self.root / 'build/tidy/src_Selected.txt',
                         (self.old + 25, self.old + 25))
                result = self.run_tidy('--summary', 'src/Selected.cpp')
                self.assertIn('0 of 1 files have fresh results', result.stdout)
                os.utime(self.root / dependency, (self.old, self.old))

    def test_recorded_dependencies_narrow_header_invalidation(self):
        (self.root / 'build/Release').mkdir(parents=True)
        self.write('build/Release/.ninja_deps', '')
        self.write('src/Other.h', '', self.old)
        obj = 'CMakeFiles/Native.dir/src/Selected.cpp.obj'
        self.write('build/clangd/compile_commands.json', json.dumps([{
            'file': str(self.root / 'src/Selected.cpp'),
            'output': str(self.root / 'build/Release' / obj),
            'command': 'clang-cl src/Selected.cpp',
        }]), self.old)
        ninja = self.write('bin/ninja', '#!/usr/bin/env bash\n'
                           f'printf "{obj}: #deps 2, deps mtime 1 (VALID)\\n'
                           f'    {self.root}/src/Selected.cpp\\n'
                           f'    {self.root}/src/Shared.h\\n"\n')
        ninja.chmod(0o755)
        os.utime(self.root / 'build/tidy/src_Selected.txt',
                 (self.old + 25, self.old + 25))
        os.utime(self.root / 'src/Other.h', (self.old + 50, self.old + 50))
        result = self.run_tidy('--summary', 'src/Selected.cpp')
        self.assertIn('1 of 1 files have fresh results', result.stdout,
                      'a header the object never included does not invalidate it')
        os.utime(self.root / 'src/Shared.h', (self.old + 50, self.old + 50))
        result = self.run_tidy('--summary', 'src/Selected.cpp')
        self.assertIn('0 of 1 files have fresh results', result.stdout,
                      'a header the object included does invalidate it')

    def test_empty_changed_selection_does_not_fall_back_to_all_sources(self):
        git = self.write('bin/git', '#!/usr/bin/env bash\nexit 0\n')
        git.chmod(0o755)
        result = self.run_tidy('--summary', '--changed')
        self.assertIn('0 of 0 files have fresh results', result.stdout)

    def test_failed_refresh_is_not_reported_or_reused_as_success(self):
        fake = self.write('bin/fake-tidy',
                          '#!/usr/bin/env bash\necho failed >&2\nexit 1\n')
        fake.chmod(0o755)
        self.environment['CLANG_TIDY'] = str(fake)
        result = self.run_tidy('--force', 'src/Selected.cpp')
        self.assertNotEqual(result.returncode, 0)
        result = self.run_tidy('--summary', 'src/Selected.cpp')
        self.assertIn('0 of 1 files have fresh results', result.stdout)
        fake.write_text('#!/usr/bin/env bash\necho refreshed\n')
        result = self.run_tidy('src/Selected.cpp')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('tidy src/Selected.cpp', result.stderr)
        self.assertIn('1 of 1 files have fresh results, 0 findings', result.stdout)
        self.assertFalse((self.root / 'build/tidy/src_Selected.txt.part').exists())


if __name__ == '__main__':
    unittest.main()
