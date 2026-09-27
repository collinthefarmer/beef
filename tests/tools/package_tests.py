"""Exercise the CMake packaging script with a small spec and no Windows toolchain."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]


class PackageTests(unittest.TestCase):
    def setUp(self) -> None:
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name)
        inputs = {'plugin.dll': b'binary', 'plugin.pdb': b'symbols', 'LICENSE': b'terms',
                  'notice.txt': b'notice'}
        for name, data in inputs.items():
            (self.root / name).write_bytes(data)
        (self.root / 'identity.json').write_text(json.dumps({'build': 'abc-Release'}))
        self.spec = {
            'name': 'Example-0.1.0-profile', 'identity': str(self.root / 'identity.json'),
            'files': [{'source': str(self.root / 'plugin.dll'), 'destination': 'SKSE/Plugins/Example.dll'}],
            'notices': [{'source': str(self.root / 'LICENSE'), 'destination': 'LICENSE'},
                        {'source': str(self.root / 'notice.txt'), 'destination': 'licenses/notice.txt'}],
            'symbols': [{'source': str(self.root / 'plugin.pdb'), 'destination': 'Example.pdb'}]}
        self.output = self.root / 'archives'

    def package(self) -> subprocess.CompletedProcess[str]:
        (self.root / 'spec.json').write_text(json.dumps(self.spec))
        return subprocess.run(['cmake', f'-DSPEC={self.root / "spec.json"}', f'-DWORK={self.root / "work"}',
                               f'-DOUTPUT={self.output}', '-P', str(ROOT / 'cmake/Package.cmake')],
                              text=True, capture_output=True)

    def outputs(self) -> dict[str, bytes]:
        return {path.name: path.read_bytes() for path in self.output.iterdir()}

    def test_archives_carry_notices_and_one_checksum_file_and_repeat_exactly(self) -> None:
        self.assertEqual(self.package().returncode, 0)
        first = self.outputs()
        self.assertEqual(set(first), {'Example-0.1.0-profile-abc-Release.zip',
                                      'Example-0.1.0-profile-abc-Release-symbols.zip',
                                      'Example-0.1.0-profile-abc-Release.sha256'})
        with zipfile.ZipFile(self.output / 'Example-0.1.0-profile-abc-Release.zip') as mod:
            self.assertEqual(sorted(mod.namelist()),
                             ['LICENSE', 'SKSE/Plugins/Example.dll', 'licenses/notice.txt'])
            self.assertEqual(mod.read('SKSE/Plugins/Example.dll'), b'binary')
        with zipfile.ZipFile(self.output / 'Example-0.1.0-profile-abc-Release-symbols.zip') as symbols:
            self.assertEqual(sorted(symbols.namelist()), ['Example.pdb', 'LICENSE', 'licenses/notice.txt'])
        for line in first['Example-0.1.0-profile-abc-Release.sha256'].decode().splitlines():
            digest, name = line.split('  ')
            self.assertEqual(digest, hashlib.sha256(first[name]).hexdigest())
        self.assertEqual(self.package().returncode, 0)
        self.assertEqual(self.outputs(), first)

    def test_missing_input_or_colliding_destination_fails_and_keeps_archives(self) -> None:
        self.assertEqual(self.package().returncode, 0)
        before = self.outputs()
        (self.root / 'plugin.pdb').unlink()
        self.assertNotEqual(self.package().returncode, 0)
        self.assertEqual(self.outputs(), before)
        (self.root / 'plugin.pdb').write_bytes(b'symbols')
        self.spec['files'].append(dict(self.spec['notices'][0]))
        self.assertIn('Two package entries write LICENSE', self.package().stderr)
        self.assertEqual(self.outputs(), before)


if __name__ == '__main__':
    unittest.main()
