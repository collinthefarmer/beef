#!/usr/bin/env bash
set -uo pipefail
cd "$(dirname "$0")/.."
python3 - "$@" <<'PY'
import collections, pathlib, re, sys
root = pathlib.Path('.').resolve()
source = pathlib.Path('build/tidy')
target = pathlib.Path('docs/wip/tidy-baseline.txt')
rows = []
for f in sorted(source.glob('*.txt')):
	for line in f.read_text(errors='replace').splitlines():
		m = re.match(r'^(/\S+?):(\d+):(\d+): warning: (.*) \[([a-z][a-z0-9.-]+)\]$', line)
		if not m:
			continue
		path, line_no, _col, _msg, check = m.groups()
		try:
			rel = str(pathlib.Path(path).resolve().relative_to(root))
		except ValueError:
			continue
		rows.append((rel, int(line_no), check))
rows.sort()
counts = collections.Counter(c for _, _, c in rows)
out = ["# clang-tidy baseline, %d sources, %d findings" % (len(list(source.glob('*.txt'))), len(rows)),
       "# regenerate: tools/tidy.sh && tools/tidy-baseline.sh", ""]
out += ["# %-52s %s" % (c, n) for c, n in counts.most_common()]
out += [""]
out += ["%s:%d: [%s]" % r for r in rows]
if "--check" in sys.argv:
	current = "\n".join(out) + "\n"
	if not target.exists() or target.read_text() != current:
		print("clang-tidy findings differ from docs/wip/tidy-baseline.txt", file=sys.stderr)
		sys.exit(1)
	print("clang-tidy findings match the baseline")
else:
	target.write_text("\n".join(out) + "\n")
	print("wrote %s: %d findings" % (target, len(rows)))
PY
