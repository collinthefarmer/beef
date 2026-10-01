"""Check the unattended run host's settings, run file and report without a game."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('regression_run', ROOT / 'tools/regression-run.py')
host = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(host)

LOCAL = '''MO2_MODS_DIR=/mods
MO2_EXE="/mnt/c/MO2/ModOrganizer.exe"
MO2_PROFILE=Skyrim 2
MO2_LAUNCH=SKSE
SKSE_LOG_DIR=/logs
OTHER=ignored
'''


class RegressionRunTests(unittest.TestCase):
    def test_settings_come_from_local_env_and_environment_wins(self):
        settings = host.read_settings({'MO2_LAUNCH': 'SKSE - beef testing'}, host.local_env(LOCAL))
        self.assertEqual(settings, host.Settings(Path('/mnt/c/MO2/ModOrganizer.exe'), 'Skyrim 2',
                                                 'SKSE - beef testing', Path('/logs')))
        self.assertNotIn('OTHER', host.local_env(LOCAL))

    def test_missing_settings_are_named(self):
        problem = host.read_settings({}, {'MO2_EXE': 'x'})
        self.assertIsInstance(problem, str)
        for key in ('MO2_PROFILE', 'MO2_LAUNCH', 'SKSE_LOG_DIR'):
            self.assertIn(key, problem)
        self.assertNotIn('MO2_EXE (', problem)

    def test_run_request_matches_the_plugin_format(self):
        request = host.run_request('20261001T120000', 'BEEFRegression', ['lifecycle'], 1000.5)
        self.assertEqual(request, {'format': 1, 'run': '20261001T120000', 'save': 'BEEFRegression',
                                   'suite': ['lifecycle'], 'notAfter': 1600})

    def test_report_reads_the_end_line_and_skips_partial_lines(self):
        text = '\n'.join([
            json.dumps({'kind': 'start', 'run': 'r', 'trace': 'trace.jsonl'}),
            json.dumps({'kind': 'step', 'case': 'lifecycle', 'step': 0, 'action': 'solo',
                        'outcome': 'PASS', 'frames': 0, 'reason': ''}),
            json.dumps({'kind': 'case', 'case': 'lifecycle', 'outcome': 'BLOCKED'}),
            json.dumps({'kind': 'end', 'outcome': 'BLOCKED', 'reason': ''}),
            '{"kind": "st',
        ])
        lines = host.result_lines(text)
        self.assertTrue(host.ended(lines))
        report = host.report_of(lines)
        self.assertEqual((report.outcome, report.trace), ('BLOCKED', 'trace.jsonl'))
        printed = host.format_report(report)
        self.assertIn('lifecycle  0     solo', printed)
        self.assertIn('run: BLOCKED', printed)

    def test_report_without_an_end_line_fails(self):
        report = host.report_of(host.result_lines(json.dumps({'kind': 'start'})))
        self.assertEqual(report.outcome, 'FAIL')
        crashed = host.report_of([], 'CRASHED', 'the game exited without an end line')
        self.assertIn('CRASHED (the game exited', host.format_report(crashed))

    def test_plus_separates_launches(self):
        self.assertEqual(host.launches(['lifecycle']), [['lifecycle']])
        self.assertEqual(host.launches(['studio-save', '+', 'studio-reload', 'unload']),
                         [['studio-save'], ['studio-reload', 'unload']])
        for bad in (['+', 'lifecycle'], ['lifecycle', '+'], ['a', '+', '+', 'b']):
            self.assertIsInstance(host.launches(bad), str)

    def test_missing_save_checks_the_profile_save_folder(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            saves = root / 'profiles' / 'Skyrim 2' / 'saves'
            saves.mkdir(parents=True)
            mods = str(root / 'mods')
            self.assertEqual(host.missing_save(mods, 'Skyrim 2', 'BEEFRegression'),
                             saves / 'BEEFRegression.ess')
            (saves / 'BEEFRegression.ess').write_bytes(b'')
            self.assertIsNone(host.missing_save(mods, 'Skyrim 2', 'BEEFRegression'))
            self.assertIsNone(host.missing_save(mods, 'Global saves', 'BEEFRegression'))

    def test_only_this_runs_unread_file_is_withdrawn(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / host.RUN_FILE
            path.write_text(json.dumps({'run': 'other'}))
            host.withdraw_run_file(path, 'mine')
            self.assertTrue(path.exists())
            path.write_text(json.dumps({'run': 'mine'}))
            host.withdraw_run_file(path, 'mine')
            self.assertFalse(path.exists())
            host.withdraw_run_file(path, 'mine')


if __name__ == '__main__':
    unittest.main()
