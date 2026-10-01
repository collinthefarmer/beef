"""Write the mod and symbol archives and their checksum file from a package spec."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys

ARCHIVES_SPEC = importlib.util.spec_from_file_location(
    'archives', Path(__file__).resolve().parent / 'archives.py')
archives = importlib.util.module_from_spec(ARCHIVES_SPEC)
ARCHIVES_SPEC.loader.exec_module(archives)


def entries(spec: dict, groups: tuple[str, ...]) -> list[tuple[str, Path]]:
    found: dict[str, Path] = {}
    for group in groups:
        for entry in spec[group]:
            destination = entry['destination']
            if destination in found:
                sys.exit(f'Two package entries write {destination}')
            found[destination] = Path(entry['source'])
    return sorted(found.items())


def contents(files: list[tuple[str, Path]]) -> list[tuple[str, bytes]]:
    return [(destination, source.read_bytes()) for destination, source in files]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--spec', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    spec = json.loads(args.spec.read_text())
    build = json.loads(Path(spec['identity']).read_text())['build']
    stem = f"{spec['name']}-{build}"
    outputs = {f'{stem}.zip': entries(spec, ('files', 'notices')),
                f'{stem}-symbols.zip': entries(spec, ('symbols', 'notices'))}
    shutil.rmtree(args.work, ignore_errors=True)
    args.work.mkdir(parents=True)
    for name, files in outputs.items():
        archives.write_zip(args.work / name, contents(files))
    args.output.mkdir(parents=True, exist_ok=True)
    checksums = ''.join(f'{hashlib.sha256((args.work / name).read_bytes()).hexdigest()}  {name}\n'
                        for name in outputs)
    for name in outputs:
        os.replace(args.work / name, args.output / name)
        print(args.output / name)
    (args.output / f'{stem}.sha256').write_text(checksums)


if __name__ == '__main__':
    try:
        main()
    except (OSError, KeyError, ValueError) as error:
        sys.exit(str(error))
