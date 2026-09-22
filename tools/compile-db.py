"""Configure Windows and publish the first-party editor database."""
import json
import os
import subprocess
from pathlib import Path

os.chdir(Path(__file__).resolve().parents[1])
subprocess.run(['cmake', '--preset', 'windows-release'], check=True)
root = Path.cwd()
entries = json.loads(Path('build/Release/compile_commands.json').read_text())
entries = [entry for entry in entries
           if Path(entry['file']).is_relative_to(root / 'src')]
out = Path('build/clangd/compile_commands.json')
out.parent.mkdir(parents=True, exist_ok=True)
text = json.dumps(entries, indent=2) + '\n'
if not out.exists() or out.read_text() != text:
    temporary = out.with_suffix('.tmp')
    temporary.write_text(text)
    temporary.replace(out)
print(f'{len(entries)} entries in {out}')
