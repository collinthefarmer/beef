#!/usr/bin/env python3
"""Package a local demo with the complete framework runtime manifest."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile


def inputs(manifest, fixture, recipes, readme):
    files = {}
    for entry in manifest['files'] + manifest['notices']:
        destination = entry['destination']
        if destination == 'README.md':
            destination = 'FRAMEWORK_README.md'
        name = PurePosixPath(destination)
        if name.is_absolute() or '..' in name.parts or '\\' in destination:
            raise ValueError(f'Unsafe destination: {destination}')
        if destination in files:
            raise ValueError(f'Duplicate destination: {destination}')
        files[destination] = Path(entry['source'])
    required = {'SKSE/Plugins/BetterEnchantmentEffects.dll',
                'textures/BetterEnchantmentEffects/slots/slot_00.dds',
                'SKSE/Plugins/BetterEnchantmentEffects/templates/fill.json',
                'SKSE/Plugins/BetterEnchantmentEffects/templates/bare.json'}
    if not required <= files.keys():
        raise ValueError('Framework manifest is missing required rendering inputs')
    files.update({
        'BetterEnchantmentEffectsDemo.esp': fixture / 'BetterEnchantmentEffectsDemo.esp',
        'records.json': fixture / 'records.json',
        'README.md': readme,
    })
    for recipe in recipes:
        destination = f'SKSE/Plugins/BetterEnchantmentEffects/recipes/examples/{recipe.name}'
        if recipe.suffix != '.json' or destination in files:
            raise ValueError(f'Invalid or duplicate recipe: {recipe.name}')
        files[destination] = recipe
    for destination, source in files.items():
        if not source.is_file():
            raise ValueError(f'Missing input for {destination}: {source}')
    return files


def package(files, output):
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix('.tmp')
    hashes = {name: hashlib.sha256(path.read_bytes()).hexdigest()
              for name, path in files.items()}
    with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, path in sorted(files.items()):
            archive.write(path, name)
        archive.writestr('CONTENTS.json', json.dumps(hashes, indent=2) + '\n')
    with zipfile.ZipFile(temporary) as archive:
        if archive.testzip() is not None:
            raise ValueError('Archive integrity failure')
        for name, expected in hashes.items():
            if hashlib.sha256(archive.read(name)).hexdigest() != expected:
                raise ValueError(f'Archive content mismatch: {name}')
    temporary.replace(output)
    return len(files)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--recipe', type=Path, action='append', required=True)
    parser.add_argument('--readme', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    files = inputs(json.loads(args.manifest.read_text()), args.fixture,
                   args.recipe, args.readme)
    count = package(files, args.output)
    print(f'{args.output}: {count} files plus verified CONTENTS.json')


if __name__ == '__main__':
    main()
