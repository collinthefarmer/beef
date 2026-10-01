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

    def soak_events(self):
        def step(time_ms, label):
            return {'unix_ms': time_ms, 'fields': {'action': 'regression.step',
                                                   'operation': label}}

        def beat(time_ms, frames, frame_us, frame_max_us, targets, target_bytes):
            return {'unix_ms': time_ms, 'fields': {
                'action': 'heartbeat', 'frames': str(frames), 'frame_us': str(frame_us),
                'frame_max_us': str(frame_max_us), 'targets': str(targets),
                'target_bytes': str(target_bytes)}}
        return [step(0, 'solo arcane-circuit'), beat(500, 150, 1000, 50, 1, 10),
                beat(1000, 150, 1000, 50, 1, 10), step(1000, 'hold baseline 1s'),
                step(1100, 'spawn-crowd 12'), step(1200, 'await-crowd-rendered'),
                beat(2000, 10, 50000, 900000, 80, 500), step(2000, 'hold settle 1s'),
                beat(3000, 75, 150000, 4000, 20, 300), step(3000, 'hold steady 1s'),
                step(3100, 'despawn-crowd'), beat(4000, 140, 2000, 60, 5, 40),
                step(4100, 'hold recovery 1s')]

    def test_windows_follow_the_step_events(self):
        names = [(w.name, w.start_ms, w.end_ms) for w in host.windows(self.soak_events())]
        self.assertEqual(names, [('baseline', 0, 1000), ('burst', 1000, 1200),
                                 ('settle', 1200, 2000), ('steady', 2000, 3000),
                                 ('recovery', 3100, 4100)])

    def test_measure_sums_the_heartbeats_inside_a_window(self):
        measured = host.measurements(self.soak_events())
        self.assertEqual(measured['baseline']['fps'], 300.0)
        self.assertEqual(measured['steady']['plugin_frame_us_mean'], 2000.0)
        self.assertEqual(measured['settle']['frame_max_us'], 900000)
        self.assertEqual(measured['recovery']['targets_end'], 5)

    def test_budgets_name_each_failure(self):
        measured = host.measurements(self.soak_events())
        failures = host.budget_failures(measured, {
            'settle': {'frame_max_us_max': 500000},
            'steady': {'fps_ratio_min': 0.5, 'plugin_frame_us_mean_max': 4000},
            'recovery': {'targets_end_over_baseline_max': 2,
                         'target_bytes_end_over_baseline_max': 100},
            'missing': {'fps_ratio_min': 1.0}})
        self.assertEqual(failures, [
            'settle: frame_max_us 900000 over 500000',
            'steady: fps 75.0 is 0.25 of baseline, under 0.5',
            'recovery: targets_end grew by 4 over baseline, over 2'])

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
