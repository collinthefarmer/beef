"""Summarize a diagnostic JSONL trace and its sibling rotation segments."""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re
import sys
from typing import Iterator

SEGMENT = re.compile(r'^(.*?)(?:-(\d{1,4}))?\.jsonl$')
SESSION = re.compile(r'-?\d+')
TALLIES = (('malformed', 'Malformed/partial lines'), ('skipped', 'Whole-binding restore skips'),
           ('aliases', 'Acquisitions aliasing a live presenter'),
           ('mismatches', 'Acquisitions with wrong renderer'),
           ('presenter_rejected', 'Presenter rejections'), ('lease_rejected', 'Texture lease rejections'))


def segments_of(paths: list[Path]) -> list[Path]:
    found: dict[Path, tuple[int, Path]] = {}
    for path in paths:
        match = SEGMENT.match(path.name)
        siblings = path.parent.glob(f'{match.group(1)}*.jsonl') if match else [path]
        for sibling in siblings:
            other = SEGMENT.match(sibling.name)
            if not match or (other and other.group(1) == match.group(1)):
                found[sibling.resolve()] = (int(other.group(2) or 1) if match else 0, sibling)
    return [path for _, path in sorted(found.values(), key=lambda entry: entry[0])]


def events(segments: list[Path]) -> Iterator[dict | None]:
    for segment in segments:
        with segment.open(encoding='utf-8', errors='replace') as stream:
            for line in stream:
                try:
                    yield json.loads(line)
                except (ValueError, RecursionError):
                    yield None


def number(fields: dict, name: str) -> int:
    try:
        return int(fields.get(name, 0))
    except (TypeError, ValueError, OverflowError):
        return 0


def listing(owners: Counter[str]) -> str:
    return f"{sum(owners.values())} ({', '.join(f'{o}: {c}' for o, c in owners.most_common())})"


def report(segments: list[Path], slots: int | None) -> None:
    counts, tally, recycled, heartbeat, peaks, shared, transitions, rendering, latest = (
        Counter() for _ in range(9))
    commands, readbacks, refreshes, pages, survivors = {}, defaultdict(lambda: [0, 0, 0]), [], [], []
    gpu_spans, gpu_ticks = defaultdict(lambda: [0, 0, 0]), Counter()
    fusion = {'checks': 0, 'over': 0, 'max': 0.0}
    startup, sessions, live, owners, worst, last_session = {}, set(), {}, {}, (0, 0), None
    for event in events(segments):
        fields = event.get('fields', {}) if isinstance(event, dict) else None
        if not isinstance(fields, dict):
            tally['malformed'] += 1
            continue
        kind, action = str(event.get('event', 'unknown')), fields.get('action')
        action = action if isinstance(action, str) else ''
        session, command = str(event.get('session', 'unknown')), str(event.get('command', 0))
        target = str(fields.get('target'))
        if kind == 'rotated':
            tally['rotations'] += 1
            startup = startup or {k: v for k, v in fields.items() if k in ('build', 'source', 'source_sha256')}
        if SESSION.fullmatch(session) and (last_session is None or int(session) > last_session):
            if last_session is not None and owners:
                survivors.append((last_session, int(session), Counter(owners.values())))
            last_session = int(session)
        if kind == 'texture' and action == 'acquire':
            owners[target] = str(fields.get('owner', 'untagged'))
            presenter = fields.get('presenter')
            tally['aliases'] += bool(presenter) and any(p == presenter and t != target for t, p in live.items())
            live[target] = presenter
            tally['mismatches'] += ('renderer' in fields and 'current_renderer' in fields
                                    and fields['renderer'] != fields['current_renderer'])
        elif kind == 'texture' and action in ('recycle', 'destroy'):
            owners.pop(target, None)
            live.pop(target, None)
            recycled[target] += action == 'recycle'
        elif kind == 'texture':
            shared[action.removesuffix('_shared')] += action.endswith('_shared')
            tally[action] += action in ('lease_rejected', 'presenter_rejected')
        elif kind == 'metrics' and action == 'refresh':
            refreshes.append(number(fields, 'us'))
        elif kind == 'metrics' and action == 'readback':
            op, us = readbacks[str(fields.get('op', 'unknown'))], number(fields, 'us')
            op[:] = op[0] + 1, op[1] + us, max(op[2], us)
        elif kind == 'metrics' and action == 'fusion_check':
            fusion['checks'] += number(fields, 'checks')
            fusion['over'] += number(fields, 'over_one_step')
            fusion['max'] = max(fusion['max'], float(fields.get('max_difference', 0) or 0))
        elif kind == 'metrics' and action == 'gpu_ticks':
            for name in ('timed', 'dropped', 'discarded', 'untimed_spans'):
                gpu_ticks[name] += number(fields, name)
        elif kind == 'metrics' and action == 'gpu_span':
            span = gpu_spans[str(fields.get('span', 'unknown'))]
            span[:] = (span[0] + number(fields, 'count'), span[1] + number(fields, 'total_us'),
                       max(span[2], number(fields, 'max_us')))
        elif kind == 'metrics' and action == 'heartbeat':
            heartbeat.update({name: number(fields, name) for name in ('sink_adds', 'sink_removes')})
            for name in ('targets_peak', 'target_bytes_peak'):
                peaks[name] = max(peaks[name], number(fields, name))
            for name in ('targets', 'target_bytes'):
                latest[name] = number(fields, name)
            worst = max(worst, (number(fields, 'refreshes'), number(fields, 'refresh_us')), key=lambda w: w[0])
            for name in ('frames', 'render_evaluations', 'step_executions', 'step_releases',
                         'step_restores', 'frame_us', 'tick_us', 'snapshot_us'):
                rendering[name] += number(fields, name)
            for name in ('frame_max_us', 'tick_max_us', 'snapshot_max_us'):
                peaks[name] = max(peaks[name], number(fields, name))
        counts[kind] += 1
        sessions.add(session)
        startup = fields if kind == 'startup' else startup
        if kind == 'command':
            commands[command] = fields.get('reason', 'unknown')
        if kind == 'retire' and action == 'begin':
            transitions[command] += 1
        tally['skipped'] += kind == 'restore' and fields.get('decision') == 'skip_all'
        if kind == 'page':
            pages.append((event.get('seq'), fields.get('page'), fields.get('selection')))
    print(f"Build: {startup.get('build', 'not recorded')}")
    print(f"Source: {startup.get('source', startup.get('source_sha256', 'not recorded'))}")
    ordered_sessions = sorted(sessions, key=lambda s: (0, int(s), '') if SESSION.fullmatch(s) else (1, 0, s))
    print(f"Sessions: {', '.join(ordered_sessions)}")
    print(f'Events: {dict(sorted(counts.items()))}')
    for key, label in TALLIES:
        print(f'{label}: {tally[key]}')
    print(f'Targets recycled: {len(+recycled)}; total recycles: {recycled.total()}')
    if refreshes or heartbeat or readbacks:
        print('Measurement:')
    if refreshes:
        ordered = sorted(refreshes)
        p50, p95 = ordered[len(ordered) // 2], ordered[min(len(ordered) - 1, int(len(ordered) * 0.95))]
        print(f'  Refreshes: {len(ordered)}; p50 {p50 / 1000:.1f} ms, p95 {p95 / 1000:.1f} ms, '
              f'max {ordered[-1] / 1000:.1f} ms')
    if heartbeat:
        print(f'  Worst second: {worst[0]} refreshes, {worst[1] / 1000:.1f} ms spent')
        print(f"  Sink churn: {heartbeat['sink_adds']} adds, {heartbeat['sink_removes']} removes")
        if rendering['frames']:
            frames = rendering['frames']
            print(f"  Render steps: {rendering['step_executions'] / frames:.2f} executions and "
                  f"{rendering['render_evaluations'] / frames:.2f} stack evaluations per frame "
                  f"over {frames} frames; {rendering['step_releases']} releases, "
                  f"{rendering['step_restores']} restores")
            print(f"  Plugin CPU per frame: {rendering['frame_us'] / frames / 1000:.2f} ms "
                  f"(max {peaks['frame_max_us'] / 1000:.1f}); tick "
                  f"{rendering['tick_us'] / frames / 1000:.2f} ms (max {peaks['tick_max_us'] / 1000:.1f}); "
                  f"snapshot {rendering['snapshot_us'] / frames / 1000:.2f} ms "
                  f"(max {peaks['snapshot_max_us'] / 1000:.1f})")
        print(f"  Targets peak: {peaks['targets_peak']}{f' of {slots} slots' if slots else ''}; "
              f"VRAM peak {peaks['target_bytes_peak'] / (1 << 20):.0f} MiB")
        print(f"  Targets at the last heartbeat: {latest['targets']}; "
              f"{latest['target_bytes'] / (1 << 20):.0f} MiB")
    if fusion['checks']:
        print(f"Fusion check: {fusion['checks']} stacks compared; largest difference "
              f"{fusion['max']:.2f} of 255; {fusion['over']} over one 8-bit step")
    if gpu_ticks['timed']:
        timed = gpu_ticks['timed']
        print(f"GPU time per timed tick ({timed} timed, {gpu_ticks['dropped']} dropped, "
              f"{gpu_ticks['discarded']} discarded, {gpu_ticks['untimed_spans']} untimed spans):")
        for name, (count, total, peak) in sorted(gpu_spans.items(), key=lambda item: -item[1][1]):
            print(f'  {name}: {total / timed / 1000:.2f} ms over {count / timed:.1f} spans; '
                  f'max {peak / 1000:.2f} ms')
    for op, (count, total, peak) in sorted(readbacks.items()):
        print(f'  Readback {op}: {count}; mean {total / count / 1000:.1f} ms, max {peak / 1000:.1f} ms')
    if +shared:
        print(f'Cross-actor adoptions (a shared target reused, no new one): {listing(+shared)}')
    if survivors or owners:
        print('Live targets by owner (acquired, not yet recycled or destroyed):')
        for before, after, crossing in survivors:
            print(f'  crossing session {before} -> {after}: {listing(crossing)}')
        if owners:
            print(f'  at end of trace: {listing(Counter(owners.values()))}')
    print(f"Segments read: {len(segments)}; rotations seen: {tally['rotations']}")
    if tally['rotations'] and len(segments) <= tally['rotations']:
        print('Earlier segments were deleted by rotation; the trace starts mid-run.')
    print('Retirements by originating command:')
    for command, count in transitions.items():
        print(f"  {command}: {commands.get(command, 'unscoped/incomplete trace')}: {count}")
    print('Page/selection observations:')
    for seq, page, selection in pages:
        print(f'  {seq}: {page}: {selection}')
    print('These are recorded transitions, not visual pass/fail results.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path, nargs='+', help='a segment; its siblings are read too')
    parser.add_argument('--slots', type=int, help='presenter slot count, to show the peak against')
    arguments = parser.parse_args()
    try:
        segments = segments_of(arguments.trace)
        if not segments:
            raise OSError(f"no trace segment matches {', '.join(map(str, arguments.trace))}")
        report(segments, arguments.slots)
    except OSError as error:
        sys.exit(f'trace-report: {error}')
