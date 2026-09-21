import argparse
import collections
import json
import pathlib
import re


# A rotation segment is a short trailing index (-2, -3, ...); the 16-digit
# run id in a trace file name is part of the base, never a segment.
SEGMENT_SUFFIX = re.compile(r"^(.*?)(?:-(\d{1,4}))?\.jsonl$")


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
    refresh_us = []
    readbacks = collections.defaultdict(lambda: {"count": 0, "us": 0, "max_us": 0})
    heartbeat = collections.Counter()
    heartbeat_max = collections.Counter()
    worst_second = {"refreshes": 0, "refresh_us": 0}
    target_owners = {}
    survivor_snapshots = []
    last_session = None
    shared_adoptions = collections.Counter()
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
            try:
                session = int(event.get("session"))
            except (TypeError, ValueError):
                session = None
            if session is not None and (last_session is None
                                        or session > last_session):
                if last_session is not None and target_owners:
                    survivor_snapshots.append(
                        (last_session, session,
                         collections.Counter(target_owners.values())))
                last_session = session
            if kind == "texture":
                action = fields.get("action")
                target = fields.get("target")
                if action == "acquire":
                    target_owners[target] = str(fields.get("owner", "untagged"))
                elif action in ("recycle", "destroy"):
                    target_owners.pop(target, None)
                elif action and action.endswith("_shared"):
                    shared_adoptions[action[: -len("_shared")]] += 1
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
            if kind == "metrics":
                action = fields.get("action")

                def number(name):
                    try:
                        return int(fields.get(name, 0))
                    except (TypeError, ValueError):
                        return 0

                if action == "refresh":
                    refresh_us.append(number("us"))
                elif action == "readback":
                    op = readbacks[str(fields.get("op", "unknown"))]
                    op["count"] += 1
                    op["us"] += number("us")
                    op["max_us"] = max(op["max_us"], number("us"))
                elif action == "heartbeat":
                    for name in ("refreshes", "refresh_us", "sink_adds",
                                 "sink_removes", "readbacks", "readback_us"):
                        heartbeat[name] += number(name)
                    for name in ("refresh_max_us", "readback_max_us",
                                 "targets_peak", "target_bytes_peak"):
                        heartbeat_max[name] = max(heartbeat_max[name], number(name))
                    if number("refreshes") > worst_second["refreshes"]:
                        worst_second = {"refreshes": number("refreshes"),
                                        "refresh_us": number("refresh_us")}
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
    if refresh_us or heartbeat or readbacks:
        print("Measurement:")
        if refresh_us:
            ordered = sorted(refresh_us)
            p50 = ordered[len(ordered) // 2]
            p95 = ordered[min(len(ordered) - 1, int(len(ordered) * 0.95))]
            print(f"  Refreshes: {len(ordered)}; p50 {p50 / 1000:.1f} ms, "
                  f"p95 {p95 / 1000:.1f} ms, max {ordered[-1] / 1000:.1f} ms")
        if heartbeat:
            print(f"  Worst second: {worst_second['refreshes']} refreshes, "
                  f"{worst_second['refresh_us'] / 1000:.1f} ms spent")
            print(f"  Sink churn: {heartbeat['sink_adds']} adds, "
                  f"{heartbeat['sink_removes']} removes")
            print(f"  Targets peak: {heartbeat_max['targets_peak']} of 512 slots; "
                  f"VRAM peak {heartbeat_max['target_bytes_peak'] / (1 << 20):.0f} MiB")
        for op, stats in sorted(readbacks.items()):
            mean = stats["us"] / stats["count"] / 1000 if stats["count"] else 0
            print(f"  Readback {op}: {stats['count']}; mean {mean:.1f} ms, "
                  f"max {stats['max_us'] / 1000:.1f} ms")
    if shared_adoptions:
        summary = ", ".join(f"{kind}: {count}"
                            for kind, count in shared_adoptions.most_common())
        print(f"Cross-actor adoptions (a shared target reused, no new one): "
              f"{sum(shared_adoptions.values())} ({summary})")
    if survivor_snapshots or target_owners:
        print("Live targets by owner (acquired, not yet recycled or destroyed):")
        for before, after, owners in survivor_snapshots:
            summary = ", ".join(f"{owner}: {count}"
                                for owner, count in owners.most_common())
            print(f"  crossing session {before} -> {after}: "
                  f"{sum(owners.values())} ({summary})")
        if target_owners:
            owners = collections.Counter(target_owners.values())
            summary = ", ".join(f"{owner}: {count}"
                                for owner, count in owners.most_common())
            print(f"  at end of trace: {sum(owners.values())} ({summary})")
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
