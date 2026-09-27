"""Build the identity and presenter rules of cmake/Generated.cmake in a small Git project."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class GeneratedTests(unittest.TestCase):
    def setUp(self) -> None:
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name) / 'project'
        (self.root / 'cmake').mkdir(parents=True)
        for name in ('Generated.cmake', 'BuildIdentity.cmake', 'presenter-slot.dds', 'source-revision.txt'):
            shutil.copy2(ROOT / 'cmake' / name, self.root / 'cmake' / name)
        shutil.copy2(ROOT / '.gitattributes', self.root / '.gitattributes')
        (self.root / '.gitignore').write_text('out/\nignored.txt\n')
        (self.root / 'src').mkdir()
        (self.root / 'src/source.cpp').write_text('int source;\n')
        (self.root / 'README.md').write_text('fixture\n')
        (self.root / 'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.24)\nproject(Fixture NONE)\n'
            'add_custom_target(Fixture ALL)\ninclude(cmake/Generated.cmake)\n')
        self.git('init', '-q')
        self.git('add', '.')
        self.git('commit', '-qm', 'fixture')

    def git(self, *args: str) -> str:
        return subprocess.run(['git', '-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
                               *args], cwd=self.root, check=True, capture_output=True, text=True).stdout

    def build(self, source: Path) -> dict[str, str]:
        build = source / 'out'
        for command in (['cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja',
                         '-DCMAKE_BUILD_TYPE=Release', '-DBEEF_COMPATIBILITY_PROFILE=test'],
                        ['cmake', '--build', str(build)]):
            result = subprocess.run(command, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return json.loads((build / 'generated/build-identity.json').read_text())

    def test_identity_follows_git_state_and_rewrites_only_on_change(self) -> None:
        revision = self.git('rev-parse', 'HEAD').strip()
        clean = self.build(self.root)
        self.assertEqual(clean['build'], f'{revision[:12]}-Release')
        self.assertEqual((clean['source'], clean['compatibility_profile']), ('clean', 'test'))
        header = self.root / 'out/generated/BuildIdentity.h'
        written = header.stat().st_mtime_ns
        (self.root / 'ignored.txt').write_text('ignored\n')
        self.assertEqual(self.build(self.root), clean)
        self.assertEqual(header.stat().st_mtime_ns, written)
        (self.root / 'README.md').write_text('edited docs\n')
        (self.root / 'notes.md').write_text('untracked docs\n')
        self.assertEqual(self.build(self.root), clean)
        self.assertEqual(header.stat().st_mtime_ns, written)
        (self.root / 'src/source.cpp').write_text('int changed;\n')
        edited = self.build(self.root)
        self.assertRegex(edited['build'], rf'^{revision[:12]}-dirty-[0-9a-f]{{12}}-Release$')
        (self.root / 'src/untracked.cpp').write_text('int added;\n')
        self.assertNotEqual(self.build(self.root)['source'], edited['source'])
        self.git('add', '.')
        self.git('commit', '-qm', 'next')
        committed = self.build(self.root)
        self.assertEqual(committed['source'], 'clean')
        self.assertNotEqual(committed['revision'], revision)

    def test_git_archive_extraction_builds_as_archive_of_its_revision(self) -> None:
        extracted = self.root.parent / 'extracted'
        extracted.mkdir()
        archive = subprocess.run(['git', 'archive', 'HEAD'], cwd=self.root, check=True,
                                 capture_output=True).stdout
        subprocess.run(['tar', '-x', '-C', str(extracted)], input=archive, check=True)
        identity = self.build(extracted)
        self.assertEqual(identity['revision'], self.git('rev-parse', 'HEAD').strip())
        self.assertEqual(identity['source'], 'archive')
        (extracted / 'cmake/source-revision.txt').write_text('$Format:%H$\n')
        result = subprocess.run(['cmake', '--build', str(extracted / 'out')], text=True, capture_output=True)
        self.assertIn('not stamped by git archive', result.stdout + result.stderr)

    def test_presenter_slots_are_copies_of_the_checked_in_texture(self) -> None:
        self.build(self.root)
        slots = sorted((self.root / 'out/presenters').glob('slot_*.dds'))
        self.assertEqual(len(slots), 1024)
        self.assertEqual({path.read_bytes() for path in slots},
                         {(ROOT / 'cmake/presenter-slot.dds').read_bytes()})
        self.assertTrue((self.root / 'out/presenters/slot_00.dds').exists())


if __name__ == '__main__':
    unittest.main()
