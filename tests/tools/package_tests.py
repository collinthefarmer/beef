"""Exercise candidate packaging without a game or Windows toolchain."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('packaging_tool', ROOT / 'tools/package.py')
TOOL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TOOL)


class PackageTests(unittest.TestCase):
    def fixture(self, root):
        identity = root / 'identity.json'
        profile = json.loads((ROOT / 'cmake/compatibility/steam-1.6.1170.json').read_text())
        compatibility = root / 'compatibility.json'
        compatibility.write_text(json.dumps(profile))
        identity.write_text(json.dumps({'build': 'abc-123-Release', 'compatibility': profile}))
        declaration = root / 'Plugin.cpp'
        declaration.write_text(TOOL.declaration(profile, 'Example', '0.1.0'))
        dll = root / 'plugin.dll'
        dll.write_bytes(b'candidate binary')
        pdb = root / 'plugin.pdb'
        pdb.write_bytes(b'symbols')
        license_file = root / 'LICENSE'
        license_file.write_bytes(b'license terms')
        spec = {'name': 'Example', 'version': '0.1.0', 'identity': str(identity),
                'declaration': str(declaration), 'compatibility': str(compatibility),
                'files': [{'source': str(dll), 'destination': 'SKSE/Plugins/Example.dll'}],
                'symbols': [{'source': str(pdb), 'destination': 'Example.pdb'}],
                'notices': [{'source': str(license_file), 'destination': 'LICENSE'}]}
        path = root / 'spec.json'
        path.write_text(json.dumps(spec))
        return path, spec

    def test_inventory_symbols_hashes_and_repeatability(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            path, spec = self.fixture(root)
            output = root / 'archives'
            output.mkdir()
            (output / 'stale.dll').write_bytes(b'obsolete')
            TOOL.package(path, output)
            archives = sorted(output.glob('*.zip'))
            self.assertEqual(len(archives), 2)
            before = {item.name: item.read_bytes() for item in archives}
            for archive in archives:
                manifest = TOOL.verify(archive)
                expected = 'symbols' if archive.stem.endswith('-symbols') else 'mod'
                self.assertEqual(manifest['kind'], expected)
                self.assertFalse(manifest['runtime_verified'])
                entries = spec['symbols'] if expected == 'symbols' else spec['files']
                entries = entries + spec['notices']
                self.assertEqual(set(manifest['files']), {entry['destination'] for entry in entries} | {'COMPATIBILITY.json'})
                with zipfile.ZipFile(archive) as contents:
                    self.assertEqual(contents.read('LICENSE'), b'license terms')
            TOOL.package(path, output)
            self.assertEqual(before, {item.name: item.read_bytes() for item in archives})
            for line in next(output.glob('*.sha256')).read_text().splitlines():
                checksum, name = line.split('  ')
                self.assertEqual(checksum, TOOL.digest((output / name).read_bytes()))

    def test_removed_asset_does_not_survive_repackaging(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            path, spec = self.fixture(root)
            output = root / 'archives'
            extra = dict(spec['files'][0], destination='obsolete.dds')
            spec['files'].append(extra)
            path.write_text(json.dumps(spec))
            TOOL.package(path, output)
            spec['files'].pop()
            path.write_text(json.dumps(spec))
            TOOL.package(path, output)
            archive = next(item for item in output.glob('*.zip') if not item.stem.endswith('-symbols'))
            self.assertNotIn('obsolete.dds', TOOL.verify(archive)['files'])

    def test_refuses_missing_inputs_without_replacing_archive(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            path, spec = self.fixture(root)
            output = root / 'archives'
            TOOL.package(path, output)
            before = {item.name: item.read_bytes() for item in output.iterdir()}
            Path(spec['symbols'][0]['source']).unlink()
            with self.assertRaises(ValueError):
                TOOL.package(path, output)
            self.assertEqual(before, {item.name: item.read_bytes() for item in output.iterdir()})

    def test_rejects_profile_and_declaration_drift_without_replacing_archives(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            path, spec = self.fixture(root)
            output = root / 'archives'
            TOOL.package(path, output)
            before = {item.name: item.read_bytes() for item in output.iterdir()}
            for key in ('identity', 'declaration', 'compatibility'):
                file = Path(spec[key])
                original = file.read_text()
                if key == 'declaration':
                    file.write_text(original.replace('1170', '640'))
                else:
                    data = json.loads(original)
                    target = data['compatibility'] if key == 'identity' else data
                    target['minimum_skse'] = [2, 2, 7, 0]
                    file.write_text(json.dumps(data))
                with self.subTest(key=key), self.assertRaises(ValueError):
                    TOOL.package(path, output)
                self.assertEqual(before, {item.name: item.read_bytes() for item in output.iterdir()})
                file.write_text(original)

    def test_rejects_unsafe_and_case_duplicate_destinations(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / 'input'
            source.write_bytes(b'data')
            for name in ('../escape', '/absolute', 'a\\b', 'C:/file', 'manifest.json', 'a//b'):
                with self.subTest(name=name), self.assertRaises(ValueError):
                    TOOL.collect([{'source': str(source), 'destination': name}])
            with self.assertRaises(ValueError):
                TOOL.collect([{'source': str(source), 'destination': name} for name in ('a.dll', 'A.dll')])

    def test_refuses_missing_or_conflicting_notices_without_replacing_archives(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            path, spec = self.fixture(root)
            output = root / 'archives'
            TOOL.package(path, output)
            before = {item.name: item.read_bytes() for item in output.iterdir()}
            notice = spec['notices'][0]
            for notices in ([], [dict(notice, source=str(root / 'missing'))],
                            [dict(notice, destination=spec['files'][0]['destination'])],
                            [dict(notice, destination=spec['symbols'][0]['destination'])]):
                with self.subTest(notices=notices):
                    path.write_text(json.dumps(dict(spec, notices=notices)))
                    with self.assertRaises(ValueError):
                        TOOL.package(path, output)
                    self.assertEqual(before, {item.name: item.read_bytes() for item in output.iterdir()})

    def test_verifier_rejects_rehashed_profile_disagreement(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            path, spec = self.fixture(root)
            output = root / 'archives'
            TOOL.package(path, output)
            archive = next(output.glob('*.zip'))
            with zipfile.ZipFile(archive) as source:
                metadata = json.loads(source.read('manifest.json'))
                files = {name: source.read(name) for name in metadata.pop('files')}
            profile = json.loads(files['COMPATIBILITY.json'])
            profile['minimum_skse'] = [2, 2, 7, 0]
            files['COMPATIBILITY.json'] = json.dumps(profile).encode()
            with self.assertRaisesRegex(ValueError, 'metadata disagrees'):
                TOOL.write_archive(root / 'inconsistent.zip', files, metadata)

    def test_verifier_detects_tampering_and_extra_files(self):
        with tempfile.TemporaryDirectory() as folder:
            archive = Path(folder) / 'test.zip'
            TOOL.write_archive(archive, {'file': b'original'}, {})
            with zipfile.ZipFile(archive) as source:
                manifest = source.read('manifest.json')
            with zipfile.ZipFile(archive, 'w') as destination:
                destination.writestr('manifest.json', manifest)
                destination.writestr('file', b'changed')
            with self.assertRaises(ValueError):
                TOOL.verify(archive)
            TOOL.write_archive(archive, {'file': b'original'}, {})
            with zipfile.ZipFile(archive, 'a') as destination:
                destination.writestr('stale', b'unlisted')
            with self.assertRaises(ValueError):
                TOOL.verify(archive)


if __name__ == '__main__':
    unittest.main()
