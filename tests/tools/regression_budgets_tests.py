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
        names = [(w.case, w.name, w.start_ms, w.end_ms)
                 for w in budgets.windows(self.soak_events())]
        self.assertEqual(names, [('', 'baseline', 0, 1000), ('', 'burst', 1000, 1200),
                                 ('', 'settle', 1200, 2000), ('', 'steady', 2000, 3000),
                                 ('', 'recovery', 3100, 4100)])

    def test_measure_sums_the_heartbeats_inside_a_window(self):
        measured = budgets.measurements(self.soak_events())
        self.assertEqual(measured[('', 'baseline')]['fps'], 300.0)
        self.assertEqual(measured[('', 'steady')]['plugin_frame_us_mean'], 2000.0)
        self.assertEqual(measured[('', 'settle')]['frame_max_us'], 900000)
        self.assertEqual(measured[('', 'recovery')]['targets_end'], 5)

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

    def case_events(self, case, start_ms, gap_targets, frame_us):
        def edge(time_ms, window, kind):
            return {'unix_ms': time_ms, 'fields': {'action': 'regression.window',
                                                   'case': case, 'window': window,
                                                   'edge': kind}}

        def beat(time_ms, targets, frame_max_us):
            return {'unix_ms': time_ms, 'fields': {
                'action': 'heartbeat', 'frames': '100', 'frame_max_us': str(frame_max_us),
                'targets': str(targets), 'target_bytes': str(targets * 1000)}}
        events = [edge(start_ms, 'baseline', 'begin'), beat(start_ms + 50, 1, 10),
                  edge(start_ms + 100, 'baseline', 'end')]
        for cycle, targets in enumerate(gap_targets, start=1):
            at = start_ms + 100 * (2 * cycle)
            events += [edge(at, f'wave-{cycle}', 'begin'), beat(at + 50, 40, frame_us),
                       edge(at + 100, f'wave-{cycle}', 'end'),
                       edge(at + 100, f'gap-{cycle}', 'begin'), beat(at + 150, targets, 10),
                       edge(at + 200, f'gap-{cycle}', 'end')]
        return events

    def test_cases_keep_their_own_windows(self):
        events = sorted(self.case_events('soak-waves', 0, [3, 4, 9], 600000)
                        + self.case_events('soak-stack', 10000, [3, 3], 20),
                        key=lambda event: event['unix_ms'])
        measured = budgets.measurements(events)
        self.assertEqual(measured[('soak-waves', 'gap-3')]['targets_end'], 9)
        self.assertEqual(measured[('soak-stack', 'gap-2')]['targets_end'], 3)
        limits = budgets.parse_budgets({
            'wave-*': {'frame_max_us_max': 500000},
            'gap-*': {'targets_end_growth_max': 2}})
        self.assertIsInstance(limits, list)
        self.assertEqual(budgets.budget_failures(measured, limits), [
            'soak-waves/wave-1: frame_max_us 600000 over 500000',
            'soak-waves/wave-2: frame_max_us 600000 over 500000',
            'soak-waves/wave-3: frame_max_us 600000 over 500000',
            'soak-waves/gap-*: targets_end grew by 6 from gap-1 to gap-3, over 2'])

    def test_live_targets_exclude_the_pool_scratch_and_resident_targets(self):
        events = [
            {'unix_ms': 0, 'fields': {'action': 'regression.window', 'case': 'soak',
                                      'window': 'recovery', 'edge': 'begin'}},
            {'unix_ms': 500, 'fields': {
                'action': 'heartbeat', 'frames': '60', 'targets': '20',
                'target_bytes': '2000', 'pool_targets': '5', 'pool_bytes': '500',
                'scratch_targets': '3', 'scratch_bytes': '300',
                'resident_targets': '1', 'resident_bytes': '100'}},
            {'unix_ms': 1000, 'fields': {'action': 'regression.window', 'case': 'soak',
                                         'window': 'recovery', 'edge': 'end'}}]
        measured = budgets.measurements(events)[('soak', 'recovery')]
        self.assertEqual(measured['live_targets_end'], 11)
        self.assertEqual(measured['live_target_bytes_end'], 1100)
        self.assertEqual(measured['pool_bytes_end'], 500)

    def test_cost_is_split_per_phase_and_normalised_per_second(self):
        events = [
            {'unix_ms': 0, 'fields': {'action': 'regression.window', 'case': 'soak',
                                      'window': 'steady', 'edge': 'begin'}},
            {'unix_ms': 900, 'fields': {'action': 'gpu_span', 'span': 'RenderTick',
                                        'total_us': '2000'}},
            {'unix_ms': 900, 'fields': {'action': 'gpu_span',
                                        'span': 'CompositeStack 1024 rgba8',
                                        'total_us': '1500'}},
            {'unix_ms': 1000, 'fields': {
                'action': 'heartbeat', 'frames': '20', 'frame_us': '40000',
                'render_us': '30000', 'signals_us': '4000', 'plugin_over_8ms': '2',
                'plugin_over_16ms': '1', 'hitches_50ms': '1', 'interval_max_us': '70000'}},
            {'unix_ms': 2000, 'fields': {
                'action': 'heartbeat', 'frames': '20', 'frame_us': '40000',
                'render_us': '30000', 'signals_us': '4000', 'plugin_over_8ms': '1',
                'interval_max_us': '40000'}},
            {'unix_ms': 2000, 'fields': {'action': 'regression.window', 'case': 'soak',
                                         'window': 'steady', 'edge': 'end'}}]
        measured = budgets.measurements(events)[('soak', 'steady')]
        self.assertEqual(measured['plugin_frame_us_mean'], 2000.0)
        self.assertEqual(measured['plugin_ms_per_s'], 40.0)
        self.assertEqual(measured['plugin_share_pct'], 4.0)
        self.assertEqual(measured['heartbeats'], 2)
        self.assertEqual(measured['render_us_mean'], 1500.0)
        self.assertEqual(measured['signals_us_mean'], 200.0)
        self.assertEqual(measured['gpu_us_mean'], 50.0)
        self.assertEqual(measured['plugin_over_8ms'], 3)
        self.assertEqual(measured['plugin_over_16ms'], 1)
        self.assertEqual(measured['hitches_50ms'], 1)
        self.assertEqual(measured['interval_max_us'], 70000)
        table = budgets.format_measurements({('soak', 'steady'): measured})
        self.assertIn('cost per frame and second:', table)
        self.assertIn('spikes and hitches:', table)

    def test_a_stall_names_the_last_step_window_and_heartbeat(self):
        results = [
            {'kind': 'step', 'case': 'soak-fight', 'step': 9, 'action': 'await-crowd-hurt',
             'outcome': 'PASS'},
            {'kind': 'window', 'case': 'soak-fight', 'window': 'fight', 'edge': 'begin'}]
        events = [{'unix_ms': 1000, 'fields': {'action': 'heartbeat'}},
                  {'unix_ms': 1500, 'fields': {'action': 'post'}}]
        lines = budgets.stall_lines(events, results, 11000)
        self.assertIn('last finished step: soak-fight 9 await-crowd-hurt PASS; the run '
                      'stopped in the step after it', lines)
        self.assertIn('open window: soak-fight/fight', lines)
        self.assertIn('last heartbeat: 10.0 s before this report', lines)
        self.assertIn('last trace actions: post', lines)

    def test_missing_data_fails_instead_of_passing(self):
        def edge(time_ms, case, window, kind):
            return {'unix_ms': time_ms, 'fields': {'action': 'regression.window',
                                                   'case': case, 'window': window,
                                                   'edge': kind}}

        def beat(time_ms, frames):
            return {'unix_ms': time_ms, 'fields': {'action': 'heartbeat',
                                                   'frames': str(frames),
                                                   'frame_us': '100', 'tick_us': '0'}}
        events = [edge(0, 'soak', 'baseline', 'begin'), beat(500, 0),
                  edge(1000, 'soak', 'baseline', 'end'),
                  edge(1000, 'soak', 'steady', 'begin'), beat(1500, 30),
                  edge(2000, 'soak', 'steady', 'end'),
                  edge(2000, 'soak', 'settle', 'begin'), edge(2100, 'soak', 'settle', 'end'),
                  edge(3000, 'soak', 'steady', 'begin'), beat(3500, 30),
                  edge(4000, 'soak', 'steady', 'end'),
                  edge(4000, 'soak', 'recovery', 'begin')]
        measured = budgets.measurements(events)
        limits = budgets.parse_budgets({'steady': {'fps_ratio_min': 0.5},
                                        'settle': {'frame_max_us_max': 1000}})
        self.assertEqual(budgets.budget_failures(measured, limits), [
            'soak/settle: no heartbeat fell inside the window, so its budgets cannot be '
            'checked',
            'soak/steady: fps has no baseline above zero, so its ratio cannot be checked'])
        self.assertEqual(budgets.unmeasurable_windows(events), [
            'soak/steady: the window appears twice, so its measures are ambiguous',
            'soak/recovery: the window never closed'])

    def test_gpu_ticks_count_timed_and_untimed(self):
        events = [
            {'unix_ms': 0, 'fields': {'action': 'regression.window', 'case': 'soak',
                                      'window': 'steady', 'edge': 'begin'}},
            {'unix_ms': 500, 'fields': {'action': 'gpu_ticks', 'timed': '20',
                                        'dropped': '3', 'discarded': '2'}},
            {'unix_ms': 1000, 'fields': {'action': 'regression.window', 'case': 'soak',
                                         'window': 'steady', 'edge': 'end'}}]
        measured = budgets.measurements(events)[('soak', 'steady')]
        self.assertEqual(measured['gpu_ticks_timed'], 20)
        self.assertEqual(measured['gpu_ticks_untimed'], 5)

    def test_sync_time_counts_in_the_plugin_share(self):
        events = [
            {'unix_ms': 0, 'fields': {'action': 'regression.window', 'case': 'soak',
                                      'window': 'steady', 'edge': 'begin'}},
            {'unix_ms': 1000, 'fields': {'action': 'heartbeat', 'frames': '10',
                                         'frame_us': '10000', 'sync_us': '10000'}},
            {'unix_ms': 1000, 'fields': {'action': 'regression.window', 'case': 'soak',
                                         'window': 'steady', 'edge': 'end'}}]
        measured = budgets.measurements(events)[('soak', 'steady')]
        self.assertEqual(measured['plugin_frame_us_mean'], 1000.0)
        self.assertEqual(measured['plugin_share_pct'], 2.0)

    def test_resolution_levels_count_at_the_window_end(self):
        def at(time_ms, fields):
            return {'unix_ms': time_ms, 'fields': fields}
        events = [
            at(0, {'action': 'regression.window', 'case': 'soak-distance',
                   'window': 'steady', 'edge': 'begin'}),
            at(200, {'action': 'resolution_levels', 'full': '12', 'half': '0',
                     'quarter': '0', 'eighth': '0'}),
            at(300, {'action': 'resolution_level', 'from': 'full', 'to': 'half'}),
            at(400, {'action': 'resolution_level', 'from': 'full', 'to': 'eighth'}),
            at(1000, {'action': 'heartbeat', 'frames': '10',
                      'screen_fraction_median': '0.214'}),
            at(1000, {'action': 'resolution_levels', 'full': '3', 'half': '3',
                      'quarter': '3', 'eighth': '3'}),
            at(1000, {'action': 'regression.window', 'case': 'soak-distance',
                      'window': 'steady', 'edge': 'end'})]
        measured = budgets.measurements(events)[('soak-distance', 'steady')]
        self.assertEqual([measured[f'{level}_end'] for level in budgets.LEVELS], [3, 3, 3, 3])
        self.assertEqual(measured['level_changes'], 2)
        self.assertEqual(measured['screen_fraction_median_end'], 0.214)

    def test_heartbeats_without_the_cost_fields_are_reported(self):
        events = [
            {'unix_ms': 0, 'fields': {'action': 'regression.window', 'case': 'soak',
                                      'window': 'steady', 'edge': 'begin'}},
            {'unix_ms': 500, 'fields': {'action': 'heartbeat', 'frames': '10'}},
            {'unix_ms': 1000, 'fields': {'action': 'regression.window', 'case': 'soak',
                                         'window': 'steady', 'edge': 'end'}}]
        self.assertEqual(budgets.incomplete_heartbeats(events),
                         ['soak/steady: heartbeats lack frame_us, tick_us, so its budgets '
                          'cannot be checked'])

    def test_recipe_sets_list_each_distinct_set_once(self):
        shown = {'action': 'regression.recipes', 'arcane-circuit': '00AA',
               'winterglass': 'missing'}
        events = [{'unix_ms': 1, 'fields': dict(shown)},
                  {'unix_ms': 2, 'fields': {'action': 'heartbeat'}},
                  {'unix_ms': 3, 'fields': dict(shown)}]
        self.assertEqual(budgets.recipe_sets(events),
                         ['arcane-circuit=00AA winterglass=missing'])

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
