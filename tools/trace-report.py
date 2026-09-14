import argparse
import collections
import json
import pathlib
import re


SEGMENT_SUFFIX = re.compile(r"^(.*?)(?:-(\d+))?\.jsonl$")


def segments_of(paths):
    """The given segments plus their siblings, oldest first."""
    found = {}
    for path in paths:
        match = SEGMENT_SUFFIX.match(path.name)
        if not match:
            found[path.resolve()] = (0, path)
            continue
        base = match.group(1)
        for sibling in path.parent.glob(f"{base}*.jsonl"):
            sibling_match = SEGMENT_SUFFIX.match(sibling.name)
            if sibling_match and sibling_match.group(1) == base:
                index = int(sibling_match.group(2) or 1)
                found[sibling.resolve()] = (index, sibling)
    return [path for _index, path in sorted(found.values(), key=lambda entry: entry[0])]


def lines_in_order(segments):
    for segment in segments:
        with segment.open(encoding="utf-8", errors="replace") as stream:
            yield from stream


def main():
    parser = argparse.ArgumentParser(description="Summarize a diagnostic JSONL run")
    parser.add_argument("trace", type=pathlib.Path, nargs="+",
                        help="a trace segment; its sibling segments are read too")
    args = parser.parse_args()
    segments = segments_of(args.trace)
    counts = collections.Counter()
    commands = {}
    transitions = collections.Counter()
    malformed = 0
    skipped = 0
    recycled = collections.Counter()
    pages = []
    startup = {}
    sessions = set()
    live_targets = {}
    presenter_aliases = 0
    renderer_mismatches = 0
    rejected_presenters = 0
    rejected_leases = 0
    rotations = 0
    for line in lines_in_order(segments):
            try:
                event = json.loads(line)
                if not isinstance(event, dict):
                    raise ValueError("event is not an object")
                fields = event.get("fields", {})
                if not isinstance(fields, dict):
                    raise ValueError("fields is not an object")
            except (ValueError, TypeError):
                malformed += 1
                continue
            kind = event.get("event", "unknown")
            if kind == "rotated":
                rotations += 1
                if not startup:
                    startup = {k: v for k, v in fields.items() if k in ("build", "source_sha256")}
            if kind == "texture":
                action = fields.get("action")
                target = fields.get("target")
                if action == "acquire":
                    presenter = fields.get("presenter")
                    if presenter and any(p == presenter and t != target
                                         for t, p in live_targets.items()):
                        presenter_aliases += 1
                    live_targets[target] = presenter
                    if ("renderer" in fields and "current_renderer" in fields
                            and fields["renderer"] != fields["current_renderer"]):
                        renderer_mismatches += 1
                elif action in ("recycle", "destroy"):
                    live_targets.pop(target, None)
                elif action == "lease_rejected":
                    rejected_leases += 1
                elif action == "presenter_rejected":
                    rejected_presenters += 1
            counts[str(kind)] += 1
            sessions.add(str(event.get("session", "unknown")))
            command = str(event.get("command", 0))
            if kind == "startup":
                startup = fields
            elif kind == "command":
                commands[command] = fields.get("reason", "unknown")
            elif kind == "retire" and fields.get("action") == "begin":
                transitions[command] += 1
            elif kind == "restore" and fields.get("decision") == "skip_all":
                skipped += 1
            elif kind == "texture" and fields.get("action") == "recycle":
                recycled[str(fields.get("target"))] += 1
            elif kind == "page":
                pages.append((event.get("seq"), fields.get("page"), fields.get("selection")))
    print(f"Build: {startup.get('build', 'not recorded')}")
    print(f"Source SHA256: {startup.get('source_sha256', 'not recorded')}")
    print(f"Sessions: {', '.join(sorted(sessions))}")
    print(f"Events: {dict(sorted(counts.items()))}")
    print(f"Malformed/partial lines: {malformed}")
    print(f"Whole-binding restore skips: {skipped}")
    print(f"Acquisitions aliasing a live presenter: {presenter_aliases}")
    print(f"Acquisitions with wrong renderer: {renderer_mismatches}")
    print(f"Presenter rejections: {rejected_presenters}")
    print(f"Texture lease rejections: {rejected_leases}")
    print(f"Targets recycled: {len(recycled)}; total recycles: {sum(recycled.values())}")
    print(f"Segments read: {len(segments)}; rotations seen: {rotations}")
    if rotations and len(segments) <= rotations:
        print("Earlier segments were deleted by rotation; the trace starts mid-run.")
    print("Retirements by originating command:")
    for command, count in transitions.items():
        print(f"  {command}: {commands.get(command, 'unscoped/incomplete trace')}: {count}")
    print("Page/selection observations:")
    for seq, page, selection in pages:
        print(f"  {seq}: {page}: {selection}")
    print("These are recorded transitions, not visual pass/fail results.")


if __name__ == "__main__":
    main()
