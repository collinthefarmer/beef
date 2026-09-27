"""Compile only the regression scripts with Caprica in Skyrim mode."""
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def compiler_path(path: Path, windows: bool) -> str:
    path = path.resolve()
    if windows and sys.platform != 'win32':
        return subprocess.check_output(['wslpath', '-w', str(path)], text=True).strip()
    return str(path)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/regression/Scripts')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    windows = args.compiler.suffix.lower() == '.exe'
    fixture = ROOT / 'tests/in-game'
    sources = fixture / 'Scripts/Source'
    path = lambda value: compiler_path(value, windows)
    subprocess.run([str(args.compiler.resolve()), '--ignorecwd', '--game=skyrim',
                    '--flags', path(fixture / 'TESV_Papyrus_Flags.flg'),
                    '--import', path(fixture / 'imports'), '--import', path(sources),
                    '--output', path(args.output), path(sources)], check=True)
    for name in ('BEEFRegressionNative', 'BEEFRegressionRunner'):
        result = args.output / (name + '.pex')
        if not result.is_file() or result.stat().st_size < 16:
            raise ValueError(f'Compiler did not produce {result}')
        print(result)


if __name__ == '__main__':
    main()
