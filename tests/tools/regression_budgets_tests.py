"""Check the soak window measures and the budget limits without a game."""
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('regression_budgets',
                                              ROOT / 'tools/regression_budgets.py')
budgets = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(budgets)


class RegressionBudgetTests(unittest.TestCase):
    def soak_events(self):
        def edge(time_ms, window, kind):
            return {'unix_ms': time_ms, 'fields': {'action': 'regression.window',
                                                   'window': window, 'edge': kind}}

        def beat(time_ms, frames, frame_us, frame_max_us, targets, target_bytes):
            return {'unix_ms': time_ms, 'fields': {
                'action': 'heartbeat', 'frames': str(frames), 'frame_us': str(frame_us),
                'frame_max_us': str(frame_max_us), 'targets': str(targets),
                'target_bytes': str(target_bytes)}}
        return [edge(0, 'baseline', 'begin'), beat(500, 150, 1000, 50, 1, 10),
                beat(1000, 150, 1000, 50, 1, 10), edge(1000, 'baseline', 'end'),
                edge(1000, 'burst', 'begin'), edge(1200, 'burst', 'end'),
                edge(1200, 'settle', 'begin'), beat(2000, 10, 50000, 900000, 80, 500),
                edge(2000, 'settle', 'end'), edge(2000, 'steady', 'begin'),
                beat(3000, 75, 150000, 4000, 20, 300), edge(3000, 'steady', 'end'),
                edge(3100, 'recovery', 'begin'), beat(4000, 140, 2000, 60, 5, 40),
                edge(4100, 'recovery', 'end'), edge(5000, 'unclosed', 'begin')]

    def test_windows_pair_the_plugin_window_events(self):
        names = [(w.name, w.start_ms, w.end_ms) for w in budgets.windows(self.soak_events())]
        self.assertEqual(names, [('baseline', 0, 1000), ('burst', 1000, 1200),
                                 ('settle', 1200, 2000), ('steady', 2000, 3000),
                                 ('recovery', 3100, 4100)])

    def test_measure_sums_the_heartbeats_inside_a_window(self):
        measured = budgets.measurements(self.soak_events())
        self.assertEqual(measured['baseline']['fps'], 300.0)
        self.assertEqual(measured['steady']['plugin_frame_us_mean'], 2000.0)
        self.assertEqual(measured['settle']['frame_max_us'], 900000)
        self.assertEqual(measured['recovery']['targets_end'], 5)

    def test_budgets_name_each_failure(self):
        measured = budgets.measurements(self.soak_events())
        limits = budgets.parse_budgets({
            'settle': {'frame_max_us_max': 500000},
            'steady': {'fps_ratio_min': 0.5, 'plugin_frame_us_mean_max': 4000},
            'recovery': {'targets_end_over_baseline_max': 2,
                         'target_bytes_end_over_baseline_max': 100},
            'missing': {'fps_ratio_min': 1.0}})
        self.assertIsInstance(limits, list)
        failures = budgets.budget_failures(measured, limits)
        self.assertEqual(failures, [
            'settle: frame_max_us 900000 over 500000',
            'steady: fps 75.0 is 0.25 of baseline, under 0.5',
            'recovery: targets_end grew by 4 over baseline, over 2'])

    def test_an_unknown_budget_key_is_an_error(self):
        for raw in ({'steady': {'fps_ratio_minimum': 0.5}},
                    {'steady': {'frames_max': 10}},
                    {'steady': {'fps_ratio_min': '0.5'}},
                    {'steady': [1]},
                    []):
            self.assertIsInstance(budgets.parse_budgets(raw), str, raw)

    def test_the_shipped_budgets_parse(self):
        raw = json.loads((ROOT / 'tests/regression/budgets.json').read_text(encoding='utf-8'))
        self.assertIsInstance(budgets.parse_budgets(raw), list)


if __name__ == '__main__':
    unittest.main()
