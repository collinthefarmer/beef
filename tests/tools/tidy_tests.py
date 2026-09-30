"""Exercise source selection, failure handling and the baseline comparison with a fake clang-tidy."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SIZE = 'readability-function-size'


class TidyTests(unittest.TestCase):
    def setUp(self) -> None:
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name)
        for name in ('tools', 'src/extern', 'build/Release'):
            (self.root / name).mkdir(parents=True)
        shutil.copy2(ROOT / 'tools/tidy.py', self.root / 'tools/tidy.py')
        (self.root / 'tools/tidy-baseline.txt').write_text('')
        (self.root / '.clang-tidy').write_text('Checks: -*\n')
        (self.root / 'src/Shared.h').write_text('')
        (self.root / 'src/Other.h').write_text('')
        (self.root / 'src/Forced.h').write_text('')
        (self.root / 'build/Release/pch.hxx').write_text(f'#include "{self.root}/src/Forced.h"\n')
        entries = []
        for name, text in (('One.cpp', '#include "Shared.h"\n'), ('Two.cpp', '#include <Other.h>\n'),
                           ('extern/Vendor.cpp', '')):
            (self.root / 'src' / name).write_text(text)
            entries.append({'file': f'src/{name}', 'directory': str(self.root),
                            'command': f'clang-cl /FI{self.root}/build/Release/pch.hxx'})
        (self.root / 'build/Release/compile_commands.json').write_text(json.dumps(entries))
        self.fake = self.root / 'fake-tidy'
        self.calls = self.root / 'calls.txt'
        self.env = dict(os.environ, CLANG_TIDY=str(self.fake))
        self.findings()

    def findings(self, *lines: str, status: int = 0) -> None:
        echoes = ''.join(f'echo "{line}"\n' for line in lines)
        self.fake.write_text('#!/usr/bin/env bash\n'
                             'if [ "$1" = --list-checks ]; then\n'
                             '  printf "Enabled checks:\\n    bugprone-example\\n    readability-function-size\\n'
                             '    clang-analyzer-core.NullDereference\\n\\n"; exit 0\nfi\n'
                             f'echo "$*" >> {self.calls}\n{echoes}exit {status}\n')
        self.fake.chmod(0o755)

    def warning(self, line: int, message: str, check: str = SIZE, path: str = 'src/Shared.h') -> str:
        return f'{self.root}/{path}:{line}:1: warning: {message} [{check}]'

    def analyzed(self) -> list[str]:
        calls = self.calls.read_text().splitlines() if self.calls.exists() else []
        self.calls.unlink(missing_ok=True)
        return sorted(call.split()[-1] for call in calls)

    def tidy(self, *args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run([sys.executable, 'tools/tidy.py', *args], cwd=self.root, env=self.env,
                              text=True, capture_output=True)

    def git(self, *args: str) -> None:
        subprocess.run(['git', '-c', 'user.name=t', '-c', 'user.email=t@t', *args], cwd=self.root,
                       check=True, capture_output=True)

    def test_findings_are_deduplicated_and_limited_to_first_party_sources(self) -> None:
        self.findings(self.warning(3, 'problem', 'bugprone-example'),
                      f'{self.root}/src/extern/Vendor.h:9:1: warning: vendor [bugprone-example]')
        result = self.tidy()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.count('src/Shared.h:3: [bugprone-example] problem\n'), 1)
        self.assertNotIn('Vendor', result.stdout)
        self.assertIn('2 translation units, 1 distinct first-party findings', result.stdout)
        self.assertIn('1 translation units', self.tidy('src/One.cpp').stdout)
        self.assertEqual(self.tidy('src/Absent.cpp').returncode, 2)

    def test_update_writes_a_reviewable_baseline_with_messages(self) -> None:
        self.findings(self.warning(3, "function 'Draw' exceeds recommended size/complexity thresholds"))
        self.assertEqual(self.tidy('--update').returncode, 0)
        baseline = (self.root / 'tools/tidy-baseline.txt').read_text()
        self.assertTrue(baseline.startswith('# clang-tidy baseline, 2 sources, 1 findings\n'))
        self.assertIn(f"{'# ' + SIZE:<55}1\n", baseline)
        self.assertTrue(baseline.endswith(
            f"\nsrc/Shared.h:3: [{SIZE}] function 'Draw' exceeds recommended size/complexity thresholds\n"))
        for args in (('src/One.cpp', '--update'), ('--update', '--only', SIZE), ('--update', '--changed')):
            self.assertEqual(self.tidy(*args).returncode, 2)
        self.assertEqual((self.root / 'tools/tidy-baseline.txt').read_text(), baseline)

    def test_check_ignores_moved_lines_and_changed_numbers(self) -> None:
        self.findings(self.warning(3, "function 'Draw' has cognitive complexity of 30 (threshold 25)"))
        self.tidy('--update')
        self.findings(self.warning(40, "function 'Draw' has cognitive complexity of 31 (threshold 25)"))
        result = self.tidy('--check')
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertNotIn('beyond', result.stdout)

    def test_check_prints_each_new_finding_and_exits_one(self) -> None:
        self.findings(self.warning(3, "function 'Draw' exceeds thresholds"))
        self.tidy('--update')
        self.findings(self.warning(9, "function 'Draw' exceeds thresholds"),
                      self.warning(20, "function 'Fill' exceeds thresholds"),
                      self.warning(5, 'use nullptr', 'modernize-use-nullptr', 'src/One.cpp'))
        result = self.tidy('--check')
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn(f'src/One.cpp:5: [modernize-use-nullptr] use nullptr\n\n'
                      f"src/Shared.h:20: [{SIZE}] function 'Fill' exceeds thresholds\n\n"
                      f'[modernize-use-nullptr] +1 beyond the baseline\n'
                      f'[{SIZE}] +1 beyond the baseline\n', result.stdout)
        self.assertNotIn('Shared.h:9:', result.stdout)
        self.assertEqual(self.tidy('src/One.cpp', '--check').returncode, 1)

    def test_rows_without_a_message_allow_a_count_per_file_and_check(self) -> None:
        (self.root / 'tools/tidy-baseline.txt').write_text(f'# old\n\nsrc/Shared.h:3: [{SIZE}]\n')
        self.findings(self.warning(9, "function 'Draw' exceeds thresholds"))
        self.assertEqual(self.tidy('--check').returncode, 0)
        self.findings(self.warning(9, "function 'Draw' exceeds thresholds"),
                      self.warning(20, "function 'Fill' exceeds thresholds"))
        result = self.tidy('--check')
        self.assertEqual(result.returncode, 1)
        self.assertIn(f'src/Shared.h:9: [{SIZE}]', result.stdout)
        self.assertIn(f'src/Shared.h:20: [{SIZE}]', result.stdout)
        self.assertIn(f'[{SIZE}] +1 beyond the baseline', result.stdout)

    def test_only_runs_the_enabled_checks_that_match(self) -> None:
        self.assertEqual(self.tidy('src/One.cpp', '--only', 'readability-*').returncode, 0)
        self.assertIn(f'--checks=-*,{SIZE} src/One.cpp', self.calls.read_text())
        self.analyzed()
        self.tidy('src/One.cpp')
        self.assertIn('--checks=-clang-analyzer-* src/One.cpp', self.calls.read_text())
        result = self.tidy('--only', 'clang-analyzer-*')
        self.assertEqual(result.returncode, 2)
        self.assertIn('need --analyzer', result.stderr)
        self.assertEqual(self.tidy('--only', 'cert-*').returncode, 2)

    def test_changed_selects_edited_sources_and_their_includers(self) -> None:
        self.git('init', '-q', '-b', 'main')
        self.git('add', '-A')
        self.git('commit', '-qm', 'base')
        self.git('checkout', '-qb', 'topic')
        (self.root / 'src/Other.h').write_text('int x;\n')
        self.git('commit', '-qam', 'topic')
        self.assertEqual(self.tidy('--changed').returncode, 0)
        self.assertEqual(self.analyzed(), ['src/Two.cpp'])
        (self.root / 'src/Shared.h').write_text('int y;\n')
        self.tidy('--changed', '--base', 'HEAD')
        self.assertEqual(self.analyzed(), ['src/One.cpp'])
        (self.root / 'src/Forced.h').write_text('int z;\n')
        self.tidy('--changed', '--base', 'HEAD')
        self.assertEqual(self.analyzed(), ['src/One.cpp', 'src/Two.cpp'])
        self.git('checkout', '-q', '--', '.')
        result = self.tidy('--changed', '--base', 'HEAD')
        self.assertIn('no changed sources', result.stdout)
        self.assertEqual(self.analyzed(), [])
        (self.root / 'src/New.cpp').write_text('')
        self.assertIn('lacks src/New.cpp', self.tidy('--changed').stderr)
        self.assertEqual(self.tidy('--changed', 'src/One.cpp').returncode, 2)

    def test_failed_analysis_or_incomplete_database_fails(self) -> None:
        self.findings('interrupted', status=1)
        result = self.tidy('--check')
        self.assertEqual(result.returncode, 2)
        self.assertIn('clang-tidy failed on src/', result.stderr)
        self.findings()
        (self.root / 'src/New.cpp').write_text('')
        self.assertIn('lacks src/New.cpp', self.tidy().stderr)


if __name__ == '__main__':
    unittest.main()
