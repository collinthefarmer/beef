"""Run in-game regression cases unattended: launch Skyrim through MO2, wait, report."""
import argparse
from dataclasses import dataclass
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
NAME = 'BetterEnchantmentEffects'
RUN_FILE = f'{NAME}-regression-run.json'
GAME = 'SkyrimSE.exe'
SETTINGS = {
    'MO2_EXE': 'the WSL path of ModOrganizer.exe',
    'MO2_PROFILE': 'the MO2 profile to launch',
    'MO2_LAUNCH': 'the MO2 executable title that starts SKSE',
    'SKSE_LOG_DIR': 'the WSL path of My Games/Skyrim Special Edition/SKSE',
}
RUN_FILE_LIFETIME_SECONDS = 600
START_GRACE_SECONDS = 300
REFUSAL_GRACE_SECONDS = 30
QUIT_GRACE_SECONDS = 60
RELAUNCH_SECONDS = 60
BETWEEN_LAUNCHES_SECONDS = 15
BUDGETS = ROOT / 'tests/regression/budgets.json'
POLL_SECONDS = 2


@dataclass(frozen=True)
class Settings:
    mo2_exe: Path
    profile: str
    launch: str
    log_dir: Path


@dataclass(frozen=True)
class Report:
    outcome: str
    reason: str
    lines: list[dict]
    trace: str


def local_env(text: str) -> dict[str, str]:
    values = {}
    for line in text.splitlines():
        key, separator, value = line.partition('=')
        if separator and key.strip() in SETTINGS | {'MO2_MODS_DIR': ''}:
            value = value.strip()
            if len(value) >= 2 and value[0] == value[-1] == '"':
                value = value[1:-1]
            values[key.strip()] = value
    return values


def read_settings(environ: dict[str, str], local: dict[str, str]) -> Settings | str:
    values = {key: environ.get(key) or local.get(key, '') for key in SETTINGS}
    missing = [f'{key} ({meaning})' for key, meaning in SETTINGS.items() if not values[key]]
    if missing:
        return 'set these in the environment or local.env: ' + '; '.join(missing)
    return Settings(Path(values['MO2_EXE']), values['MO2_PROFILE'], values['MO2_LAUNCH'],
                    Path(values['SKSE_LOG_DIR']))


def run_request(run: str, save: str, suite: list[str], now: float) -> dict:
    return {'format': 1, 'run': run, 'save': save, 'suite': suite,
            'notAfter': int(now) + RUN_FILE_LIFETIME_SECONDS}


def launches(cases: list[str]) -> list[list[str]] | str:
    segments: list[list[str]] = [[]]
    for case in cases:
        if case == '+':
            segments.append([])
        else:
            segments[-1].append(case)
    if any(not segment for segment in segments):
        return 'each launch needs at least one case; "+" separates launches'
    return segments


def result_lines(text: str) -> list[dict]:
    lines = []
    for line in text.splitlines():
        try:
            value = json.loads(line)
        except ValueError:
            continue
        if isinstance(value, dict):
            lines.append(value)
    return lines


def ended(lines: list[dict]) -> bool:
    return any(line.get('kind') == 'end' for line in lines)


def report_of(lines: list[dict], outcome: str | None = None, reason: str = '') -> Report:
    end = next((line for line in lines if line.get('kind') == 'end'), None)
    start = next((line for line in lines if line.get('kind') == 'start'), {})
    if outcome is None:
        outcome = str(end.get('outcome', 'FAIL')) if end else 'FAIL'
        reason = str(end.get('reason', '')) if end else 'the results have no end line'
    return Report(outcome, reason, lines, str(start.get('trace', '')))


def format_report(report: Report) -> str:
    rows = [('case', 'step', 'action', 'outcome', 'frames', 'reason')]
    for line in report.lines:
        if line.get('kind') == 'step':
            rows.append(tuple(str(line.get(key, '')) for key in
                              ('case', 'step', 'action', 'outcome', 'frames', 'reason')))
        elif line.get('kind') == 'case':
            rows.append((str(line.get('case', '')), '', '(case)', str(line.get('outcome', '')), '', ''))
    widths = [max(len(row[column]) for row in rows) for column in range(5)]
    text = ['  '.join(cell.ljust(width) for cell, width in zip(row, widths)) + '  ' + row[5]
            for row in rows]
    text.append(f'run: {report.outcome}' + (f' ({report.reason})' if report.reason else ''))
    if report.trace:
        text.append(f'trace: {report.trace}')
    return '\n'.join(line.rstrip() for line in text)


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


def budget_failures(measured: dict[str, dict[str, float]], budgets: dict) -> list[str]:
    failures = []
    baseline = measured.get('baseline', {})
    for window, limits in budgets.items():
        values = measured.get(window)
        if values is None:
            continue
        for limit, bound in limits.items():
            if limit == 'fps_ratio_min':
                ratio = values['fps'] / max(baseline.get('fps', 0), 0.001)
                if ratio < bound:
                    failures.append(f'{window}: fps {values["fps"]} is {ratio:.2f} of baseline, '
                                    f'under {bound}')
            elif limit.endswith('_over_baseline_max'):
                name = limit.removesuffix('_over_baseline_max')
                growth = values[name] - baseline.get(name, 0)
                if growth > bound:
                    failures.append(f'{window}: {name} grew by {growth} over baseline, '
                                    f'over {bound}')
            elif limit.endswith('_max'):
                name = limit.removesuffix('_max')
                if values[name] > bound:
                    failures.append(f'{window}: {name} {values[name]} over {bound}')
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


def game_running(tasklist: str) -> bool:
    listing = subprocess.run([tasklist, '/FI', f'IMAGENAME eq {GAME}', '/NH'],
                             capture_output=True, text=True, check=False).stdout
    return GAME.lower() in listing.lower()


def missing_save(mods_dir: str, profile: str, save: str) -> Path | None:
    saves = Path(mods_dir).parent / 'profiles' / profile / 'saves'
    if not mods_dir or not saves.is_dir():
        return None
    path = saves / f'{save}.ess'
    return None if path.is_file() else path


def wait_for_results(settings: Settings, run: str, timeout: float, tasklist: str,
                     relaunch) -> Report:
    run_file = settings.log_dir / RUN_FILE
    results = settings.log_dir / f'{NAME}-regression-{run}.jsonl'
    begin = time.monotonic()
    taken_at = None
    seen_game = False
    relaunched = False
    lines: list[dict] = []
    while True:
        elapsed = time.monotonic() - begin
        if results.exists():
            lines = result_lines(results.read_text(encoding='utf-8', errors='replace'))
        running = game_running(tasklist)
        seen_game = seen_game or running
        if ended(lines):
            return report_of(lines)
        if taken_at is None and not run_file.exists():
            taken_at = elapsed
        if taken_at is None and not seen_game and not relaunched and elapsed > RELAUNCH_SECONDS:
            relaunched = True
            relaunch()
        if seen_game and not running:
            return report_of(lines, 'CRASHED', 'the game exited without an end line')
        if taken_at is None and elapsed > START_GRACE_SECONDS:
            return report_of(lines, 'BLOCKED', 'the game did not read the run file; '
                             'is the current DLL installed and the profile right?')
        if taken_at is not None and not lines and elapsed - taken_at > REFUSAL_GRACE_SECONDS:
            return report_of(lines, 'BLOCKED', 'the plugin refused the run file; see its log')
        if elapsed > timeout:
            return report_of(lines, 'TIMEOUT', f'no end line within {int(timeout)} seconds')
        time.sleep(POLL_SECONDS)


def withdraw_run_file(path: Path, run: str) -> None:
    try:
        if json.loads(path.read_text(encoding='utf-8')).get('run') == run:
            path.unlink()
    except (OSError, ValueError, AttributeError):
        pass


def wait_for_quit(tasklist: str) -> bool:
    deadline = time.monotonic() + QUIT_GRACE_SECONDS
    while time.monotonic() < deadline:
        if not game_running(tasklist):
            return True
        time.sleep(POLL_SECONDS)
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cases', nargs='+',
                        help='case names in run order; "+" starts a new launch, '
                             'for example studio-save + studio-reload')
    parser.add_argument('--save', default='BEEFRegression', help='save name without extension')
    parser.add_argument('--timeout', type=float, default=900, help='seconds to wait for the end line')
    parser.add_argument('--manual', action='store_true',
                        help='write the run file and wait; start the game yourself')
    args = parser.parse_args()
    local_path = ROOT / 'local.env'
    local = local_env(local_path.read_text(encoding='utf-8')) if local_path.is_file() else {}
    settings = read_settings(dict(os.environ), local)
    if isinstance(settings, str):
        print(settings, file=sys.stderr)
        return 2
    tasklist = shutil.which('tasklist.exe')
    if not tasklist:
        print('tasklist.exe is not on PATH; run this from WSL with Windows interop', file=sys.stderr)
        return 2
    if not settings.log_dir.is_dir():
        print(f'SKSE_LOG_DIR is not a directory: {settings.log_dir}', file=sys.stderr)
        return 2
    if not args.manual and not settings.mo2_exe.is_file():
        print(f'MO2_EXE is not a file: {settings.mo2_exe}', file=sys.stderr)
        return 2
    if game_running(tasklist):
        print(f'{GAME} is running; quit it first', file=sys.stderr)
        return 2
    mods_dir = os.environ.get('MO2_MODS_DIR') or local.get('MO2_MODS_DIR', '')
    if (absent := missing_save(mods_dir, settings.profile, args.save)) is not None:
        print(f'no save at {absent}; create it in game with the console command '
              f'save {args.save}', file=sys.stderr)
        return 2
    segments = launches(args.cases)
    if isinstance(segments, str):
        print(segments, file=sys.stderr)
        return 2
    stamp = time.strftime('%Y%m%dT%H%M%S')
    passed = []
    for index, cases in enumerate(segments):
        if index > 0:
            time.sleep(BETWEEN_LAUNCHES_SECONDS)
        passed.append(launch(settings, f'{stamp}-{index + 1}', cases, args, tasklist))
    if len(passed) > 1:
        print(f'launches passed: {sum(passed)} of {len(passed)}')
    return 0 if all(passed) else 1


def start_game(settings: Settings) -> None:
    subprocess.Popen([str(settings.mo2_exe), '-p', settings.profile,
                      f'moshortcut://:{settings.launch}'],
                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                     stderr=subprocess.DEVNULL, start_new_session=True)


def launch(settings: Settings, run: str, cases: list[str], args: argparse.Namespace,
           tasklist: str) -> bool:
    request = run_request(run, args.save, cases, time.time())
    (settings.log_dir / RUN_FILE).write_text(json.dumps(request) + '\n', encoding='utf-8')
    print(f'run {run}: {" ".join(cases)} from save {args.save}')
    start = (lambda: print('start the game through MO2 now')) if args.manual \
        else (lambda: start_game(settings))
    start()
    report = wait_for_results(settings, run, args.timeout, tasklist, start)
    withdraw_run_file(settings.log_dir / RUN_FILE, run)
    quit_cleanly = report.outcome in ('CRASHED',) or wait_for_quit(tasklist)
    print(format_report(report))
    print(f'results: {settings.log_dir / f"{NAME}-regression-{run}.jsonl"}')
    failures = check_soak(settings, report)
    for failure in failures:
        print(f'budget: {failure}')
    if not quit_cleanly:
        print(f'{GAME} did not quit within {QUIT_GRACE_SECONDS} seconds')
    return report.outcome == 'PASS' and quit_cleanly and not failures


def check_soak(settings: Settings, report: Report) -> list[str]:
    if not report.trace:
        return []
    trace = settings.log_dir / report.trace
    if not trace.is_file():
        return [f'trace {trace} is missing']
    measured = measurements(trace_events(trace))
    if not measured:
        return []
    print(format_measurements(measured))
    budgets = json.loads(BUDGETS.read_text(encoding='utf-8')) if BUDGETS.is_file() else {}
    return budget_failures(measured, budgets)

if __name__ == '__main__':
    sys.exit(main())
