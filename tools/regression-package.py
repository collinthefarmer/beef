"""Package the opt-in console-driven regression quest and compiled scripts."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('demo_plugin', ROOT / 'tools/demo-plugin.py')
demo = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(demo)
PLUGIN = 'BetterEnchantmentEffectsRegression.esp'


def quest_plugin() -> bytes:
    script = b'BEEFRegressionRunner'
    vmad = struct.pack('<HHHH', 5, 2, 1, len(script)) + script + struct.pack('<BH', 0, 0)
    quest = demo.record(b'QUST', 0x01000800, [
        (b'EDID', demo.text('beef_regression')),
        (b'VMAD', vmad),
        (b'FULL', demo.text('BEEF regression driver')),
        (b'DNAM', struct.pack('<HBB4sI', 0, 0, 0, bytes(4), 0)),
        (b'NEXT', b''),
        (b'ANAM', demo.u32(0)),
    ])
    header = demo.record(b'TES4', 0, [
        (b'HEDR', struct.pack('<fII', 1.7, 2, 0x801)),
        (b'CNAM', demo.text('BetterEnchantmentEffects')),
        (b'MAST', demo.text('Skyrim.esm')), (b'DATA', bytes(8)),
    ])
    return header + demo.group(b'QUST', quest)


def payload(scripts: Path) -> dict[str, bytes]:
    files = {PLUGIN: quest_plugin(),
             'README.md': (ROOT / 'tests/in-game/README.md').read_bytes(),
             'QUICK_REFERENCE.md': (ROOT / 'tests/in-game/QUICK_REFERENCE.md').read_bytes()}
    for name in ('BEEFRegressionNative', 'BEEFRegressionRunner'):
        source = ROOT / 'tests/in-game/Scripts/Source' / (name + '.psc')
        binary = scripts / (name + '.pex')
        data = binary.read_bytes()
        if len(data) < 16 or data[:4] != bytes.fromhex('fa57c0de') or data[6:8] != b'\x00\x01':
            raise ValueError(f'{binary}: expected a Skyrim PEX')
        files['Scripts/' + binary.name] = data
        files['Scripts/Source/' + source.name] = source.read_bytes()
    files['LICENSE'] = (ROOT / 'LICENSE').read_bytes()
    files['COPYING.md'] = (ROOT / 'COPYING.md').read_bytes()
    return files


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scripts', type=Path, default=ROOT / 'build/regression/Scripts')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/regression/BEEF-regression.zip')
    args = parser.parse_args()
    files = payload(args.scripts)
    manifest = {name: hashlib.sha256(data).hexdigest() for name, data in sorted(files.items())}
    files['CONTENTS.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(files.items()):
            archive.writestr(name, data)
    print(args.output)


if __name__ == '__main__':
    main()
