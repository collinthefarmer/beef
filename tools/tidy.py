"""Run clang-tidy over the Windows database and compare findings with the reviewed baseline."""
import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
import fnmatch
from functools import cache
from itertools import groupby
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DATABASE = ROOT / 'build/Release'
BASELINE = ROOT / 'tools/tidy-baseline.txt'
CONFIG = '.clang-tidy'
WARNING = re.compile(r'^(.*?):(\d+):\d+: warning: (.*) \[([A-Za-z][A-Za-z0-9_.,-]+)\]$')
BASELINE_ROW = re.compile(r'^(.*?):(\d+): \[([^\]]+)\](?: (.*))?$')
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)
FORCED_INCLUDE = re.compile(r'(?:/FI|-include\s*)(\S+)')
DIGITS = re.compile(r'\d+')
FINDINGS_BEYOND = 1
FAILED = 2


@dataclass(frozen=True, order=True)
class Finding:
    path: str
    line: int
    check: str
    message: str


@dataclass(frozen=True)
class BaselineRow:
    path: str
    line: int
    check: str
    message: str | None


@dataclass(frozen=True)
class Excess:
    path: str
    check: str
    count: int
    findings: tuple[Finding, ...]


Sources = dict[str, tuple[Path, ...]]


def first_party(path: Path) -> str | None:
    try:
        name = path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return None
    return name if name.startswith('src/') and not name.startswith(('src/extern/', 'src/cs/')) else None


def database_sources() -> Sources:
    sources: Sources = {}
    for entry in json.loads((DATABASE / 'compile_commands.json').read_text()):
        name = first_party(Path(entry['directory']) / entry['file'])
        if name:
            command = entry.get('command') or ' '.join(entry.get('arguments', []))
            forced = (Path(entry['directory']) / path for path in FORCED_INCLUDE.findall(command))
            sources[name] = tuple(forced)
    return sources


def run_git(*args: str) -> str:
    return subprocess.run(['git', *args], cwd=ROOT, text=True, capture_output=True, check=True).stdout


def changed_files(base: str | None) -> set[str]:
    reference = base or run_git('merge-base', 'HEAD', 'main').strip()
    listed = run_git('diff', '--name-only', reference, '--') + run_git('ls-files', '--others', '--exclude-standard')
    return {name for name in listed.splitlines() if name}


@cache
def direct_includes(path: Path) -> tuple[Path, ...]:
    try:
        text = path.read_text(errors='replace')
    except OSError:
        return ()
    candidates = (base / name for name in INCLUDE.findall(text)
                  for base in (path.parent, ROOT / 'src', ROOT / 'src/extern'))
    return tuple(candidate.resolve() for candidate in candidates if candidate.is_file())


def include_closure(start: list[Path]) -> set[Path]:
    seen: set[Path] = set()
    pending = [path.resolve() for path in start]
    while pending:
        path = pending.pop()
        if path in seen or not path.is_relative_to(ROOT):
            continue
        seen.add(path)
        pending.extend(direct_includes(path))
    return seen


def affected_sources(sources: Sources, changed: set[str]) -> list[str]:
    if CONFIG in changed:
        return sorted(sources)
    touched = {(ROOT / name).resolve() for name in changed}
    return sorted(name for name, forced in sources.items()
                  if include_closure([ROOT / name, *forced]) & touched)


def enabled_checks(analyzer: bool) -> list[str]:
    command = [os.environ.get('CLANG_TIDY', 'clang-tidy'), '--list-checks']
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, check=False)
    if result.returncode:
        raise RuntimeError(f'clang-tidy --list-checks failed:\n{result.stdout}{result.stderr}')
    names = [line.strip() for line in result.stdout.splitlines() if line.startswith((' ', '\t')) and line.strip()]
    return [name for name in names if analyzer or not name.startswith('clang-analyzer-')]


def checks_argument(patterns: list[str], analyzer: bool) -> str | None:
    if not patterns:
        return None if analyzer else '--checks=-clang-analyzer-*'
    selected = [name for name in enabled_checks(analyzer)
                if any(fnmatch.fnmatchcase(name, pattern) for pattern in patterns)]
    if not selected:
        raise ValueError('no check enabled in .clang-tidy matches ' + ', '.join(patterns)
                         + ('' if analyzer else ' (static analyzer checks need --analyzer)'))
    return '--checks=-*,' + ','.join(selected)


def parse_findings(output: str) -> list[Finding]:
    findings = []
    for match in filter(None, map(WARNING.match, output.splitlines())):
        name = first_party(DATABASE / match.group(1))
        if name:
            findings.append(Finding(name, int(match.group(2)), match.group(4), match.group(3)))
    return findings


def analyze(source: str, checks: str | None, position: str) -> list[Finding]:
    command = [os.environ.get('CLANG_TIDY', 'clang-tidy'), '-p', str(DATABASE), '--quiet',
               *([checks] if checks else []), source]
    print(f'tidy {position} {source}', file=sys.stderr, flush=True)
    result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, check=False)
    if result.returncode:
        raise RuntimeError(f'clang-tidy failed on {source}:\n{result.stdout}')
    return parse_findings(result.stdout)


def parse_baseline(text: str) -> list[BaselineRow]:
    rows = filter(None, map(BASELINE_ROW.match, text.splitlines()))
    return [BaselineRow(row.group(1), int(row.group(2)), row.group(3), row.group(4)) for row in rows]


def baseline_text(findings: list[Finding], sources: int) -> str:
    checks = Counter(finding.check for finding in findings)
    return (f'# clang-tidy baseline, {sources} sources, {len(findings)} findings\n'
            '# regenerate: python3 tools/tidy.py --update\n\n'
            + ''.join(f'# {check:<52} {count}\n'
                      for check, count in sorted(checks.items(), key=lambda item: (-item[1], item[0])))
            + '\n' + ''.join(f'{f.path}:{f.line}: [{f.check}] {f.message}\n' for f in findings))


def comparison_key(path: str, check: str, message: str) -> tuple[str, str, str]:
    return path, check, DIGITS.sub('#', message)


def excess(findings: list[Finding], rows: list[BaselineRow]) -> list[Excess]:
    exact = Counter(comparison_key(row.path, row.check, row.message) for row in rows if row.message is not None)
    loose = Counter((row.path, row.check) for row in rows if row.message is None)
    ordered = sorted(findings, key=lambda f: comparison_key(f.path, f.check, f.message))
    unexplained: dict[tuple[str, str], tuple[int, tuple[Finding, ...]]] = {}
    for key, group in groupby(ordered, key=lambda f: comparison_key(f.path, f.check, f.message)):
        members = tuple(group)
        extra = len(members) - exact[key]
        if extra > 0:
            count, listed = unexplained.get(key[:2], (0, ()))
            unexplained[key[:2]] = (count + extra, listed + members)
    return [Excess(path, check, count - loose[(path, check)], tuple(sorted(listed)))
            for (path, check), (count, listed) in sorted(unexplained.items())
            if count > loose[(path, check)]]


def listing(findings: list[Finding]) -> str:
    files = groupby(sorted(findings), key=lambda f: f.path)
    return '\n'.join(''.join(f'{f.path}:{f.line}: [{f.check}] {f.message}\n' for f in group)
                     for _, group in files)


def excess_report(excesses: list[Excess]) -> str:
    counts: Counter[str] = Counter()
    for item in excesses:
        counts[item.check] += item.count
    summary = ''.join(f'[{check}] +{count} beyond the baseline\n' for check, count in sorted(counts.items()))
    return listing([f for item in excesses for f in item.findings]) + '\n' + summary


def select_sources(args: argparse.Namespace, known: Sources) -> list[str]:
    if args.changed:
        changed = changed_files(args.base)
        missing = sorted(name for name in changed
                         if name.endswith('.cpp') and first_party(ROOT / name) and name not in known
                         and (ROOT / name).is_file())
        if missing:
            raise ValueError('the Windows database lacks ' + ', '.join(missing))
        return affected_sources(known, changed)
    requested = [first_party(ROOT / name) for name in args.files]
    if None in requested or not set(requested) <= set(known):
        raise ValueError('every file must be a first-party source in the Windows database')
    expected = {first_party(path) for path in (ROOT / 'src').rglob('*.cpp')} - {None}
    if not args.files and expected - set(known):
        raise ValueError('the Windows database lacks ' + ', '.join(sorted(expected - set(known))))
    return [name for name in requested if name] or sorted(known)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('files', nargs='*', help='first-party sources; all of them when omitted')
    parser.add_argument('--jobs', type=int, default=4, choices=range(1, 9), metavar='1-8')
    parser.add_argument('--analyzer', action='store_true', help='include the static analyzer')
    parser.add_argument('--changed', action='store_true',
                        help='analyze the sources changed since --base, and the sources that include a changed header')
    parser.add_argument('--base', metavar='REF', help='the revision --changed compares with; the merge base with main when omitted')
    parser.add_argument('--only', action='append', default=[], metavar='CHECK',
                        help='run only the enabled checks that match this name or glob; repeatable')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--check', action='store_true', help='exit 1 on findings beyond the baseline')
    mode.add_argument('--update', action='store_true', help='rewrite the baseline from a full run')
    args = parser.parse_args()
    if args.update and (args.files or args.analyzer or args.changed or args.only):
        parser.error('--update needs a full run without --analyzer, --changed or --only')
    if args.changed and args.files:
        parser.error('--changed selects the sources itself; name no files')
    if args.base and not args.changed:
        parser.error('--base needs --changed')
    return args


def main() -> int:
    args = parse_arguments()
    try:
        sources = select_sources(args, database_sources())
        if not sources:
            print('clang-tidy: no changed sources')
            return 0
        checks = checks_argument(args.only, args.analyzer)
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            parts = pool.map(lambda item: analyze(item[1], checks, f'[{item[0]}/{len(sources)}]'),
                             enumerate(sources, 1))
            findings = sorted({finding for part in parts for finding in part})
        if args.update:
            BASELINE.write_text(baseline_text(findings, len(sources)))
            print(f'wrote {BASELINE.relative_to(ROOT)}: {len(findings)} findings')
            return 0
        excesses = excess(findings, parse_baseline(BASELINE.read_text())) if args.check else []
        print(excess_report(excesses) if excesses else '' if args.check else listing(findings), end='')
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.CalledProcessError) as error:
        detail = error.stderr if isinstance(error, subprocess.CalledProcessError) else error
        print(f'tidy: {detail}', file=sys.stderr)
        return FAILED
    print(f'clang-tidy: {len(sources)} translation units, {len(findings)} distinct first-party findings')
    return FINDINGS_BEYOND if excesses else 0


if __name__ == '__main__':
    sys.exit(main())
