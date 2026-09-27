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


class TidyTests(unittest.TestCase):
    def setUp(self) -> None:
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name)
        for name in ('tools', 'src/extern', 'build/Release'):
            (self.root / name).mkdir(parents=True)
        shutil.copy2(ROOT / 'tools/tidy.py', self.root / 'tools/tidy.py')
        entries = []
        for name in ('One.cpp', 'Two.cpp', 'extern/Vendor.cpp'):
            (self.root / 'src' / name).write_text('')
            entries.append({'file': f'src/{name}', 'directory': str(self.root), 'command': 'clang++'})
        (self.root / 'build/Release/compile_commands.json').write_text(json.dumps(entries))
        self.fake = self.root / 'fake-tidy'
        self.env = dict(os.environ, CLANG_TIDY=str(self.fake))
        self.findings()

    def findings(self, *lines: str, status: int = 0) -> None:
        echoes = ''.join(f'echo "{line}"\n' for line in lines)
        self.fake.write_text(f'#!/usr/bin/env bash\n{echoes}exit {status}\n')
        self.fake.chmod(0o755)

    def tidy(self, *args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run([sys.executable, 'tools/tidy.py', *args], cwd=self.root, env=self.env,
                              text=True, capture_output=True)

    def test_findings_are_deduplicated_and_limited_to_first_party_sources(self) -> None:
        header = self.root / 'src/Shared.h'
        self.findings(f'{header}:3:1: warning: problem [bugprone-example]',
                      f'{self.root}/src/extern/Vendor.h:9:1: warning: vendor [bugprone-example]')
        result = self.tidy()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.count('src/Shared.h:3: [bugprone-example]'), 1)
        self.assertNotIn('Vendor', result.stdout)
        self.assertIn('2 translation units, 1 distinct first-party findings', result.stdout)
        self.assertIn('1 translation units', self.tidy('src/One.cpp').stdout)
        self.assertNotEqual(self.tidy('src/Absent.cpp').returncode, 0)

    def test_baseline_compares_counts_per_file_and_check(self) -> None:
        header = self.root / 'src/Shared.h'
        self.findings(f'{header}:3:1: warning: problem [bugprone-example]')
        self.assertEqual(self.tidy('--update').returncode, 0)
        baseline = (self.root / 'tools/tidy-baseline.txt').read_text()
        self.assertTrue(baseline.startswith('# clang-tidy baseline, 2 sources, 1 findings\n'))
        self.assertIn(f"{'# bugprone-example':<55}1\n", baseline)
        self.assertTrue(baseline.endswith('\nsrc/Shared.h:3: [bugprone-example]\n'))
        self.findings(f'{header}:9:1: warning: problem [bugprone-example]')
        self.assertEqual(self.tidy('--check').returncode, 0)
        self.findings(f'{header}:9:1: warning: problem [bugprone-example]',
                      f'{header}:10:1: warning: second [bugprone-example]')
        result = self.tidy('src/One.cpp', '--check')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('src/Shared.h: [bugprone-example] +1 beyond the baseline', result.stdout)
        self.assertNotEqual(self.tidy('src/One.cpp', '--update').returncode, 0)
        self.assertEqual((self.root / 'tools/tidy-baseline.txt').read_text(), baseline)

    def test_failed_analysis_or_incomplete_database_fails(self) -> None:
        self.findings('interrupted', status=1)
        result = self.tidy('--check')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('clang-tidy failed on src/', result.stderr)
        self.findings()
        (self.root / 'src/New.cpp').write_text('')
        self.assertIn('lacks src/New.cpp', self.tidy().stderr)


if __name__ == '__main__':
    unittest.main()
