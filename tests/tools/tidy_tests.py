"""Exercise analysis selection, failure handling and baseline gates with a fake tool."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class TidyTests(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name)
        for name in ('tools', 'src', 'build/Release'):
            (self.root / name).mkdir(parents=True)
        for name in ('tidy.py', 'tidy-baseline.py'):
            shutil.copy2(ROOT / 'tools' / name, self.root / 'tools' / name)
        entries = []
        for name in ('One.cpp', 'Two.cpp'):
            source = self.root / 'src' / name
            source.write_text('')
            entries.append({'file': str(source), 'directory': str(self.root),
                            'command': 'clang++ -c ' + str(source)})
        (self.root / 'src/Shared.h').write_text('')
        (self.root / 'build/Release/compile_commands.json').write_text(json.dumps(entries))
        self.fake = self.root / 'fake-tidy'
        self.fake.write_text('#!/usr/bin/env bash\nexit 0\n')
        self.fake.chmod(0o755)
        self.env = dict(os.environ, CLANG_TIDY=str(self.fake))
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        subprocess.run(['git', '-C', str(self.root), 'add', '.'], check=True)
        subprocess.run(['git', '-C', str(self.root), '-c', 'user.name=Test',
                        '-c', 'user.email=test@example.invalid',
                        'commit', '-qm', 'fixture'], check=True)

    def invoke(self, script, *args):
        return subprocess.run([sys.executable, 'tools/' + script, *args], cwd=self.root,
                              env=self.env, text=True, capture_output=True)

    def tidy(self, *args):
        result = self.invoke('tidy.py', *args)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads((self.root / 'build/tidy/latest.json').read_text())

    def test_source_selection_and_header_expansion(self):
        self.assertEqual(self.tidy('src/One.cpp')['files'], ['src/One.cpp'])
        result = self.tidy('src/Shared.h')
        self.assertTrue(result['full'])
        self.assertEqual(len(result['files']), 2)

    def test_empty_changed_selection_stays_empty_and_header_change_runs_all(self):
        self.assertEqual(self.tidy('--changed')['files'], [])
        (self.root / 'src/Shared.h').write_text('changed')
        self.assertTrue(self.tidy('--changed')['full'])

    def test_missing_database_entry_fails_and_invalidates_previous_report(self):
        self.tidy()
        (self.root / 'src/New.cpp').write_text('')
        result = self.invoke('tidy.py', 'src/New.cpp')
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / 'build/tidy/latest.json').exists())
        self.assertNotEqual(self.invoke('tidy.py').returncode, 0)

    def test_failed_run_cannot_reuse_prior_success(self):
        self.tidy()
        self.fake.write_text('#!/usr/bin/env bash\necho interrupted\nexit 1\n')
        self.assertNotEqual(self.invoke('tidy.py').returncode, 0)
        self.assertNotEqual(self.invoke('tidy-baseline.py', '--check').returncode, 0)
        self.fake.write_text('#!/usr/bin/env bash\nexit 0\n')
        self.tidy()

    def test_analyzer_names_and_external_findings_are_retained(self):
        header = self.root / 'src/Shared.h'
        external = self.root / 'vendor/Library.h'
        self.fake.write_text(
            '#!/usr/bin/env bash\n' +
            f'echo "{header}:3:1: warning: leak [clang-analyzer-cplusplus.NewDeleteLeaks]"\n' +
            f'echo "{header}:4:1: warning: value [clang-analyzer-core.uninitialized.Assign]"\n' +
            f'echo "{external}:9:1: warning: address [clang-analyzer-core.FixedAddressDereference]"\n')
        result = self.invoke('tidy.py', '--analyzer')
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads((self.root / 'build/tidy-analyzer/latest.json').read_text())
        self.assertEqual(report['diagnostics'], [
            ['src/Shared.h', 3, 'clang-analyzer-cplusplus.NewDeleteLeaks'],
            ['src/Shared.h', 4, 'clang-analyzer-core.uninitialized.Assign']])
        self.assertEqual(report['external_diagnostics'], [
            [str(external), 9, 'clang-analyzer-core.FixedAddressDereference']])
        self.assertTrue(report['full'])
        self.assertTrue(report['analyzer'])
        self.assertFalse((self.root / 'build/tidy/latest.json').exists())

    def test_header_findings_are_deduplicated_and_gate_ignores_line_movement(self):
        header = self.root / 'src/Shared.h'
        self.fake.write_text(f'#!/usr/bin/env bash\necho "{header}:3:1: warning: problem [bugprone-example]"\n')
        result = self.tidy()
        self.assertEqual(len(result['diagnostics']), 1)
        self.assertEqual(self.invoke('tidy-baseline.py').returncode, 0)
        self.fake.write_text(f'#!/usr/bin/env bash\necho "{header}:9:1: warning: problem [bugprone-example]"\n')
        self.tidy()
        self.assertEqual(self.invoke('tidy-baseline.py', '--check').returncode, 0)
        with self.fake.open('a') as out:
            out.write(f'echo "{header}:10:1: warning: second [bugprone-example]"\n')
        self.tidy('src/One.cpp')
        self.assertNotEqual(self.invoke('tidy-baseline.py', '--gate', 'src/One.cpp').returncode, 0)
        self.assertNotEqual(self.invoke('tidy-baseline.py', '--check').returncode, 0)
        self.assertNotEqual(self.invoke('tidy-baseline.py').returncode, 0)


if __name__ == '__main__':
    unittest.main()
