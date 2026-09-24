"""Build and verify a curated source snapshot with pinned dependency sources."""
import argparse
import gzip
import importlib.util
import io
import json
from pathlib import Path, PurePosixPath
import subprocess
import tarfile
import tempfile

from package import digest
from compatibility import validate

SPEC = importlib.util.spec_from_file_location('build_identity', Path(__file__).with_name('build-identity.py'))
IDENTITY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IDENTITY)


def safe_name(name):
    path = PurePosixPath(name)
    if (not name or str(path) != name or path.is_absolute() or '..' in path.parts
            or '\\' in name or ':' in name or '.git' in path.parts):
        raise ValueError(f'unsafe source path: {name}')
    return name


def add_file(files, name, data, mode=0o644):
    safe_name(name)
    if name.casefold() in {key.casefold() for key in files}:
        raise ValueError(f'duplicate source path: {name}')
    files[name] = (data, mode)


def collect_project(root, names):
    files = {}
    for name in names:
        safe_name(name)
        path = root / name
        if (not path.is_file() or not path.resolve().is_relative_to(root.resolve())
                or any(p.is_symlink() for p in (path, *path.parents))):
            raise ValueError(f'missing or nonregular source: {name}')
        add_file(files, name, path.read_bytes(), 0o755 if path.stat().st_mode & 0o111 else 0o644)
    return files


def git(root, *args):
    return subprocess.run(['git', '-C', str(root), *args], check=True, capture_output=True).stdout


def dependency_files(root, spec):
    revision = spec['revision']
    if git(root, 'status', '--porcelain', '--untracked-files=no'):
        raise ValueError(f'dependency has tracked modifications: {root}')
    if git(root, 'rev-parse', 'HEAD').decode().strip() != revision:
        raise ValueError(f'dependency revision mismatch: {root}')
    if git(root, 'rev-parse', spec['pin'] + '^{commit}').decode().strip() != revision:
        raise ValueError(f'dependency pin mismatch: {root}')
    entries = []
    excluded = set(spec['exclude'])
    found = set()
    for row in git(root, 'ls-tree', '-rz', 'HEAD').split(b'\0'):
        if not row:
            continue
        info, name = row.split(b'\t', 1)
        mode, kind, oid = info.decode().split()
        name = name.decode()
        safe_name(name)
        if kind != 'blob' or mode not in ('100644', '100755'):
            raise ValueError(f'unreviewed submodule or special dependency entry: {name}')
        if name in excluded:
            found.add(name)
        else:
            entries.append((name, mode, oid))
    if found != excluded:
        raise ValueError('dependency exclusions differ from pinned tree')
    result = subprocess.run(['git', '-C', str(root), 'cat-file', '--batch'],
                            input=''.join(oid + '\n' for _, _, oid in entries).encode(),
                            capture_output=True, check=True).stdout
    stream = io.BytesIO(result)
    files = {}
    for name, mode, oid in entries:
        actual_oid, kind, length = stream.readline().decode().split()
        if actual_oid != oid or kind != 'blob':
            raise ValueError('dependency blob mismatch')
        data = stream.read(int(length))
        if len(data) != int(length) or stream.read(1) != b'\n':
            raise ValueError('truncated dependency blob')
        add_file(files, name, data, int(mode, 8) & 0o777)
    return files


def read_archive(path):
    files = {}
    with tarfile.open(path, 'r:gz') as archive:
        for member in archive:
            if not member.isfile() or member.mode not in (0o644, 0o755):
                raise ValueError(f'nonregular archive member: {member.name}')
            add_file(files, member.name, archive.extractfile(member).read(), member.mode)
    return files


def verify_files(files):
    manifest = json.loads(files['SOURCE_MANIFEST.json'][0])
    if manifest['schema'] != 1 or set(files) != set(manifest['files']) | {'SOURCE_MANIFEST.json'}:
        raise ValueError('source manifest inventory mismatch')
    for name, expected in manifest['files'].items():
        data, mode = files[name]
        if expected != dict(sha256=digest(data), bytes=len(data), mode=mode):
            raise ValueError(f'source content mismatch: {name}')
    provenance = json.loads(files['SOURCE_PROVENANCE.json'][0])
    identity = manifest['identity']
    if (provenance['revision'] != identity['revision']
            or provenance['source_sha256'] != identity['source_sha256']
            or provenance['compatibility_sha256'] != IDENTITY.profile_hash(identity['compatibility'])):
        raise ValueError('source provenance disagrees with build identity')
    return manifest


def create(root, identity_path, dependency_root, output):
    inventory = json.loads((root / 'tools/source-inventory.json').read_text())
    identity = json.loads(identity_path.read_text())
    profile = validate(identity['compatibility'], root)
    fingerprint, _ = IDENTITY.source_identity(root)
    if not (root / '.git').exists():
        raise ValueError('source archive creation requires the producing Git checkout')
    revision = git(root, 'rev-parse', 'HEAD').decode().strip()
    if identity['source_sha256'] != fingerprint or identity['revision'] != revision:
        raise ValueError('source differs from candidate identity; rebuild before archiving')
    files = collect_project(root, inventory['files'])
    with tempfile.TemporaryDirectory() as folder:
        snapshot = Path(folder)
        for name, (data, mode) in files.items():
            target = snapshot / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        if IDENTITY.source_identity(snapshot) != IDENTITY.source_identity(root):
            raise ValueError('curated inventory omits or changes build identity inputs')
    dependencies = inventory['dependencies']
    if {key: spec['pin'] for key, spec in dependencies.items()} != profile['dependencies']:
        raise ValueError('source dependency pins differ from selected profile')
    cache = ['set(FETCHCONTENT_FULLY_DISCONNECTED ON CACHE BOOL "" FORCE)',
             f'set(BEEF_COMPATIBILITY_PROFILE {profile["id"]} CACHE STRING "" FORCE)']
    for spec in dependencies.values():
        directory = safe_name(spec['directory'])
        for name, (data, mode) in dependency_files(dependency_root / (directory + '-src'), spec).items():
            add_file(files, f'dependencies/{directory}/{name}', data, mode)
        cache.append(f'set(FETCHCONTENT_SOURCE_DIR_{directory.upper()} "${{CMAKE_CURRENT_LIST_DIR}}/dependencies/{directory}" CACHE PATH "" FORCE)')
    add_file(files, 'bundled-dependencies.cmake', ('\n'.join(cache) + '\n').encode())
    provenance = dict(schema=1, revision=revision, source_sha256=fingerprint,
                      compatibility_sha256=IDENTITY.profile_hash(profile))
    add_file(files, 'SOURCE_PROVENANCE.json', (json.dumps(provenance, indent=2) + '\n').encode())
    manifest = dict(schema=1, identity=identity, dependencies=dependencies,
                    files={name: dict(sha256=digest(data), bytes=len(data), mode=mode)
                           for name, (data, mode) in sorted(files.items())})
    add_file(files, 'SOURCE_MANIFEST.json', (json.dumps(manifest, indent=2) + '\n').encode())
    stem = identity['build']
    if not stem or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_' for c in stem):
        raise ValueError('unsafe build identity')
    output.mkdir(parents=True, exist_ok=True)
    destination = output / f'BetterEnchantmentEffects-{profile["id"]}-{stem}-source.tar.gz'
    with tempfile.TemporaryDirectory(dir=output) as folder:
        temporary = Path(folder) / destination.name
        with temporary.open('wb') as raw, gzip.GzipFile(fileobj=raw, mode='wb', mtime=0, filename='') as compressed:
            with tarfile.open(fileobj=compressed, mode='w') as archive:
                for name, (data, mode) in sorted(files.items()):
                    entry = tarfile.TarInfo(name)
                    entry.size, entry.mode = len(data), mode
                    archive.addfile(entry, io.BytesIO(data))
        verify_files(read_archive(temporary))
        temporary.replace(destination)
    destination.with_suffix(destination.suffix + '.sha256').write_text(
        f'{digest(destination.read_bytes())}  {destination.name}\n')
    print(destination)


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest='command', required=True)
    producer = commands.add_parser('create')
    producer.add_argument('--root', type=Path, default=Path.cwd())
    producer.add_argument('--identity', type=Path, required=True)
    producer.add_argument('--dependencies', type=Path, required=True)
    producer.add_argument('--output', type=Path, required=True)
    check = commands.add_parser('verify')
    check.add_argument('archive', type=Path)
    args = parser.parse_args()
    if args.command == 'create':
        create(args.root.resolve(), args.identity, args.dependencies, args.output)
    else:
        manifest = verify_files(read_archive(args.archive))
        print(f"Verified {len(manifest['files'])} source files: {args.archive}")


if __name__ == '__main__':
    main()
