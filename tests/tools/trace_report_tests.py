"""Exercise the trace report over rotated segments without the plugin."""
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def event(seq, kind, session=0, **fields):
    return json.dumps({"schema": 1, "run": "r", "seq": seq, "event": kind,
                       "fields": fields, "session": session, "command": 0}) + "\n"


class TraceReportTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = pathlib.Path(self.folder.name)

    def report(self, *paths):
        return subprocess.run([sys.executable, str(ROOT / "tools/trace-report.py"), *map(str, paths)],
                              text=True, capture_output=True)

    def test_sibling_segments_are_read_in_order(self):
        (self.root / "beef-trace-7-2.jsonl").write_text(
            event(3, "rotated", build="b1", segment=2)
            + event(4, "page", page="Recipes", selection="a"))
        (self.root / "beef-trace-7-3.jsonl").write_text(
            event(5, "rotated", build="b1", segment=3)
            + event(6, "page", page="Studio", selection="b"))
        result = self.report(self.root / "beef-trace-7-3.jsonl")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Segments read: 2; rotations seen: 2", result.stdout)
        self.assertIn("Build: b1", result.stdout)
        self.assertIn("starts mid-run", result.stdout)
        self.assertLess(result.stdout.index("Recipes: a"), result.stdout.index("Studio: b"))

    def test_gpu_spans_report_per_timed_tick(self):
        (self.root / "beef-trace-11.jsonl").write_text(
            event(1, "metrics", action="gpu_ticks", timed="2", dropped="1",
                  discarded="0", untimed_spans="0")
            + event(2, "metrics", action="gpu_span", span="RenderTick", count="2",
                    total_us="20000", max_us="12000")
            + event(3, "metrics", action="gpu_span", span="GenerateMips 2048",
                    count="10", total_us="8000", max_us="900"))
        result = self.report(self.root / "beef-trace-11.jsonl")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("GPU time per timed tick (2 timed, 1 dropped", result.stdout)
        self.assertIn("RenderTick: 10.00 ms over 1.0 spans; max 12.00 ms", result.stdout)
        self.assertIn("GenerateMips 2048: 4.00 ms over 5.0 spans", result.stdout)
        self.assertLess(result.stdout.index("RenderTick"), result.stdout.index("GenerateMips"))

    def test_metrics_events_summarize(self):
        (self.root / "beef-trace-9.jsonl").write_text(
            event(1, "startup", build="b3", source_sha256="s")
            + event(2, "metrics", action="refresh", actor="20", us="2000")
            + event(3, "metrics", action="refresh", actor="21", us="6000")
            + event(4, "metrics", action="readback", op="mean", us="1500")
            + event(5, "metrics", action="heartbeat", refreshes="2",
                    refresh_us="8000", refresh_max_us="6000", sink_adds="3",
                    sink_removes="1", readbacks="1", readback_us="1500",
                    readback_max_us="1500", targets="4", targets_peak="5",
                    target_bytes="1048576", target_bytes_peak="2097152"))
        result = self.report(self.root / "beef-trace-9.jsonl", "--slots", "1024")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Refreshes: 2", result.stdout)
        self.assertIn("max 6.0 ms", result.stdout)
        self.assertIn("Worst second: 2 refreshes, 8.0 ms spent", result.stdout)
        self.assertIn("Sink churn: 3 adds, 1 removes", result.stdout)
        self.assertIn("Targets peak: 5 of 1024 slots; VRAM peak 2 MiB", result.stdout)
        self.assertIn("Readback mean: 1; mean 1.5 ms, max 1.5 ms", result.stdout)
        unstated = self.report(self.root / "beef-trace-9.jsonl")
        self.assertIn("Targets peak: 5; VRAM peak 2 MiB", unstated.stdout)

    def test_malformed_lines_and_values_never_crash(self):
        (self.root / "beef-trace-11.jsonl").write_text(
            "not json\n" + "[" * 100000 + "\n" + "[]\n" + '{"fields": 3}\n' + "Infinity\n"
            + json.dumps({"event": ["texture"], "fields": {}}) + "\n"
            + event(1, "texture", action=7, target=[1, 2], owner={"a": 1})
            + event(2, "texture", action=["acquire"], target={"t": 1})
            + event(3, "texture", action="acquire", target=[1], presenter=[2])
            + event(4, "metrics", action="heartbeat", refreshes=float("inf"), targets_peak=[1])
            + event(5, "metrics", action="readback", op=[1], us={"x": 1})
            + event(6, "page", session=[1], page={"p": 1}, selection=None)
            + event(7, "startup", session="late", build=["b"]))
        result = self.report(self.root / "beef-trace-11.jsonl")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Malformed/partial lines: 5", result.stdout)

    def test_sessions_sort_as_numbers(self):
        (self.root / "beef-trace-12.jsonl").write_text(
            "".join(event(i, "page", session=s, page="p", selection="")
                    for i, s in enumerate((10, 2, 1, "late"))))
        result = self.report(self.root / "beef-trace-12.jsonl")
        self.assertIn("Sessions: 1, 2, 10, late", result.stdout)

    def test_missing_trace_is_a_message(self):
        result = self.report(self.root / "absent.jsonl")
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("Traceback", result.stderr)

    def test_survivors_are_grouped_by_owner_at_session_boundaries(self):
        (self.root / "beef-trace-10.jsonl").write_text(
            event(1, "startup", build="b4", source_sha256="s")
            + event(2, "texture", action="acquire", target="1", owner="stack")
            + event(3, "texture", action="acquire", target="2", owner="stack")
            + event(4, "texture", action="acquire", target="3", owner="preview")
            + event(5, "texture", action="recycle", target="2")
            + event(6, "page", session=1, page="Recipes", selection="")
            + event(7, "texture", session=1, action="destroy", target="3"))
        result = self.report(self.root / "beef-trace-10.jsonl")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("crossing session 0 -> 1: 2 (stack: 1, preview: 1)",
                      result.stdout)
        self.assertIn("at end of trace: 1 (stack: 1)", result.stdout)

    def test_single_unrotated_file_reports_no_rotation(self):
        (self.root / "beef-trace-8.jsonl").write_text(
            event(1, "startup", build="b2", source_sha256="s")
            + event(2, "page", page="Recipes", selection=""))
        result = self.report(self.root / "beef-trace-8.jsonl")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Segments read: 1; rotations seen: 0", result.stdout)
        self.assertIn("Build: b2", result.stdout)
        self.assertNotIn("mid-run", result.stdout)


if __name__ == "__main__":
    unittest.main()
