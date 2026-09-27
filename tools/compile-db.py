"""Configure Windows and publish the first-party editor database, with native test entries."""
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

if shutil.which('cmake') is None:
    sys.exit("cmake is not on PATH; run 'nix develop' first")
os.chdir(Path(__file__).resolve().parents[1])
subprocess.run(['cmake', '--preset', 'windows-release'], check=True)
root = Path.cwd()


def entries_under(database: Path, folder: str) -> list[dict[str, str]]:
    return [entry for entry in json.loads(database.read_text())
            if (Path(entry['directory']) / entry['file']).resolve().is_relative_to(root / folder)]


entries = entries_under(Path('build/Release/compile_commands.json'), 'src')
native = Path('build/native/compile_commands.json')
if native.exists():
    entries += entries_under(native, 'tests')
    print(f'tests/ entries from {native}')
else:
    print(f'no {native}; run cmake --preset native so renames reach tests/')
out = Path('build/clangd/compile_commands.json')
out.parent.mkdir(parents=True, exist_ok=True)
text = json.dumps(entries, indent=2) + '\n'
if not out.exists() or out.read_text() != text:
    temporary = out.with_suffix('.tmp')
    temporary.write_text(text)
    temporary.replace(out)
print(f'{len(entries)} entries in {out}')
