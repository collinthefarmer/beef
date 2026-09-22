"""Run clang-tidy over a CMake database; results are reports, never a cache."""
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
WARNING = re.compile(r'^(.*?):(\d+):\d+: warning: .* \[([a-z][a-z0-9.,-]+)\]$')


def source_path(entry):
    path = Path(entry['file'])
    return (Path(entry['directory']) / path).resolve()


def own_source(path):
    try:
        rel = path.relative_to(ROOT)
    except ValueError:
        return False
    return bool(rel.parts) and rel.parts[0] == 'src' and not any(
        part in ('_old', 'extern', 'cs') for part in rel.parts)


def changed_files():
    tracked = subprocess.check_output(
        ['git', 'diff', '--name-only', '-z', 'HEAD', '--', 'src'], cwd=ROOT)
    untracked = subprocess.check_output(
        ['git', 'ls-files', '--others', '--exclude-standard', '-z', '--', 'src'], cwd=ROOT)
    return [os.fsdecode(p) for p in (tracked + untracked).split(b'\0') if p]


def select(entries, paths, selected):
    sources = sorted({source_path(e) for e in entries if own_source(source_path(e))})
    if not selected:
        return sources, True
    paths = [(ROOT / p).resolve() for p in paths if own_source((ROOT / p).resolve())]
    # Header edits and removals need a full pass without a second dependency graph.
    if any(p.suffix in ('.h', '.hpp', '.hxx', '.inl') or not p.exists() for p in paths):
        return sources, True
    missing = set(paths) - set(sources)
    if missing:
        raise ValueError('Sources absent from compilation database: ' + ', '.join(map(str, sorted(missing))))
    return sorted(set(paths)), set(paths) == set(sources)


def run(args):
    os.chdir(ROOT)
    output = ROOT / 'build' / ('tidy-analyzer' if args.analyzer else 'tidy')
    output.mkdir(parents=True, exist_ok=True)
    report = output / 'latest.json'
    # Invalidate before any fallible work: a failed invocation has no usable report.
    report.unlink(missing_ok=True)
    entries = json.loads((Path(args.build_dir) / 'compile_commands.json').read_text())
    paths = args.files + (changed_files() if args.changed else [])
    files, full = select(entries, paths, bool(args.files) or args.changed)
    if full:
        expected = {p.resolve() for p in (ROOT / 'src').rglob('*.cpp') if own_source(p.resolve())}
        missing = expected - set(files)
        if missing:
            raise ValueError('Full analysis requires a current Windows database; missing: ' +
                             ', '.join(str(p.relative_to(ROOT)) for p in sorted(missing)))
    diagnostics = set()
    failures = []
    binary = os.environ.get('CLANG_TIDY', 'clang-tidy')
    with tempfile.TemporaryDirectory(prefix='run-', dir=output) as temp:
        def analyze(source):
            print(f'tidy {source.relative_to(ROOT)}', file=sys.stderr, flush=True)
            command = [binary, '-p', str(Path(args.build_dir).resolve()), '--quiet']
            if not args.analyzer:
                command.append('--checks=-clang-analyzer-*')
            command.append(str(source))
            result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, check=False)
            log = Path(temp) / (str(source.relative_to(ROOT)).replace('/', '_') + '.txt')
            log.write_text(result.stdout)
            return source, result
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            for source, result in pool.map(analyze, files):
                if result.returncode:
                    failures.append(str(source.relative_to(ROOT)))
                    print(result.stdout, file=sys.stderr)
                for line in result.stdout.splitlines():
                    match = WARNING.match(line)
                    if match:
                        path, number, check = match.groups()
                        path = Path(path)
                        if not path.is_absolute():
                            path = Path(args.build_dir).resolve() / path
                        if own_source(path.resolve()):
                            diagnostics.add((str(path.resolve().relative_to(ROOT)), int(number), check))
        # Keep logs for diagnosis even when a run fails, but publish no successful report.
        logs = output / 'logs'
        logs.mkdir(exist_ok=True)
        for log in Path(temp).iterdir():
            log.replace(logs / log.name)
    if failures:
        raise RuntimeError('clang-tidy failed: ' + ', '.join(failures))
    data = {'full': full, 'analyzer': args.analyzer,
            'files': [str(f.relative_to(ROOT)) for f in files],
            'diagnostics': sorted(diagnostics)}
    temporary = report.with_suffix('.part')
    temporary.write_text(json.dumps(data, indent=2) + '\n')
    temporary.replace(report)
    print(f'clang-tidy: {len(files)} translation units, {len(diagnostics)} distinct findings')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('files', nargs='*')
    parser.add_argument('--changed', action='store_true')
    parser.add_argument('--analyzer', action='store_true')
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--build-dir', default=os.environ.get('TIDY_BUILD_DIR', 'build/Release'))
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    try:
        run(args)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f'tidy: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
