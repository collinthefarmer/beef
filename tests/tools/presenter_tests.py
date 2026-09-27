"""Check the checked-in presenter slot texture and the trace report's presenter counters."""
import json
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class PresenterTests(unittest.TestCase):
    def test_slot_texture_is_a_black_opaque_1x1_rgba_dds(self):
        data = (ROOT / 'cmake/presenter-slot.dds').read_bytes()
        self.assertEqual(len(data), 132)
        self.assertEqual(data[:4], b'DDS ')
        header = struct.unpack('<31I', data[4:128])
        self.assertEqual(header[:7], (124, 0x100F, 1, 1, 4, 0, 0))
        self.assertEqual(header[18:26], (32, 0x41, 0, 32, 0xFF, 0xFF00, 0xFF0000, 0xFF000000))
        self.assertEqual(header[26], 0x1000)
        self.assertEqual(data[128:], bytes([0, 0, 0, 255]))

    def test_report_detects_concurrent_alias_and_ignores_retired_target(self):
        with tempfile.TemporaryDirectory() as folder:
            path = pathlib.Path(folder) / 'trace.jsonl'
            fields = [
                dict(action='acquire', target='1', presenter='a', renderer='r1', current_renderer='r1'),
                dict(action='acquire', target='2', presenter='a', renderer='r2', current_renderer='r1'),
                dict(action='recycle', target='1'),
                dict(action='destroy', target='2'),
                dict(action='acquire', target='3', presenter='a', renderer='r3', current_renderer='r3'),
                dict(action='presenter_rejected', reason='duplicate_presenter'),
                dict(action='lease_rejected', reason='registration_conflict'),
            ]
            path.write_text('\n'.join(json.dumps(dict(event='texture', fields=f)) for f in fields))
            result = subprocess.run([sys.executable, str(ROOT / 'tools/trace-report.py'), str(path)],
                                    check=True, capture_output=True, text=True)
            self.assertIn('Acquisitions aliasing a live presenter: 1', result.stdout)
            self.assertIn('Acquisitions with wrong renderer: 1', result.stdout)
            self.assertIn('Presenter rejections: 1', result.stdout)
            self.assertIn('Texture lease rejections: 1', result.stdout)


if __name__ == '__main__':
    unittest.main()
