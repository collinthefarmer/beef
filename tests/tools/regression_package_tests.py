"""Check the optional quest's record structure and script payload boundary."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('regression_package', ROOT / 'tools/regression-package.py')
package = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package)


class RegressionPackageTests(unittest.TestCase):
    def test_quest_is_manual_and_attaches_runner_without_properties(self):
        records = list(package.demo.records(package.quest_plugin()))
        self.assertEqual([r[0] for r in records], [b'TES4', b'QUST'])
        self.assertEqual(records[1][1], 0x01000800)
        fields = dict(package.demo.subrecords(records[1][3]))
        self.assertEqual(fields[b'EDID'], b'beef_regression\0')
        self.assertEqual(struct.unpack_from('<H', fields[b'DNAM'])[0] & 1, 0)
        vmad = fields[b'VMAD']
        self.assertEqual(struct.unpack_from('<HHH', vmad), (5, 2, 1))
        length = struct.unpack_from('<H', vmad, 6)[0]
        self.assertEqual(vmad[8:8 + length], b'BEEFRegressionRunner')
        self.assertEqual(vmad[8 + length:], b'\0\0\0')

    def test_only_our_compiled_scripts_are_packaged(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for name in ['BEEFRegressionNative', 'BEEFRegressionRunner', 'Actor', 'Quest']:
                (root / (name + '.pex')).write_bytes(bytes.fromhex('fa57c0de03020001') + bytes(20))
            files = package.payload(root)
            self.assertEqual({n for n in files if n.endswith('.pex')},
                             {'Scripts/BEEFRegressionNative.pex', 'Scripts/BEEFRegressionRunner.pex'})
            (root / 'BEEFRegressionRunner.pex').write_bytes(b'not a script')
            with self.assertRaises(ValueError):
                package.payload(root)

    def test_missing_binary_refuses_package(self):
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(FileNotFoundError):
                package.payload(Path(folder))


if __name__ == '__main__':
    unittest.main()
