"""Build a source archive from a temporary Git project and pinned dependency checkouts."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def git(directory: Path, *args: str) -> str:
    return subprocess.run(['git', '-C', str(directory), '-c', 'user.name=Test',
                           '-c', 'user.email=test@example.invalid', *args],
                          check=True, capture_output=True, text=True).stdout.strip()


def repository(directory: Path, files: dict[str, str]) -> None:
    for name, text in files.items():
        (directory / name).parent.mkdir(parents=True, exist_ok=True)
        (directory / name).write_text(text)
    git(directory, 'init', '-q')
    git(directory, 'add', '.')
    git(directory, 'commit', '-qm', 'fixture')


class SourceArchiveTests(unittest.TestCase):
    def setUp(self) -> None:
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name) / 'project'
        self.deps = Path(folder.name) / 'deps'
        pins = {}
        for key, name in (('commonlib', 'commonlibsse'), ('spdlog', 'spdlog'), ('rapidcsv', 'rapidcsv')):
            checkout = self.deps / f'{name}-src'
            repository(checkout, {'LICENSE': name, 'Flash/CLIK.as': 'proprietary',
                                  'tests/REL/version-1-5-97-0.bin': 'data'})
            git(checkout, 'tag', 'v1.0')
            (checkout / 'LICENSE').write_text('edited after the pin')
            (checkout / 'untracked.txt').write_text('not source')
            pins[key] = 'v1.0'
        (self.root / 'tools').mkdir(parents=True)
        shutil.copy2(ROOT / 'tools/source-archive.py', self.root / 'tools/source-archive.py')
        repository(self.root, {
            '.gitattributes': (ROOT / '.gitattributes').read_text(),
            'cmake/source-revision.txt': '$Format:%H$\n',
            'cmake/compatibility/test.json': json.dumps({'dependencies': pins}),
            'docs/checkpoints/captured.log': 'runtime capture',
            'src/main.cpp': 'int main() {}\n'})

    def archive(self) -> subprocess.CompletedProcess[str]:
        return subprocess.run([sys.executable, str(self.root / 'tools/source-archive.py'), '--profile', 'test',
                               '--dependencies', str(self.deps), '--output', str(self.root.parent / 'out')],
                              text=True, capture_output=True)

    def test_archive_holds_head_and_pinned_dependencies(self) -> None:
        result = self.archive()
        self.assertEqual(result.returncode, 0, result.stderr)
        path = Path(result.stdout.strip())
        revision = git(self.root, 'rev-parse', 'HEAD')
        self.assertEqual(path.name, f'BetterEnchantmentEffects-test-{revision[:12]}-source.tar.gz')
        with tarfile.open(path) as archive:
            names = {member.name for member in archive if member.isfile()}
            read = lambda name: archive.extractfile(name).read().decode()
            self.assertEqual(read('cmake/source-revision.txt'), revision + '\n')
            self.assertEqual(read('dependencies/spdlog/LICENSE'), 'spdlog')
            bundled = read('bundled-dependencies.cmake')
        self.assertIn('src/main.cpp', names)
        self.assertNotIn('docs/checkpoints/captured.log', names)
        self.assertIn('dependencies/spdlog/Flash/CLIK.as', names)
        self.assertFalse({'dependencies/commonlibsse/Flash/CLIK.as',
                          'dependencies/commonlibsse/tests/REL/version-1-5-97-0.bin'} & names)
        self.assertFalse(any(name.endswith('untracked.txt') for name in names))
        self.assertIn('set(FETCHCONTENT_FULLY_DISCONNECTED ON', bundled)
        self.assertIn('set(FETCHCONTENT_SOURCE_DIR_COMMONLIBSSE '
                      '"${CMAKE_CURRENT_LIST_DIR}/dependencies/commonlibsse"', bundled)
        checksum = path.with_name(path.name + '.sha256').read_text()
        self.assertEqual(checksum, f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n')

    def test_dirty_tree_or_absent_pin_is_refused_with_a_message(self) -> None:
        (self.root / 'src/extra.cpp').write_text('int extra;\n')
        result = self.archive()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('uncommitted or untracked', result.stderr)
        (self.root / 'src/extra.cpp').unlink()
        git(self.deps / 'rapidcsv-src', 'tag', '-d', 'v1.0')
        result = self.archive()
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn('Traceback', result.stderr)


if __name__ == '__main__':
    unittest.main()
