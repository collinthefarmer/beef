"""Compare successful tidy reports with the reviewed diagnostic allowance."""
import collections
import os
import json
from pathlib import Path
import re
import sys

os.chdir(Path(__file__).resolve().parents[1])
report = Path('build/tidy/latest.json')
target = Path('tools/tidy-baseline.txt')
if not report.exists():
    sys.exit('No successful tidy report; run python3 tools/tidy.py first.')
data = json.loads(report.read_text())
rows = {tuple(row) for row in data['diagnostics']}
pattern = re.compile(r'^(.*):(\d+): \[([^\]]+)\]$')
base = set()
if target.exists():
    for line in target.read_text().splitlines():
        match = pattern.match(line)
        if match:
            path, number, check = match.groups()
            base.add((path, int(number), check))
if '--gate' in sys.argv or '--check' in sys.argv:
    if '--check' in sys.argv and not data['full']:
        sys.exit('A full tidy run is required for the full baseline check.')
    if '--gate' in sys.argv:
        requested = sys.argv[sys.argv.index('--gate') + 1:]
        missing = set(requested) - set(data['files'])
        if missing and not data['full']:
            sys.exit('The tidy report does not cover the requested files.')
    current = collections.Counter((path, check) for path, _, check in rows)
    previous = collections.Counter((path, check) for path, _, check in base)
    added = current - previous
    if added:
        for (path, check), count in sorted(added.items()):
            print(f'{path}: [{check}] +{count}', file=sys.stderr)
        sys.exit('New clang-tidy findings; fix or explicitly review the baseline change.')
    print('clang-tidy: no new findings (including headers; line numbers ignored)')
else:
    if not data['full']:
        sys.exit('Refusing to replace the baseline from a partial run.')
    text = '# clang-tidy baseline; distinct diagnostics, compared by file/check count\n'
    text += '# regenerate after review: python3 tools/tidy.py && python3 tools/tidy-baseline.py\n\n'
    text += ''.join(f'{path}:{line}: [{check}]\n' for path, line, check in sorted(rows))
    target.write_text(text)
    print(f'wrote {target}: {len(rows)} distinct findings')
