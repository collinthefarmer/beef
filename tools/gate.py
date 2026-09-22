"""Run repository validation with the tools supplied by the Nix entry point."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
FROZEN = re.compile(r'^(src/(_old|extern|cs)|tests/_old)/')


def run(*args):
    subprocess.run(args, check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('stage', choices=('commit', 'push', 'release'))
    stage = parser.parse_args().stage
    os.chdir(ROOT)
    if not os.environ.get('BEEF_DEV_SHELL'):
        sys.exit('Run tools/gate.sh to enter the pinned Nix environment.')
    if stage == 'commit':
        names = subprocess.check_output([
            'git', 'diff', '--cached', '--name-only', '-z', '--diff-filter=ACMRD',
            '--', 'src/*.cpp', 'src/*.h', 'tests/*.cpp', 'tests/*.h',
        ]).decode().split('\0')
        staged = [name for name in names if name and not FROZEN.match(name)]
        if staged:
            run('tools/format.sh', '--check', *staged)
            run('tools/layers.sh')
        return
    run('tools/format.sh', '--check')
    run('tools/layers.sh')
    findings = []
    for path in sorted(Path('src').rglob('*')):
        if path.suffix not in ('.cpp', '.h') or FROZEN.match(path.as_posix()):
            continue
        for number, line in enumerate(path.read_text().splitlines(), 1):
            if re.search(r'(^|[\s;}])//', line) and 'NOLINT' not in line:
                findings.append(f'{path}:{number}: {line}')
    if findings:
        sys.exit('\n'.join(findings) + '\nMove C++ prose comments to REFERENCE.md.')
    run('cmake', '--preset', 'native-sanitized')
    run('cmake', '--build', '--preset', 'native-sanitized')
    run('ctest', '--preset', 'native-sanitized')
    if stage == 'release':
        run('cmake', '--preset', 'windows-release')
        run('cmake', '--build', '--preset', 'windows-release')
        run(sys.executable, 'tools/compile-db.py')
        run(sys.executable, 'tools/tidy.py')
        run(sys.executable, 'tools/tidy-baseline.py', '--check')


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
    except OSError as error:
        sys.exit(str(error))
