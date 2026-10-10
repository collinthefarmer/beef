"""Run in-game regression cases unattended: launch Skyrim through MO2, wait, report."""
import argparse
from dataclasses import dataclass
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
BUDGETS_SPEC = importlib.util.spec_from_file_location(
    'regression_budgets', Path(__file__).resolve().parent / 'regression_budgets.py')
budgets = importlib.util.module_from_spec(BUDGETS_SPEC)
BUDGETS_SPEC.loader.exec_module(budgets)
NAME = 'BetterEnchantmentEffects'
RUN_FILE = f'{NAME}-regression-run.json'
GAME = 'SkyrimSE.exe'
SETTINGS = {
    'MO2_EXE': 'the WSL path of ModOrganizer.exe',
    'MO2_PROFILE': 'the MO2 profile to launch',
    'MO2_LAUNCH': 'the MO2 executable title that starts SKSE',
    'SKSE_LOG_DIR': 'the WSL path of My Games/Skyrim Special Edition/SKSE',
}
OPTIONAL_SETTINGS = {
    'MO2_MODS_DIR': 'the WSL path of the MO2 mods folder',
}
SAVE_FILE_SUFFIXES = ('.ess', '.skse')
RUN_FILE_LIFETIME_SECONDS = 600
START_GRACE_SECONDS = 300
REFUSAL_GRACE_SECONDS = 30
QUIT_GRACE_SECONDS = 60
RELAUNCH_SECONDS = 60
BETWEEN_LAUNCHES_SECONDS = 15
MIN_MEASURED_SECONDS = 10
MAX_MEASURED_SECONDS = 3600
MAX_RENDER_TARGET_BUDGET_MIB = 65536
QUICK_EDGE_SECONDS = 10
BUDGETS_FILE = ROOT / 'tests/regression/budgets.json'
SEED_SAVES = ROOT / 'tests/regression/saves'
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
        if separator and key.strip() in SETTINGS | OPTIONAL_SETTINGS:
            value = value.strip()
            if len(value) >= 2 and value[0] == value[-1] == '"':
                value = value[1:-1]
            values[key.strip()] = value
    return values


def read_settings(environ: dict[str, str], local: dict[str, str],
                  profile: str | None = None) -> Settings | str:
    values = {key: environ.get(key) or local.get(key, '') for key in SETTINGS}
    if profile:
        values['MO2_PROFILE'] = profile
    missing = [f'{key} ({meaning})' for key, meaning in SETTINGS.items() if not values[key]]
    if missing:
        return 'set these in the environment or local.env: ' + '; '.join(missing)
    return Settings(Path(values['MO2_EXE']), values['MO2_PROFILE'], values['MO2_LAUNCH'],
                    Path(values['SKSE_LOG_DIR']))


def run_file(run: str, save: str, suite: list[str], now: float,
             verify_render_plans: bool = False, measured_seconds: int | None = None,
             screen_resolution: bool | None = None,
             render_target_budget_mib: int | None = None) -> dict:
    request = {'format': 1, 'run': run, 'save': save, 'suite': suite,
               'notAfter': int(now) + RUN_FILE_LIFETIME_SECONDS}
    if verify_render_plans:
        request['verifyRenderPlans'] = True
    if measured_seconds is not None:
        request['measuredSeconds'] = measured_seconds
    if screen_resolution is not None:
        request['screenResolution'] = screen_resolution
    if render_target_budget_mib is not None:
        request['renderTargetBudgetMiB'] = render_target_budget_mib
    return request


def quick_seconds(text: str) -> int:
    seconds = int(text)
    if not MIN_MEASURED_SECONDS <= seconds <= MAX_MEASURED_SECONDS:
        raise argparse.ArgumentTypeError(
            f'--quick is {MIN_MEASURED_SECONDS}..{MAX_MEASURED_SECONDS} seconds')
    return seconds


def budget_mib(text: str) -> int:
    mebibytes = int(text)
    if not 0 <= mebibytes <= MAX_RENDER_TARGET_BUDGET_MIB:
        raise argparse.ArgumentTypeError(
            f'--render-target-budget is 0..{MAX_RENDER_TARGET_BUDGET_MIB} MiB')
    return mebibytes


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


def game_running(tasklist: str) -> bool:
    listing = subprocess.run([tasklist, '/FI', f'IMAGENAME eq {GAME}', '/NH'],
                             capture_output=True, text=True, check=False).stdout
    return GAME.lower() in listing.lower()


def missing_save(mods_dir: str, profile: str, save: str) -> Path | None:
    saves = profile_saves(mods_dir, profile)
    if not mods_dir or not saves.is_dir():
        return None
    path = saves / f'{save}.ess'
    return None if path.is_file() else path


def profile_saves(mods_dir: str, profile: str) -> Path:
    return Path(mods_dir).parent / 'profiles' / profile / 'saves'


@dataclass(frozen=True)
class SaveCopy:
    source: Path
    target: Path
    backup: Path | None


def seed_copies(seed: Path, target: Path, save: str, stamp: str) -> list[SaveCopy] | str:
    if not (seed / f'{save}.ess').is_file():
        return f'no seed save at {seed / save}.ess'
    if not target.is_dir():
        return f'the profile has no saves folder of its own: {target}'
    copies = []
    for suffix in SAVE_FILE_SUFFIXES:
        source = seed / f'{save}{suffix}'
        destination = target / f'{save}{suffix}'
        if not source.is_file():
            return f'the seed save has no {suffix} file: {source}'
        if destination.is_file() and destination.read_bytes() == source.read_bytes():
            continue
        backup = (target / f'{save}{suffix}.before-seed-{stamp}'
                  if destination.is_file() else None)
        copies.append(SaveCopy(source, destination, backup))
    return copies


def apply_copies(copies: list[SaveCopy]) -> None:
    for copy in copies:
        if copy.backup is not None:
            shutil.copy2(copy.target, copy.backup)
        shutil.copy2(copy.source, copy.target)
        kept = f' (kept the old one as {copy.backup.name})' if copy.backup else ''
        print(f'copied the seed save to {copy.target}{kept}')


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
    parser.add_argument('--profile', help='the MO2 profile to launch, in place of MO2_PROFILE')
    parser.add_argument('--timeout', type=float, default=900, help='seconds to wait for the end line')
    parser.add_argument('--verify-render-plans', action='store_true',
                        help='rebuild every reused render plan and log any difference')
    parser.add_argument('--quick', type=quick_seconds, metavar='SECONDS',
                        help='hold each measured window (steady, fight, orbit) this long and '
                             f'baseline and recovery at most {QUICK_EDGE_SECONDS} s; for '
                             'comparisons only, the full soak is the checkpoint gate')
    parser.add_argument('--screen-resolution', choices=('on', 'off'),
                        help='turn ScreenResolution on or off for this run in place of the '
                             'INI value; the start line records the value used')
    parser.add_argument('--render-target-budget', type=budget_mib, metavar='MIB',
                        help='use this RenderTargetBudgetMiB for this run in place of the '
                             'INI value; 0 sets no cap; the start line records the value used')
    parser.add_argument('--manual', action='store_true',
                        help='write the run file and wait; start the game yourself')
    args = parser.parse_args()
    local_path = ROOT / 'local.env'
    local = local_env(local_path.read_text(encoding='utf-8')) if local_path.is_file() else {}
    settings = read_settings(dict(os.environ), local, args.profile)
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
    segments = launches(args.cases)
    if isinstance(segments, str):
        print(segments, file=sys.stderr)
        return 2
    mods_dir = os.environ.get('MO2_MODS_DIR') or local.get('MO2_MODS_DIR', '')
    stamp = time.strftime('%Y%m%dT%H%M%S')
    if mods_dir and (SEED_SAVES / f'{args.save}.ess').is_file():
        copies = seed_copies(SEED_SAVES, profile_saves(mods_dir, settings.profile), args.save,
                             stamp)
        if isinstance(copies, str):
            print(copies, file=sys.stderr)
            return 2
        apply_copies(copies)
    if (absent := missing_save(mods_dir, settings.profile, args.save)) is not None:
        print(f'no save at {absent}; create it in game with the console command '
              f'save {args.save}', file=sys.stderr)
        return 2
    passed = []
    for index, cases in enumerate(segments):
        if index > 0:
            time.sleep(BETWEEN_LAUNCHES_SECONDS)
        passed.append(launch(settings, f'{stamp}-{index + 1}', cases, args.save, args.timeout,
                             args.manual, tasklist, args.verify_render_plans, args.quick,
                             screen_resolution_of(args.screen_resolution),
                             args.render_target_budget))
    if len(passed) > 1:
        print(f'launches passed: {sum(passed)} of {len(passed)}')
    return 0 if all(passed) else 1


def screen_resolution_of(choice: str | None) -> bool | None:
    return None if choice is None else choice == 'on'


def start_game(settings: Settings) -> None:
    subprocess.Popen([str(settings.mo2_exe), '-p', settings.profile,
                      f'moshortcut://:{settings.launch}'],
                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                     stderr=subprocess.DEVNULL, start_new_session=True)


def launch(settings: Settings, run: str, cases: list[str], save: str, timeout: float,
           manual: bool, tasklist: str, verify_render_plans: bool = False,
           measured_seconds: int | None = None, screen_resolution: bool | None = None,
           render_target_budget_mib: int | None = None) -> bool:
    request = run_file(run, save, cases, time.time(), verify_render_plans, measured_seconds,
                       screen_resolution, render_target_budget_mib)
    (settings.log_dir / RUN_FILE).write_text(json.dumps(request) + '\n', encoding='utf-8')
    print(f'run {run}: {" ".join(cases)} from save {save}')
    if measured_seconds is not None:
        print(f'quick run: measured windows {measured_seconds} s, baseline and recovery '
              f'{QUICK_EDGE_SECONDS} s; the full soak is the checkpoint gate')
    if screen_resolution is not None:
        print(f'ScreenResolution {"on" if screen_resolution else "off"} for this run')
    if render_target_budget_mib is not None:
        print(f'RenderTargetBudgetMiB {render_target_budget_mib} for this run')
    start = (lambda: print('start the game through MO2 now')) if manual \
        else (lambda: start_game(settings))
    start()
    report = wait_for_results(settings, run, timeout, tasklist, start)
    withdraw_run_file(settings.log_dir / RUN_FILE, run)
    quit_cleanly = report.outcome in ('CRASHED',) or wait_for_quit(tasklist)
    print(format_report(report))
    print(f'results: {settings.log_dir / f"{NAME}-regression-{run}.jsonl"}')
    failures = []
    if report.trace and report.outcome in ('TIMEOUT', 'CRASHED'):
        trace = settings.log_dir / report.trace
        if trace.is_file():
            for line in budgets.stall_lines(budgets.trace_events(trace), report.lines,
                                            int(time.time() * 1000)):
                print(f'stall: {line}')
    if report.trace:
        table, failures = budgets.check_budgets(settings.log_dir / report.trace, BUDGETS_FILE)
        if table:
            print(table)
    for failure in failures:
        print(f'budget: {failure}')
    if not quit_cleanly:
        print(f'{GAME} did not quit within {QUIT_GRACE_SECONDS} seconds')
    return report.outcome == 'PASS' and quit_cleanly and not failures


if __name__ == '__main__':
    sys.exit(main())
