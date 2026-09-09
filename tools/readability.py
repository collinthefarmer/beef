#!/usr/bin/env python3
from __future__ import annotations

import argparse
import bisect
import json
import re
import statistics
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Sequence

CONTROL_KEYWORDS = frozenset(
    {
        "if", "for", "while", "switch", "catch", "return", "sizeof", "decltype",
        "else", "do", "constexpr", "static_assert", "requires", "noexcept",
        "alignas", "explicit", "operator",
    }
)

SHORT_NAME_ALLOWLIST = frozenset(
    {"i", "j", "k", "n", "x", "y", "z", "u", "v", "w", "r", "g", "b", "a", "id", "ok", "it", "os", "is", "to", "of", "at", "dt"}
)

LANGUAGE_WORDS = frozenset(
    """alignas alignof and asm auto bool break case catch char class const consteval
    constexpr continue decltype default delete do double dynamic_cast else enum explicit
    export extern false float for friend goto if inline int long mutable namespace new
    noexcept not nullptr operator or private protected public register return short signed
    sizeof static struct switch template this throw true try typedef typeid typename union
    unsigned using virtual void volatile while std size_t uint8_t uint16_t uint32_t uint64_t
    int8_t int16_t int32_t int64_t""".split()
)

MECHANICS = {
    "std::visit": r"std::visit\b",
    "std::get": r"std::get<",
    "holds_alternative": r"holds_alternative\b",
    "static_cast": r"static_cast<",
    "reinterpret_cast": r"reinterpret_cast<",
    "template<": r"\btemplate\s*<",
}

DRIFT_GROUPS = {
    "worn thing": ("piece", "item", "geometry", "shape"),
    "applying": ("apply", "write", "bind", "install"),
    "definition": ("recipe", "effect", "definition"),
    "wearer": ("actor", "wearer", "npc", "character"),
    "removing": ("retire", "remove", "clear", "drop", "revert"),
    "picture": ("texture", "image", "map", "bitmap"),
}


@dataclass(frozen=True)
class Thresholds:
    nesting: int = 4
    length: int = 60
    params: int = 4
    booleans: int = 1


@dataclass(frozen=True)
class Function:
    name: str
    signature: str
    params: tuple[str, ...]
    start_line: int
    end_line: int
    max_depth: int

    @property
    def length(self) -> int:
        return self.end_line - self.start_line + 1

    @property
    def boolean_params(self) -> int:
        return sum(1 for p in self.params if re.search(r"\bbool\b", p))


@dataclass(frozen=True)
class Scan:
    path: Path
    lines: tuple[str, ...]
    code: str
    functions: tuple[Function, ...]
    comment_lines: tuple[int, ...]

    @property
    def line_count(self) -> int:
        return len(self.lines)


@dataclass(frozen=True)
class Finding:
    metric: str
    path: str
    line: int
    value: float
    detail: str


Metric = Callable[[Scan, Thresholds], list[Finding]]


def strip_literals(text: str) -> tuple[str, tuple[int, ...]]:
    out = list(text)
    comments: list[int] = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            comments.append(i)
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            comments.append(i)
            out[i] = out[i + 1] = " "
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            if i + 1 < n:
                out[i] = out[i + 1] = " "
                i += 2
            continue
        if c in "\"'":
            quote = c
            i += 1
            while i < n and text[i] != quote:
                if text[i] == "\\":
                    out[i] = " "
                    i += 1
                    if i < n and text[i] != "\n":
                        out[i] = " "
                    i += 1
                    continue
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            if i < n:
                i += 1
            continue
        i += 1
    return "".join(out), tuple(comments)


def line_starts(text: str) -> list[int]:
    starts = [0]
    for m in re.finditer("\n", text):
        starts.append(m.end())
    return starts


def line_of(starts: Sequence[int], offset: int) -> int:
    return bisect.bisect_right(starts, offset)


def split_params(text: str) -> tuple[str, ...]:
    params: list[str] = []
    depth = 0
    current = ""
    for ch in text:
        if ch in "(<[":
            depth += 1
        elif ch in ")>]":
            depth -= 1
        if ch == "," and depth == 0:
            params.append(current.strip())
            current = ""
            continue
        current += ch
    if current.strip():
        params.append(current.strip())
    return tuple(p for p in params if p and p != "void")


def match_open_paren(text: str, close: int) -> int:
    depth = 0
    for i in range(close, -1, -1):
        if text[i] == ")":
            depth += 1
        elif text[i] == "(":
            depth -= 1
            if depth == 0:
                return i
    return -1


def parse_signature(head: str) -> Function | None:
    trimmed = head.rstrip()
    trimmed = re.sub(r"(?:\s*(?:const|noexcept|override|final|mutable))+$", "", trimmed)
    trimmed = re.sub(r"\s*->\s*[^)]*$", "", trimmed).rstrip()
    if not trimmed.endswith(")"):
        return None
    open_paren = match_open_paren(trimmed, len(trimmed) - 1)
    if open_paren <= 0:
        return None
    before = trimmed[:open_paren].rstrip()
    name_match = re.search(r"([A-Za-z_~][A-Za-z0-9_]*)$", before)
    if not name_match:
        return None
    name = name_match.group(1)
    if name in CONTROL_KEYWORDS:
        return None
    prefix = before[: name_match.start()].rstrip()
    if prefix.endswith("]") or prefix.endswith("."):
        return None
    if not prefix and "::" not in name:
        return None
    params = split_params(trimmed[open_paren + 1 : -1])
    return Function(name, " ".join(trimmed.split()), params, 0, 0, 0)


def scan(path: Path) -> Scan:
    text = path.read_text(encoding="utf-8", errors="replace")
    code, comment_offsets = strip_literals(text)
    starts = line_starts(text)
    lines = tuple(text.splitlines())

    functions: list[Function] = []
    stack: list[dict] = []
    depth = 0
    segment_start = 0

    for i, ch in enumerate(code):
        if ch == "{":
            candidate = parse_signature(code[segment_start:i])
            depth += 1
            for frame in stack:
                frame["max_depth"] = max(frame["max_depth"], depth - frame["depth"])
            stack.append(
                {
                    "function": candidate,
                    "depth": depth,
                    "start": line_of(starts, i),
                    "max_depth": 0,
                }
            )
            segment_start = i + 1
        elif ch == "}":
            if stack:
                frame = stack.pop()
                candidate = frame["function"]
                if candidate is not None:
                    functions.append(
                        Function(
                            candidate.name,
                            candidate.signature,
                            candidate.params,
                            frame["start"],
                            line_of(starts, i),
                            frame["max_depth"],
                        )
                    )
            depth = max(0, depth - 1)
            segment_start = i + 1
        elif ch == ";":
            segment_start = i + 1

    return Scan(
        path=path,
        lines=lines,
        code=code,
        functions=tuple(functions),
        comment_lines=tuple(line_of(starts, o) for o in comment_offsets),
    )


def metric_nesting(s: Scan, t: Thresholds) -> list[Finding]:
    return [
        Finding("nesting", str(s.path), f.start_line, f.max_depth, f"{f.name} nests {f.max_depth} deep")
        for f in s.functions
        if f.max_depth > t.nesting
    ]


def metric_length(s: Scan, t: Thresholds) -> list[Finding]:
    return [
        Finding("length", str(s.path), f.start_line, f.length, f"{f.name} spans {f.length} lines")
        for f in s.functions
        if f.length > t.length
    ]


def metric_params(s: Scan, t: Thresholds) -> list[Finding]:
    findings = []
    for f in s.functions:
        if len(f.params) > t.params:
            findings.append(Finding("params", str(s.path), f.start_line, len(f.params), f"{f.name} takes {len(f.params)} parameters"))
        if f.boolean_params > t.booleans:
            findings.append(Finding("params", str(s.path), f.start_line, f.boolean_params, f"{f.name} takes {f.boolean_params} booleans"))
    return findings


def metric_mechanics(s: Scan, t: Thresholds) -> list[Finding]:
    total = 0
    parts = []
    for name, pattern in MECHANICS.items():
        count = len(re.findall(pattern, s.code))
        if count:
            total += count
            parts.append(f"{name} {count}")
    if not total:
        return []
    per_hundred = round(total * 100 / max(1, s.line_count), 1)
    return [Finding("mechanics", str(s.path), 0, per_hundred, f"{per_hundred} per 100 lines: {', '.join(parts)}")]


def metric_literals(s: Scan, t: Thresholds) -> list[Finding]:
    hits = re.findall(r"[(,]\s*(true|false|nullptr)\s*[,)]", s.code)
    if not hits:
        return []
    tally: dict[str, int] = {}
    for word in hits:
        tally[word] = tally.get(word, 0) + 1
    detail = ", ".join(f"{w} {c}" for w, c in sorted(tally.items(), key=lambda kv: -kv[1]))
    return [Finding("literals", str(s.path), 0, len(hits), f"{len(hits)} bare literals in argument position: {detail}")]


def identifiers(code: str) -> list[str]:
    return re.findall(r"\b[A-Za-z_][A-Za-z0-9_]*\b", code)


def metric_abbreviations(s: Scan, t: Thresholds) -> list[Finding]:
    counts: dict[str, int] = {}
    for name in identifiers(s.code):
        if name in LANGUAGE_WORDS:
            continue
        bare = re.sub(r"^(?:k(?=[A-Z])|a_)", "", name).lower()
        if not bare or bare in SHORT_NAME_ALLOWLIST or bare in LANGUAGE_WORDS:
            continue
        if len(bare) < 4 or not re.search(r"[aeiou]", bare):
            counts[name] = counts.get(name, 0) + 1
    return [
        Finding("abbreviations", str(s.path), 0, count, f"{name} used {count} times")
        for name, count in sorted(counts.items(), key=lambda kv: -kv[1])
    ]


def words_of(identifier: str) -> list[str]:
    return [w.lower() for w in re.findall(r"[A-Z]?[a-z]+|[A-Z]+(?![a-z])", identifier)]


def metric_drift(s: Scan, t: Thresholds) -> list[Finding]:
    seen: dict[str, dict[str, int]] = {group: {} for group in DRIFT_GROUPS}
    for name in identifiers(s.code):
        for word in words_of(name):
            for group, spellings in DRIFT_GROUPS.items():
                if word in spellings:
                    seen[group][word] = seen[group].get(word, 0) + 1
    findings = []
    for group, used in seen.items():
        if len(used) > 1:
            detail = ", ".join(f"{w} {c}" for w, c in sorted(used.items(), key=lambda kv: -kv[1]))
            findings.append(Finding("drift", str(s.path), 0, len(used), f"{group}: {detail}"))
    return findings


def metric_comments(s: Scan, t: Thresholds) -> list[Finding]:
    return [Finding("comments", str(s.path), line, 1, "comment") for line in s.comment_lines]


def metric_auto_signature(s: Scan, t: Thresholds) -> list[Finding]:
    return [
        Finding("auto-signature", str(s.path), f.start_line, 1, f"{f.name}: {f.signature[:70]}")
        for f in s.functions
        if re.search(r"\bauto\b", f.signature)
    ]


def metric_raw_memory(s: Scan, t: Thresholds) -> list[Finding]:
    starts = line_starts(s.code)
    return [
        Finding("raw-memory", str(s.path), line_of(starts, m.start()), 1, m.group(0).strip())
        for m in re.finditer(r"\bnew\s+[A-Za-z_]|\bdelete\s+[A-Za-z_\[]", s.code)
    ]


METRICS: dict[str, Metric] = {
    "nesting": metric_nesting,
    "length": metric_length,
    "params": metric_params,
    "mechanics": metric_mechanics,
    "literals": metric_literals,
    "abbreviations": metric_abbreviations,
    "drift": metric_drift,
    "comments": metric_comments,
    "auto-signature": metric_auto_signature,
    "raw-memory": metric_raw_memory,
}

ZERO_EXPECTED = frozenset({"comments", "auto-signature", "raw-memory"})

SPECULATIVE = frozenset({"drift"})


def collect(paths: Sequence[Path], names: Sequence[str], thresholds: Thresholds) -> tuple[list[Finding], list[Scan]]:
    scans = [scan(p) for p in paths]
    findings: list[Finding] = []
    for s in scans:
        for name in names:
            findings.extend(METRICS[name](s, thresholds))
    return findings, scans


def report(findings: Sequence[Finding], scans: Sequence[Scan], names: Sequence[str], top: int) -> str:
    out: list[str] = []
    lengths = [f.length for s in scans for f in s.functions]
    out.append(f"{len(scans)} files, {sum(s.line_count for s in scans)} lines, {len(lengths)} functions")
    if lengths:
        out.append(
            f"function length: median {int(statistics.median(lengths))}, "
            f"p95 {int(sorted(lengths)[int(len(lengths) * 0.95)])}, max {max(lengths)}"
        )
    for name in names:
        group = [f for f in findings if f.metric == name]
        out.append("")
        expectation = " (expected 0)" if name in ZERO_EXPECTED else ""
        out.append(f"## {name}: {len(group)}{expectation}")
        if name in {"abbreviations", "drift", "mechanics", "literals"}:
            ranked = sorted(group, key=lambda f: -f.value)
        else:
            ranked = sorted(group, key=lambda f: (-f.value, f.path, f.line))
        for f in ranked[:top]:
            place = f"{f.path}:{f.line}" if f.line else f.path
            out.append(f"  {place}  {f.detail}")
        if len(ranked) > top:
            out.append(f"  ... {len(ranked) - top} more")
    return "\n".join(out)


def main() -> int:
    parser = argparse.ArgumentParser(description="Readability metrics over C++ sources.")
    parser.add_argument("globs", nargs="*", default=["src/*.cpp", "src/*.h"])
    parser.add_argument("--exclude", action="append", default=["extern"])
    parser.add_argument("--metric", action="append", choices=sorted(METRICS), help="run only these")
    parser.add_argument("--top", type=int, default=15)
    parser.add_argument("--nesting", type=int, default=Thresholds.nesting)
    parser.add_argument("--length", type=int, default=Thresholds.length)
    parser.add_argument("--params", type=int, default=Thresholds.params)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    root = Path.cwd()
    paths = sorted(
        {
            p
            for pattern in args.globs
            for p in root.glob(pattern)
            if p.is_file() and not any(x in p.parts for x in args.exclude)
        }
    )
    if not paths:
        parser.error("no files matched")
    paths = [p.relative_to(root) for p in paths]

    names = args.metric or [n for n in METRICS if n not in SPECULATIVE]
    thresholds = Thresholds(nesting=args.nesting, length=args.length, params=args.params)
    findings, scans = collect(paths, names, thresholds)

    if args.json:
        print(json.dumps([f.__dict__ for f in findings], indent=1))
    else:
        print(report(findings, scans, names, args.top))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
