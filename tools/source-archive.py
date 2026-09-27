"""Write the corresponding-source archive: HEAD plus the pinned FetchContent sources."""
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[1]
DEPENDENCIES = {'commonlib': 'commonlibsse', 'spdlog': 'spdlog', 'rapidcsv': 'rapidcsv'}
EXCLUDED = {'commonlibsse': ('Flash', 'tests/REL/version-1-4-15-0.csv',
                             'tests/REL/version-1-5-97-0.bin', 'tests/REL/versionlib-1-6-353-0.bin')}


def git(directory: Path, *args: str) -> bytes:
    result = subprocess.run(['git', '-C', str(directory), *args], capture_output=True, check=False)
    if result.returncode:
        raise ValueError(f'git {" ".join(args)} in {directory}: {result.stderr.decode().strip()}')
    return result.stdout


def add_tar(output: tarfile.TarFile, data: bytes) -> None:
    with tarfile.open(fileobj=io.BytesIO(data)) as source:
        for member in source:
            output.addfile(member, source.extractfile(member) if member.isfile() else None)


def create(profile_name: str, dependencies: Path, output: Path) -> Path:
    if git(ROOT, 'status', '--porcelain'):
        raise ValueError('the tree has uncommitted or untracked files; the archive holds HEAD only')
    revision = git(ROOT, 'rev-parse', 'HEAD').decode().strip()
    profile = json.loads((ROOT / f'cmake/compatibility/{profile_name}.json').read_text())
    cache = ['set(FETCHCONTENT_FULLY_DISCONNECTED ON CACHE BOOL "" FORCE)',
             f'set(BEEF_COMPATIBILITY_PROFILE {profile_name} CACHE STRING "" FORCE)']
    parts = [git(ROOT, 'archive', '--format=tar', 'HEAD')]
    for key, name in DEPENDENCIES.items():
        excluded = [f':(exclude){path}' for path in EXCLUDED.get(name, ())]
        parts.append(git(dependencies / f'{name}-src', 'archive', '--format=tar',
                         f'--prefix=dependencies/{name}/', profile['dependencies'][key], '--', '.',
                         *excluded))
        cache.append(f'set(FETCHCONTENT_SOURCE_DIR_{name.upper()} '
                     f'"${{CMAKE_CURRENT_LIST_DIR}}/dependencies/{name}" CACHE PATH "" FORCE)')
    output.mkdir(parents=True, exist_ok=True)
    archive = output / f'BetterEnchantmentEffects-{profile_name}-{revision[:12]}-source.tar.gz'
    with gzip.GzipFile(archive, 'wb', mtime=0) as compressed, \
            tarfile.open(fileobj=compressed, mode='w', format=tarfile.PAX_FORMAT) as tar:
        for part in parts:
            add_tar(tar, part)
        text = ('\n'.join(cache) + '\n').encode()
        entry = tarfile.TarInfo('bundled-dependencies.cmake')
        entry.size, entry.mode = len(text), 0o644
        tar.addfile(entry, io.BytesIO(text))
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_name(archive.name + '.sha256').write_text(f'{digest}  {archive.name}\n')
    return archive


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', default='steam-1.6.1170')
    parser.add_argument('--dependencies', type=Path, default=ROOT / 'build/Release/_deps')
    parser.add_argument('--output', type=Path, default=ROOT / 'dist/archives')
    args = parser.parse_args()
    try:
        print(create(args.profile, args.dependencies, args.output))
    except (OSError, ValueError, KeyError, tarfile.TarError) as error:
        print(f'source-archive: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
