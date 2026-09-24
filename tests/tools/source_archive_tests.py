"""Exercise source inventory boundaries, pinned exports, and archive integrity."""
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
SPEC = importlib.util.spec_from_file_location('source_archive', ROOT / 'tools/source-archive.py')
TOOL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TOOL)


class SourceArchiveTests(unittest.TestCase):
    def test_curated_inventory_has_inputs_and_no_capture_or_local_state(self):
        inventory = json.loads((ROOT / 'tools/source-inventory.json').read_text())
        files = TOOL.collect_project(ROOT, inventory['files'])
        for name in files:
            self.assertFalse(any(part in ('.git', '.agents', '.claude', '.codex', '__pycache__',
                                          'build', 'dist', 'checkpoints', 'history')
                                 for part in Path(name).parts), name)
        for folder in ('src', 'cmake', 'templates', 'presets', 'schema', 'licenses'):
            actual = {p.relative_to(ROOT).as_posix() for p in (ROOT / folder).rglob('*') if p.is_file()}
            self.assertTrue(actual <= set(files), actual - set(files))
        self.assertEqual(len([name for name in files if name.startswith('tests/fixtures/efsh/')]), 7)
        self.assertTrue(all('Synthetic' in name for name in files if name.startswith('tests/fixtures/efsh/')))
        self.assertEqual(files['install.sh'][1], 0o755)

    def test_missing_unsafe_duplicate_and_symlink_inputs_fail(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'file').write_text('safe')
            (root / 'link').symlink_to(root / 'file')
            for names in (['missing'], ['../file'], ['/file'], ['a\\b'], ['.git/config'],
                          ['file', 'file'], ['link']):
                with self.subTest(names=names), self.assertRaises(ValueError):
                    TOOL.collect_project(root, names)
            (root / 'FILE').write_text('collision')
            with self.assertRaises(ValueError):
                TOOL.collect_project(root, ['file', 'FILE'])

    def test_pinned_dependency_export_excludes_only_reviewed_entries(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            def git(*args):
                return TOOL.git(root, *args)
            git('init', '-q')
            (root / 'LICENSE').write_text('notice')
            (root / 'source.cpp').write_text('int example;')
            (root / 'captured.bin').write_bytes(b'fixture')
            git('add', '.')
            git('-c', 'user.name=Test', '-c', 'user.email=test@example.invalid', 'commit', '-qm', 'fixture')
            git('tag', 'v1')
            revision = git('rev-parse', 'HEAD').decode().strip()
            spec = dict(revision=revision, pin='v1', exclude=['captured.bin'])
            (root / 'untracked-secret').write_text('must not ship')
            files = TOOL.dependency_files(root, spec)
            self.assertEqual(set(files), {'LICENSE', 'source.cpp'})
            self.assertEqual(files['LICENSE'][0], b'notice')
            with self.assertRaises(ValueError):
                TOOL.dependency_files(root, dict(spec, revision='0' * 40))
            with self.assertRaises(ValueError):
                TOOL.dependency_files(root, dict(spec, exclude=['absent']))
            (root / 'source.cpp').write_text('modified')
            with self.assertRaisesRegex(ValueError, 'tracked modifications'):
                TOOL.dependency_files(root, spec)

    def test_archive_rejects_traversal_duplicates_and_links(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'bad.tar.gz'
            for names, kind in ((['../escape'], tarfile.REGTYPE),
                                (['file', 'file'], tarfile.REGTYPE),
                                (['file'], tarfile.SYMTYPE)):
                with tarfile.open(path, 'w:gz') as archive:
                    for name in names:
                        entry = tarfile.TarInfo(name)
                        entry.type, entry.mode = kind, 0o644
                        archive.addfile(entry, io.BytesIO(b''))
                with self.assertRaises(ValueError):
                    TOOL.read_archive(path)

    def test_manifest_detects_changed_omitted_and_extra_content(self):
        provenance = dict(revision='a' * 40, source_sha256='b' * 64,
                          compatibility_sha256=TOOL.IDENTITY.profile_hash(None))
        files = {'SOURCE_PROVENANCE.json': (json.dumps(provenance).encode(), 0o644),
                 'install.sh': (b'#!/bin/sh\n', 0o755)}
        manifest = dict(schema=1, identity=dict(revision='a' * 40, source_sha256='b' * 64,
                                              compatibility=None),
                        files={name: dict(bytes=len(data), sha256=TOOL.digest(data), mode=mode)
                               for name, (data, mode) in files.items()})
        files['SOURCE_MANIFEST.json'] = (json.dumps(manifest).encode(), 0o644)
        self.assertEqual(TOOL.verify_files(files), manifest)
        for changed in (dict(files, **{'install.sh': (b'changed', 0o755)}),
                        dict(files, **{'install.sh': (b'#!/bin/sh\n', 0o644)}),
                        {k: v for k, v in files.items() if k != 'install.sh'},
                        dict(files, extra=(b'extra', 0o644))):
            with self.assertRaises(ValueError):
                TOOL.verify_files(changed)


if __name__ == '__main__':
    unittest.main()
