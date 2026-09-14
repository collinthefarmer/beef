#!/usr/bin/env bash
set -uo pipefail
cd "$(dirname "$0")/.."
python3 - "$@" <<'PY'
import collections, pathlib, re, sys
root = pathlib.Path('.').resolve()
source = pathlib.Path('build/tidy')
target = pathlib.Path('docs/wip/tidy-baseline.txt')

WARNING = re.compile(r'^(/\S+?):(\d+):(\d+): warning: (.*) \[([a-z][a-z0-9.-]+)\]$')
BASELINE_ROW = re.compile(r'^(\S+):(\d+): \[([a-z][a-z0-9.-]+)\]$')


def rel_to_root(path):
    try:
        return str(pathlib.Path(path).resolve().relative_to(root))
    except ValueError:
        return None


def current_rows():
    rows = []
    for f in sorted(source.glob('*.txt')):
        for line in f.read_text(errors='replace').splitlines():
            m = WARNING.match(line)
            if not m:
                continue
            path, line_no, _col, _msg, check = m.groups()
            rel = rel_to_root(path)
            if rel is not None:
                rows.append((rel, int(line_no), check))
    rows.sort()
    return rows


def baseline_counts():
    counts = collections.Counter()
    if not target.exists():
        return counts
    for line in target.read_text().splitlines():
        m = BASELINE_ROW.match(line)
        if m:
            counts[(m.group(1), m.group(3))] += 1
    return counts


# --gate FILE...: per-file, line-insensitive commit check. For each staged
# source, compare its own findings per check against the baseline's count for
# that file; a higher current count is a new finding and blocks the commit.
if '--gate' in sys.argv:
    gated = [a for a in sys.argv[sys.argv.index('--gate') + 1:] if not a.startswith('--')]
    base = baseline_counts()
    failures = []
    for src in gated:
        rel = rel_to_root(src)
        if rel is None:
            continue
        result = source / (rel.removesuffix('.cpp').replace('/', '_') + '.txt')
        cur = collections.Counter()
        if result.exists():
            for line in result.read_text(errors='replace').splitlines():
                m = WARNING.match(line)
                if m and rel_to_root(m.group(1)) == rel:
                    cur[m.group(5)] += 1
        for check in set(cur) | {c for (f, c) in base if f == rel}:
            was, now = base.get((rel, check), 0), cur.get(check, 0)
            if now > was:
                failures.append((rel, check, was, now))
    if failures:
        print("commit blocked: new clang-tidy findings in staged files:", file=sys.stderr)
        for rel, check, was, now in sorted(failures):
            print("  %s: [%s] %d -> %d" % (rel, check, was, now), file=sys.stderr)
        print("fix them, or if intended regenerate the baseline:", file=sys.stderr)
        print("    tools/tidy.sh --force && tools/tidy-baseline.sh", file=sys.stderr)
        sys.exit(1)
    print("staged files: no new clang-tidy findings")
    sys.exit(0)

rows = current_rows()
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
