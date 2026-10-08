"""Measure the soak windows in a trace and check them against the budgets file."""
from dataclasses import dataclass
from fnmatch import fnmatchcase
import json
from pathlib import Path
import re

@dataclass(frozen=True)
class Window:
    case: str
    name: str
    start_ms: int
    end_ms: int


WindowKey = tuple[str, str]


def trace_events(trace: Path) -> list[dict]:
    match = re.match(r'^(.*?)(?:-\d{1,4})?\.jsonl$', trace.name)
    stem = match.group(1) if match else trace.stem
    events = []
    for segment in sorted(trace.parent.glob(f'{stem}*.jsonl')):
        for line in segment.read_text(encoding='utf-8', errors='replace').splitlines():
            try:
                event = json.loads(line)
            except ValueError:
                continue
            if isinstance(event, dict) and isinstance(event.get('fields'), dict):
                events.append(event)
    return sorted(events, key=lambda event: event.get('unix_ms', 0))


def paired_windows(events: list[dict]) -> tuple[list[Window], list[WindowKey]]:
    found, opened = [], {}
    for event in events:
        fields = event['fields']
        if fields.get('action') != 'regression.window':
            continue
        key = (str(fields.get('case', '')), str(fields.get('window', '')))
        time_ms = event.get('unix_ms', 0)
        if fields.get('edge') == 'begin':
            opened[key] = time_ms
        elif fields.get('edge') == 'end' and key in opened:
            found.append(Window(*key, opened.pop(key), time_ms))
    return sorted(found, key=lambda window: window.start_ms), list(opened)


def windows(events: list[dict]) -> list[Window]:
    return paired_windows(events)[0]


def unmeasurable_windows(events: list[dict]) -> list[str]:
    closed, unclosed = paired_windows(events)
    seen, problems = set(), []
    for window in closed:
        key = (window.case, window.name)
        if key in seen:
            problems.append(f'{label(key)}: the window appears twice, so its measures '
                            f'are ambiguous')
        seen.add(key)
    problems += [f'{label(key)}: the window never closed' for key in unclosed]
    return problems


def number(fields: dict, name: str) -> int:
    try:
        return int(fields.get(name, 0))
    except (TypeError, ValueError):
        return 0


def fraction(fields: dict, name: str) -> float:
    try:
        return float(fields.get(name, 0))
    except (TypeError, ValueError):
        return 0.0


LEVELS = ('full', 'half', 'quarter', 'eighth')
PHASES = ('signals', 'render', 'render_inputs', 'render_slots', 'render_shell',
          'render_evaluate', 'cull', 'lights',
          'previews', 'sweep', 'snapshot', 'sync')


def inside(events: list[dict], window: Window, action: str) -> list[dict]:
    return [event['fields'] for event in events
            if event['fields'].get('action') == action
            and window.start_ms < event.get('unix_ms', 0) <= window.end_ms]


def measure(events: list[dict], window: Window) -> dict[str, float]:
    beats = inside(events, window, 'heartbeat')
    frames = sum(number(beat, 'frames') for beat in beats)
    seconds = max((window.end_ms - window.start_ms) / 1000, 0.001)
    last = beats[-1] if beats else {}
    peak = lambda name: max((number(beat, name) for beat in beats), default=0)
    total = lambda name: sum(number(beat, name) for beat in beats)
    per_frame = lambda micros: round(micros / max(frames, 1), 1)
    live = lambda total, held: number(last, total) - sum(
        number(last, f'{holder}_{held}') for holder in ('pool', 'scratch', 'resident'))
    gpu_us = sum(number(span, 'total_us') for span in inside(events, window, 'gpu_span')
                 if span.get('span') == 'RenderTick')
    ticks = inside(events, window, 'gpu_ticks')
    plugin_us = total('frame_us') + total('sync_us')
    levels = inside(events, window, 'resolution_levels')
    last_levels = levels[-1] if levels else {}
    return {
        'heartbeats': len(beats),
        'seconds': round(seconds, 1),
        'fps': round(frames / seconds, 1),
        'plugin_frame_us_mean': per_frame(total('frame_us')),
        'plugin_ms_per_s': round(plugin_us / seconds / 1000, 1),
        'plugin_share_pct': round(plugin_us / (seconds * 1e6) * 100, 1),
        **{f'{phase}_us_mean': per_frame(total(f'{phase}_us')) for phase in PHASES},
        'gpu_us_mean': per_frame(gpu_us),
        'gpu_ticks_timed': sum(number(tick, 'timed') for tick in ticks),
        'gpu_ticks_untimed': sum(number(tick, 'dropped') + number(tick, 'discarded')
                                 for tick in ticks),
        'out_of_view_pct': round(total('actors_out_of_view') * 100
                                 / max(total('actors_in_view') + total('actors_out_of_view'), 1),
                                 1),
        'frame_max_us': peak('frame_max_us'),
        'tick_max_us': peak('tick_max_us'),
        'sync_max_us': peak('sync_max_us'),
        'readback_max_us': peak('readback_max_us'),
        'plugin_over_8ms': total('plugin_over_8ms'),
        'plugin_over_16ms': total('plugin_over_16ms'),
        'plugin_over_33ms': total('plugin_over_33ms'),
        'interval_max_us': peak('interval_max_us'),
        'hitches_50ms': total('hitches_50ms'),
        'hitches_100ms': total('hitches_100ms'),
        'targets_end': number(last, 'targets'),
        'target_bytes_end': number(last, 'target_bytes'),
        'target_bytes_peak': peak('target_bytes_peak'),
        'pool_targets_end': number(last, 'pool_targets'),
        'pool_bytes_end': number(last, 'pool_bytes'),
        'scratch_targets_end': number(last, 'scratch_targets'),
        'scratch_bytes_end': number(last, 'scratch_bytes'),
        'live_targets_end': live('targets', 'targets'),
        'live_target_bytes_end': live('target_bytes', 'bytes'),
        **{f'{level}_end': number(last_levels, level) for level in LEVELS},
        'level_changes': len(inside(events, window, 'resolution_level')),
        'screen_fraction_median_end': fraction(last, 'screen_fraction_median'),
    }


def measurements(events: list[dict]) -> dict[WindowKey, dict[str, float]]:
    return {(window.case, window.name): measure(events, window)
            for window in windows(events)}


def label(key: WindowKey) -> str:
    case, name = key
    return f'{case}/{name}' if case else name


COST_MEASURES = ('seconds', 'fps', 'plugin_frame_us_mean', 'plugin_ms_per_s',
                 'plugin_share_pct', *(f'{phase}_us_mean' for phase in PHASES), 'gpu_us_mean',
                 'gpu_ticks_timed', 'gpu_ticks_untimed', 'out_of_view_pct')
SPIKE_MEASURES = ('frame_max_us', 'tick_max_us', 'sync_max_us', 'readback_max_us',
                  'plugin_over_8ms', 'plugin_over_16ms', 'plugin_over_33ms', 'interval_max_us',
                  'hitches_50ms', 'hitches_100ms')
MEMORY_MEASURES = ('targets_end', 'target_bytes_end', 'target_bytes_peak', 'pool_targets_end',
                   'pool_bytes_end', 'scratch_targets_end', 'scratch_bytes_end',
                   'live_targets_end', 'live_target_bytes_end')
RESOLUTION_MEASURES = (*(f'{level}_end' for level in LEVELS), 'level_changes',
                       'screen_fraction_median_end')
MEASURES = COST_MEASURES + SPIKE_MEASURES + MEMORY_MEASURES + RESOLUTION_MEASURES
TABLES = (('cost per frame and second', COST_MEASURES),
          ('spikes and hitches', SPIKE_MEASURES),
          ('render targets', MEMORY_MEASURES),
          ('screen resolution', RESOLUTION_MEASURES))
LIMIT_KINDS = ('_over_baseline_max', '_growth_max', '_ratio_min', '_max')


@dataclass(frozen=True)
class Limit:
    pattern: str
    measure: str
    kind: str
    bound: float


def parse_budgets(raw: object) -> list[Limit] | str:
    if not isinstance(raw, dict):
        return 'the budgets file must hold an object of windows'
    limits = []
    for pattern, entries in raw.items():
        if not isinstance(entries, dict):
            return f'budget window {pattern} must hold an object of limits'
        for key, bound in entries.items():
            kind = next((kind for kind in LIMIT_KINDS if key.endswith(kind)), None)
            measure = key.removesuffix(kind) if kind else ''
            if measure not in MEASURES:
                return f'budget {pattern}.{key} names no known measure and limit'
            if isinstance(bound, bool) or not isinstance(bound, (int, float)):
                return f'budget {pattern}.{key} must be a number'
            limits.append(Limit(pattern, measure, kind, bound))
    return limits


def growth_failure(case: str, matching: list[WindowKey],
                   measured: dict[WindowKey, dict[str, float]], limit: Limit) -> str | None:
    if len(matching) < 2:
        return None
    first, last = matching[0], matching[-1]
    growth = measured[last][limit.measure] - measured[first][limit.measure]
    if growth <= limit.bound:
        return None
    return (f'{label((case, limit.pattern))}: {limit.measure} grew by {growth} from '
            f'{first[1]} to {last[1]}, over {limit.bound}')


def window_failure(key: WindowKey, values: dict[str, float], baseline: dict[str, float],
                   limit: Limit) -> str | None:
    value, base = values[limit.measure], baseline.get(limit.measure, 0)
    if limit.kind == '_ratio_min':
        if base <= 0:
            return (f'{label(key)}: {limit.measure} has no baseline above zero, so its '
                    f'ratio cannot be checked')
        ratio = value / base
        if ratio < limit.bound:
            return (f'{label(key)}: {limit.measure} {value} is {ratio:.2f} of '
                    f'baseline, under {limit.bound}')
    elif limit.kind == '_over_baseline_max':
        if value - base > limit.bound:
            return (f'{label(key)}: {limit.measure} grew by {value - base} over '
                    f'baseline, over {limit.bound}')
    elif value > limit.bound:
        return f'{label(key)}: {limit.measure} {value} over {limit.bound}'
    return None


REQUIRED_HEARTBEAT_FIELDS = ('frames', 'frame_us', 'tick_us')


def incomplete_heartbeats(events: list[dict]) -> list[str]:
    found = []
    for window in windows(events):
        beats = inside(events, window, 'heartbeat')
        missing = sorted({name for beat in beats for name in REQUIRED_HEARTBEAT_FIELDS
                          if name not in beat})
        if missing:
            found.append(f"{label((window.case, window.name))}: heartbeats lack "
                         f"{', '.join(missing)}, so its budgets cannot be checked")
    return found


def budget_failures(measured: dict[WindowKey, dict[str, float]],
                    limits: list[Limit]) -> list[str]:
    failures = []
    cases = list(dict.fromkeys(case for case, _ in measured))
    silent = [key for key, values in measured.items() if values['heartbeats'] == 0
              and any(fnmatchcase(key[1], limit.pattern) for limit in limits)]
    failures += [f'{label(key)}: no heartbeat fell inside the window, so its budgets '
                 f'cannot be checked' for key in silent]
    for case in cases:
        baseline = measured.get((case, 'baseline'), {})
        for limit in limits:
            matching = [key for key in measured
                        if key[0] == case and fnmatchcase(key[1], limit.pattern)
                        and key not in silent]
            if limit.kind == '_growth_max':
                found = [growth_failure(case, matching, measured, limit)]
            else:
                found = [window_failure(key, measured[key], baseline, limit)
                         for key in matching]
            failures.extend(failure for failure in found if failure)
    return failures


def format_table(measured: dict[WindowKey, dict[str, float]], names: tuple[str, ...]) -> str:
    rows = [['window', *names]] + [[label(key)] + [str(values[name]) for name in names]
                                   for key, values in measured.items()]
    widths = [max(len(row[column]) for row in rows) for column in range(len(rows[0]))]
    return '\n'.join('  '.join(cell.ljust(width) for cell, width in zip(row, widths)).rstrip()
                     for row in rows)


def format_measurements(measured: dict[WindowKey, dict[str, float]]) -> str:
    if not measured:
        return ''
    return '\n\n'.join(f'{title}:\n{format_table(measured, names)}'
                       for title, names in TABLES)


def stall_lines(events: list[dict], results: list[dict], now_ms: int) -> list[str]:
    steps = [line for line in results if line.get('kind') == 'step']
    opened = {}
    for line in results:
        if line.get('kind') == 'window':
            key = (line.get('case'), line.get('window'))
            if line.get('edge') == 'begin':
                opened[key] = True
            else:
                opened.pop(key, None)
    beats = [event for event in events if event['fields'].get('action') == 'heartbeat']
    lines = []
    current = steps[-1].get('case') if steps else None
    if steps:
        step = steps[-1]
        lines.append(f"last finished step: {step.get('case')} {step.get('step')} "
                     f"{step.get('action')} {step.get('outcome')}; the run stopped in the "
                     f"step after it")
    for case, window in opened:
        if case == current:
            lines.append(f'open window: {case}/{window}')
    if beats:
        last = beats[-1].get('unix_ms', 0)
        lines.append(f'last heartbeat: {(now_ms - last) / 1000:.1f} s before this report')
    else:
        lines.append('no heartbeat in the trace')
    if events:
        lines.append(f"last trace event: {(now_ms - events[-1].get('unix_ms', 0)) / 1000:.1f} s "
                     f"before this report")
        recent = [event['fields'].get('action', '?') for event in events[-12:]
                  if event['fields'].get('action') not in ('heartbeat', 'gpu_span',
                                                            'step_kinds', 'gpu_ticks')]
        if recent:
            lines.append('last trace actions: ' + ', '.join(str(action) for action in recent))
    return lines


def recipe_sets(events: list[dict]) -> list[str]:
    sets = []
    for event in events:
        fields = event['fields']
        if fields.get('action') != 'regression.recipes':
            continue
        line = ' '.join(f'{name}={value}' for name, value in fields.items()
                        if name != 'action')
        if line not in sets:
            sets.append(line)
    return sets


def check_budgets(trace: Path, budgets: Path) -> tuple[str, list[str]]:
    if not trace.is_file():
        return '', [f'trace {trace} is missing']
    events = trace_events(trace)
    measured = measurements(events)
    table = '\n'.join([f'recipe set: {line}' for line in recipe_sets(events)]
                      + ([format_measurements(measured)] if measured else []))
    if not measured:
        return table, []
    try:
        limits = parse_budgets(json.loads(budgets.read_text(encoding='utf-8')))
    except (OSError, ValueError) as error:
        limits = f'the budgets file {budgets} is unreadable: {error}'
    if isinstance(limits, str):
        return table, [limits]
    return table, (incomplete_heartbeats(events) + unmeasurable_windows(events)
                   + budget_failures(measured, limits))
