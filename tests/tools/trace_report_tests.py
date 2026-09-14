"""Exercise the trace report over rotated segments without the plugin."""
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def event(seq, kind, **fields):
    return json.dumps({"schema": 1, "run": "r", "seq": seq, "event": kind,
                       "fields": fields, "session": 0, "command": 0}) + "\n"


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
