"""Run clang-tidy over the Windows database and compare findings with the reviewed baseline."""
import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DATABASE = ROOT / 'build/Release'
BASELINE = ROOT / 'tools/tidy-baseline.txt'
WARNING = re.compile(r'^(.*?):(\d+):\d+: warning: .* \[([A-Za-z][A-Za-z0-9_.,-]+)\]$')
BASELINE_ROW = re.compile(r'^(.*):(\d+): \[([^\]]+)\]$')
Finding = tuple[str, int, str]


def first_party(path: Path) -> str | None:
    try:
        name = path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return None
    return name if name.startswith('src/') and not name.startswith(('src/extern/', 'src/cs/')) else None


def database_sources() -> list[str]:
    entries = json.loads((DATABASE / 'compile_commands.json').read_text())
    names = {first_party(Path(entry['directory']) / entry['file']) for entry in entries}
    return sorted(name for name in names if name)


def analyze(source: str, analyzer: bool) -> list[Finding]:
    command = [os.environ.get('CLANG_TIDY', 'clang-tidy'), '-p', str(DATABASE), '--quiet', source]
    if not analyzer:
        command.insert(-1, '--checks=-clang-analyzer-*')
    print(f'tidy {source}', file=sys.stderr, flush=True)
    result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, check=False)
    if result.returncode:
        raise RuntimeError(f'clang-tidy failed on {source}:\n{result.stdout}')
    findings = []
    for match in filter(None, map(WARNING.match, result.stdout.splitlines())):
        name = first_party(DATABASE / match.group(1))
        if name:
            findings.append((name, int(match.group(2)), match.group(3)))
    return findings


def baseline() -> list[Finding]:
    rows = filter(None, map(BASELINE_ROW.match, BASELINE.read_text().splitlines()))
    return [(row.group(1), int(row.group(2)), row.group(3)) for row in rows]


def write_baseline(findings: list[Finding], sources: int) -> None:
    checks = Counter(check for _, _, check in findings)
    BASELINE.write_text(
        f'# clang-tidy baseline, {sources} sources, {len(findings)} findings\n'
        '# regenerate: python3 tools/tidy.py --update\n\n'
        + ''.join(f'# {check:<52} {count}\n' for check, count in sorted(checks.items(), key=lambda item: (-item[1], item[0]))) + '\n'
        + ''.join(f'{name}:{line}: [{check}]\n' for name, line, check in findings))


def new_findings(findings: list[Finding]) -> Counter[tuple[str, str]]:
    return (Counter((name, check) for name, _, check in findings)
            - Counter((name, check) for name, _, check in baseline()))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('files', nargs='*', help='first-party sources; all of them when omitted')
    parser.add_argument('--jobs', type=int, default=4, choices=range(1, 9), metavar='1-8')
    parser.add_argument('--analyzer', action='store_true', help='include the static analyzer')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--check', action='store_true', help='fail on findings beyond the baseline')
    mode.add_argument('--update', action='store_true', help='rewrite the baseline from a full run')
    args = parser.parse_args()
    if args.update and (args.files or args.analyzer):
        parser.error('--update needs a full run without --analyzer')
    try:
        known = database_sources()
        requested = [first_party(ROOT / name) for name in args.files]
        if None in requested or not set(requested) <= set(known):
            raise ValueError('every file must be a first-party source in the Windows database')
        expected = {first_party(path) for path in (ROOT / 'src').rglob('*.cpp')} - {None}
        if not args.files and expected - set(known):
            raise ValueError('the Windows database lacks ' + ', '.join(sorted(expected - set(known))))
        sources = requested or known
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            findings = sorted({f for part in pool.map(lambda s: analyze(s, args.analyzer), sources)
                               for f in part})
        if args.update:
            write_baseline(findings, len(sources))
            print(f'wrote {BASELINE.relative_to(ROOT)}: {len(findings)} findings')
            return 0
        added = new_findings(findings) if args.check else Counter()
        for (name, check), count in sorted(added.items()):
            print(f'{name}: [{check}] +{count} beyond the baseline')
        if not args.check:
            print(''.join(f'{name}:{line}: [{check}]\n' for name, line, check in findings), end='')
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        print(f'tidy: {error}', file=sys.stderr)
        return 1
    print(f'clang-tidy: {len(sources)} translation units, {len(findings)} distinct first-party findings')
    return 1 if added else 0


if __name__ == '__main__':
    sys.exit(main())
