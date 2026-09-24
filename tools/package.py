"""Create and verify candidate archives from CMake's explicit file inventory."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import tempfile
import zipfile
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from compatibility import declaration, validate


def digest(data):
    return hashlib.sha256(data).hexdigest()


def verify(path):
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)):
            raise ValueError('duplicate archive entries')
        manifest = json.loads(archive.read('manifest.json'))
        if set(names) != set(manifest['files']) | {'manifest.json'}:
            raise ValueError('archive inventory differs from manifest')
        for name, expected in manifest['files'].items():
            data = archive.read(name)
            if len(data) != expected['bytes'] or digest(data) != expected['sha256']:
                raise ValueError(f'archive content mismatch: {name}')
        if 'compatibility' in manifest:
            profile = validate(manifest['compatibility'])
            if (manifest['identity'].get('compatibility') != profile or
                    json.loads(archive.read('COMPATIBILITY.json')) != profile or
                    manifest['runtime_verified'] is not False or
                    manifest['plugin_declaration'] != declaration(profile, manifest['name'], manifest['version'])):
                raise ValueError('archive compatibility metadata disagrees')
        return manifest


def collect(entries):
    files = {}
    for entry in entries:
        name = entry['destination']
        path = PurePosixPath(name)
        if (not name or path.is_absolute() or '..' in path.parts or
                '\\' in name or ':' in name or str(path) != name or
                name == 'manifest.json' or name.casefold() in
                {key.casefold() for key in files}):
            raise ValueError(f'unsafe or duplicate destination: {name}')
        source = Path(entry['source'])
        if source.is_symlink() or not source.is_file():
            raise ValueError(f'missing or nonregular input: {source}')
        files[name] = source.read_bytes()
    return files


def write_archive(path, files, metadata):
    manifest = dict(metadata, files={name: {'bytes': len(data), 'sha256': digest(data)}
                                    for name, data in sorted(files.items())})
    with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        contents = dict(files, **{'manifest.json': (json.dumps(manifest, indent=2) + '\n').encode()})
        for name, data in sorted(contents.items()):
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data)
    verify(path)


def package(spec_path, output):
    spec = json.loads(spec_path.read_text())
    identity = json.loads(Path(spec['identity']).read_text())
    compatibility = validate(json.loads(Path(spec['compatibility']).read_text()))
    if identity.get('compatibility') != compatibility:
        raise ValueError('build identity and package compatibility differ; rebuild the selected profile')
    actual_declaration = Path(spec['declaration']).read_text()
    if actual_declaration != declaration(compatibility, spec['name'], spec['version']):
        raise ValueError('plugin declaration differs from the selected profile')
    metadata = {'schema': 1, 'name': spec['name'], 'version': spec['version'],
                'identity': identity, 'compatibility': compatibility,
                'plugin_declaration': actual_declaration,
                'runtime_verified': False}
    stem = f"{spec['name']}-{spec['version']}-{compatibility['id']}-{identity['build']}"
    if not all(character.isalnum() or character in '.-_' for character in stem):
        raise ValueError('unsafe archive name')
    if not spec['notices']:
        raise ValueError('missing license notices')
    payload = collect(spec['files'] + spec['notices'])
    symbols = collect(spec['symbols'] + spec['notices'])
    for files in (payload, symbols):
        if 'COMPATIBILITY.json' in files and json.loads(files['COMPATIBILITY.json']) != compatibility:
            raise ValueError('payload compatibility differs from the selected profile')
        files['COMPATIBILITY.json'] = (json.dumps(compatibility, sort_keys=True, indent=2) + '\n').encode()
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output) as folder:
        temporary = Path(folder)
        for suffix, files in (('', payload), ('-symbols', symbols)):
            write_archive(temporary / f'{stem}{suffix}.zip', files,
                          dict(metadata, kind='symbols' if suffix else 'mod'))
        archives = sorted(temporary.glob('*.zip'))
        checksums = ''.join(f'{digest(path.read_bytes())}  {path.name}\n' for path in archives)
        checksum_path = temporary / f'{stem}.sha256'
        checksum_path.write_text(checksums)
        for path in archives + [checksum_path]:
            destination = output / path.name
            path.replace(destination)
            print(destination)


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest='command', required=True)
    create = commands.add_parser('create')
    create.add_argument('spec', type=Path)
    create.add_argument('--output', type=Path, required=True)
    check = commands.add_parser('verify')
    check.add_argument('archive', type=Path)
    args = parser.parse_args()
    if args.command == 'create':
        package(args.spec, args.output)
    else:
        manifest = verify(args.archive)
        print(f"Verified {len(manifest['files'])} files: {args.archive}")


if __name__ == '__main__':
    main()
