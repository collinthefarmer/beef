"""Measure the soak windows in a trace and check them against the budgets file."""
from dataclasses import dataclass
import json
from pathlib import Path
import re

@dataclass(frozen=True)
class Window:
    name: str
    start_ms: int
    end_ms: int


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


def windows(events: list[dict]) -> list[Window]:
    found, opened = [], {}
    for event in events:
        fields = event['fields']
        if fields.get('action') != 'regression.window':
            continue
        name, time_ms = str(fields.get('window', '')), event.get('unix_ms', 0)
        if fields.get('edge') == 'begin':
            opened[name] = time_ms
        elif fields.get('edge') == 'end' and name in opened:
            found.append(Window(name, opened.pop(name), time_ms))
    return found


def number(fields: dict, name: str) -> int:
    try:
        return int(fields.get(name, 0))
    except (TypeError, ValueError):
        return 0


def measure(events: list[dict], window: Window) -> dict[str, float]:
    beats = [event['fields'] for event in events
             if event['fields'].get('action') == 'heartbeat'
             and window.start_ms < event.get('unix_ms', 0) <= window.end_ms]
    frames = sum(number(beat, 'frames') for beat in beats)
    seconds = max((window.end_ms - window.start_ms) / 1000, 0.001)
    last = beats[-1] if beats else {}
    peak = lambda name: max((number(beat, name) for beat in beats), default=0)
    return {
        'seconds': round(seconds, 1),
        'fps': round(frames / seconds, 1),
        'plugin_frame_us_mean': round(sum(number(b, 'frame_us') for b in beats) / max(frames, 1), 1),
        'frame_max_us': peak('frame_max_us'),
        'tick_max_us': peak('tick_max_us'),
        'refresh_max_us': peak('refresh_max_us'),
        'readback_max_us': peak('readback_max_us'),
        'targets_end': number(last, 'targets'),
        'target_bytes_end': number(last, 'target_bytes'),
        'target_bytes_peak': peak('target_bytes_peak'),
    }


def measurements(events: list[dict]) -> dict[str, dict[str, float]]:
    return {window.name: measure(events, window) for window in windows(events)}


MEASURES = ('seconds', 'fps', 'plugin_frame_us_mean', 'frame_max_us', 'tick_max_us',
            'refresh_max_us', 'readback_max_us', 'targets_end', 'target_bytes_end',
            'target_bytes_peak')
LIMIT_KINDS = ('_over_baseline_max', '_ratio_min', '_max')


@dataclass(frozen=True)
class Limit:
    window: str
    measure: str
    kind: str
    bound: float


def parse_budgets(raw: object) -> list[Limit] | str:
    if not isinstance(raw, dict):
        return 'the budgets file must hold an object of windows'
    limits = []
    for window, entries in raw.items():
        if not isinstance(entries, dict):
            return f'budget window {window} must hold an object of limits'
        for key, bound in entries.items():
            kind = next((kind for kind in LIMIT_KINDS if key.endswith(kind)), None)
            measure = key.removesuffix(kind) if kind else ''
            if measure not in MEASURES:
                return f'budget {window}.{key} names no known measure and limit'
            if isinstance(bound, bool) or not isinstance(bound, (int, float)):
                return f'budget {window}.{key} must be a number'
            limits.append(Limit(window, measure, kind, bound))
    return limits


def budget_failures(measured: dict[str, dict[str, float]], limits: list[Limit]) -> list[str]:
    failures = []
    baseline = measured.get('baseline', {})
    for limit in limits:
        values = measured.get(limit.window)
        if values is None:
            continue
        value, base = values[limit.measure], baseline.get(limit.measure, 0)
        if limit.kind == '_ratio_min':
            ratio = value / max(base, 0.001)
            if ratio < limit.bound:
                failures.append(f'{limit.window}: {limit.measure} {value} is {ratio:.2f} of '
                                f'baseline, under {limit.bound}')
        elif limit.kind == '_over_baseline_max':
            if value - base > limit.bound:
                failures.append(f'{limit.window}: {limit.measure} grew by {value - base} over '
                                f'baseline, over {limit.bound}')
        elif value > limit.bound:
            failures.append(f'{limit.window}: {limit.measure} {value} over {limit.bound}')
    return failures


def format_measurements(measured: dict[str, dict[str, float]]) -> str:
    if not measured:
        return ''
    names = list(next(iter(measured.values())).keys())
    rows = [['window'] + names] + [[window] + [str(values[name]) for name in names]
                                   for window, values in measured.items()]
    widths = [max(len(row[column]) for row in rows) for column in range(len(rows[0]))]
    return '\n'.join('  '.join(cell.ljust(width) for cell, width in zip(row, widths)).rstrip()
                     for row in rows)


def check_budgets(trace: Path, budgets: Path) -> tuple[str, list[str]]:
    if not trace.is_file():
        return '', [f'trace {trace} is missing']
    measured = measurements(trace_events(trace))
    if not measured:
        return '', []
    try:
        limits = parse_budgets(json.loads(budgets.read_text(encoding='utf-8')))
    except (OSError, ValueError) as error:
        limits = f'the budgets file {budgets} is unreadable: {error}'
    if isinstance(limits, str):
        return format_measurements(measured), [limits]
    return format_measurements(measured), budget_failures(measured, limits)
