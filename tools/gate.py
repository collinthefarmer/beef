"""Run repository validation with the tools supplied by the Nix entry point."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
FROZEN = ('src/extern/', 'src/cs/')
LEGAL_NOTICES = (
    ('// GPL-3.0-only with the additional permission in COPYING.md.',),
    ('// Original portions: GPL-3.0-only with permission in COPYING.md.',
     '// Community Shaders-derived portions: see THIRD_PARTY_NOTICES.md and',
     '// licenses/CommunityShaders-EXCEPTIONS.md.'),
)
ENGINE_FREE = ('Core.h', 'recipe', 'mesh', 'planners', 'diagnostics', 'studio', 'regression')
ADAPTER = ENGINE_FREE + ('render', 'PCH.h', 'Identity.h', 'Settings.h', 'SettingsFile.h')
ALLOWS: dict[str, tuple[str, ...]] = {
    'Core.h': (),
    'Identity.h': (),
    'PCH.h': (),
    'Settings.h': (),
    'Settings.cpp': ('Core.h', 'Settings.h'),
    'SettingsPublication.h': ('Settings.h',),
    'SettingsFile.h': ('PCH.h', 'Settings.h'),
    'SettingsFile.cpp': ADAPTER + ('engine', 'SettingsPublication.h'),
    'main.cpp': ADAPTER + ('engine', 'menu', 'BuildIdentity.h', 'BuildCompatibility.h'),
    'recipe': ('Core.h', 'recipe'),
    'mesh': ('Core.h', 'recipe', 'mesh'),
    'planners': ('Core.h', 'recipe', 'mesh', 'planners'),
    'diagnostics': ('Core.h', 'diagnostics'),
    'regression': ('Core.h', 'recipe', 'regression'),
    'studio': ENGINE_FREE,
    'validator': ENGINE_FREE,
    'render': ADAPTER,
    'engine': ADAPTER + ('engine',),
    'menu': ADAPTER + ('engine', 'menu'),
}
ENGINE_SYMBOL = re.compile(r'\b(RE|REL|SKSE)::')
INCLUDE = re.compile(r'\s*#\s*include\s*"([^"]+)"')
RAW_STRING = re.compile(r'(?:u8|[uUL])?R"([^()\\\s]{0,16})\(')


def run(*args: str) -> None:
    subprocess.run(args, check=True)


def first_party(name: str) -> bool:
    return name.startswith(('src/', 'tests/')) and name.endswith(('.cpp', '.h')) \
        and not name.startswith(FROZEN)


def git_names(*args: str) -> list[str]:
    output = subprocess.check_output(['git', *args, '-z', '--', 'src', 'tests'], cwd=ROOT)
    return sorted(name for name in output.decode().split('\0') if first_party(name))


def staged_sources() -> dict[str, str]:
    names = git_names('diff', '--cached', '--name-only', '--diff-filter=ACMR')
    return {name: subprocess.check_output(['git', 'show', f':{name}'], cwd=ROOT).decode()
            for name in names}


def working_sources() -> dict[str, str]:
    names = git_names('ls-files', '--cached', '--others', '--exclude-standard')
    return {name: (ROOT / name).read_text() for name in names if (ROOT / name).is_file()}


def until(text: str, marker: str, start: int) -> int:
    found = text.find(marker, start)
    return len(text) if found < 0 else found


def raw_string(text: str, index: int) -> re.Match[str] | None:
    return None if re.match(r'\w', text[index - 1:index]) else RAW_STRING.match(text, index)


def separates_digits(text: str, index: int) -> bool:
    token = re.search(r"[\w'.]*$", text[max(0, index - 64):index])
    return text[index] == "'" and token is not None and token.group()[:1].isdigit()


def quoted_end(text: str, index: int) -> int:
    quote, index = text[index], index + 1
    while index < len(text) and text[index] not in (quote, '\n'):
        index += 2 if text[index] == '\\' else 1
    return index + 1


def code_and_comments(text: str) -> tuple[str, list[int]]:
    code: list[str] = []
    comments: list[int] = []
    line, index = 1, 0
    while index < len(text):
        start = index
        if text.startswith('//', index):
            comments.append(line)
            index = until(text, '\n', index)
        elif text.startswith('/*', index):
            comments.append(line)
            index = until(text, '*/', index + 2) + 2
        elif raw := raw_string(text, index):
            delimiter = ')' + raw.group(1) + '"'
            index = until(text, delimiter, raw.end()) + len(delimiter)
        elif text[index] in '"\'' and not separates_digits(text, index):
            index = quoted_end(text, index)
        else:
            code.append(text[index])
            line += text[index] == '\n'
            index += 1
            continue
        skipped = text.count('\n', start, index)
        code.append('\n' * skipped)
        line += skipped
    return ''.join(code), comments


def comment_findings(name: str, text: str) -> list[str]:
    lines = text.split('\n')
    notice = next((len(n) for n in LEGAL_NOTICES if tuple(lines[:len(n)]) == n), 0)
    return [f'{name}:{number}: {lines[number - 1]}' for number in code_and_comments(text)[1]
            if number > notice and 'NOLINT' not in lines[number - 1]]


def layer_findings(name: str, text: str) -> list[str]:
    if not name.startswith('src/'):
        return []
    layer = name.split('/')[1]
    allowed = ALLOWS.get(layer)
    if allowed is None:
        return [f'{name}: {layer} has no row in the layer graph in tools/gate.py']
    findings = []
    code = code_and_comments(text)[0].split('\n')
    for number, (line, stripped) in enumerate(zip(text.split('\n'), code), 1):
        include = INCLUDE.match(line) if stripped.lstrip().startswith('#') else None
        if include and include.group(1).split('/')[0] not in allowed:
            findings.append(f'{name}:{number}: {layer} may not include "{include.group(1)}"')
        if layer in ENGINE_FREE and ENGINE_SYMBOL.search(stripped):
            findings.append(f'{name}:{number}: engine-free code names an engine symbol')
    return findings


def format_findings(sources: dict[str, str]) -> list[str]:
    binary = os.environ.get('CLANG_FORMAT', 'clang-format')

    def check(item: tuple[str, str]) -> str | None:
        result = subprocess.run([binary, f'--assume-filename={item[0]}', '--dry-run', '-Werror'],
                                input=item[1], text=True, capture_output=True, cwd=ROOT)
        return None if result.returncode == 0 else f'{item[0]}: needs clang-format'

    with ThreadPoolExecutor(max_workers=4) as pool:
        return [finding for finding in pool.map(check, sources.items()) if finding]


def check_sources(sources: dict[str, str]) -> None:
    findings = format_findings(sources)
    for name, text in sources.items():
        findings += layer_findings(name, text) + comment_findings(name, text)
    if findings:
        sys.exit('\n'.join(findings) + '\nFormat with tools/gate.sh fix; move C++ prose comments '
                 'to REFERENCE.md; widen a layer only by editing ALLOWS in tools/gate.py.')
    print(f'gate: {len(sources)} files formatted, layered and free of prose comments')


ZERO_COMMIT = '0' * 40
GATE_SOURCES = ('src', 'tests', 'tools', 'cmake', 'CMakeLists.txt', 'CMakePresets.json')


def git_text(*args: str) -> str:
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()


def passed_record() -> Path:
    path = Path(git_text('rev-parse', '--git-path', 'beef-gate-passed'))
    return path if path.is_absolute() else ROOT / path


def tree_matches_head() -> bool:
    tracked = git_text('status', '--porcelain', '--untracked-files=no')
    untracked = git_text('status', '--porcelain', '--', *GATE_SOURCES)
    return not tracked and not untracked


def record_pass() -> None:
    if not tree_matches_head():
        print('gate: the working tree differs from HEAD, so this pass is not recorded for a push')
        return
    head = git_text('rev-parse', 'HEAD')
    passed_record().write_text(head + '\n')
    print(f'gate: sanitized tests passed for {head[:12]}; git push may proceed')


def pushed_commits(lines: list[str]) -> list[str]:
    commits = []
    for line in lines:
        fields = line.split()
        if len(fields) == 4 and fields[1] != ZERO_COMMIT:
            commits.append(fields[1])
    return commits


def check_push_recorded(commits: list[str]) -> None:
    record = passed_record()
    recorded = record.read_text().strip() if record.is_file() else ''
    unchecked = [commit for commit in commits if commit != recorded]
    if unchecked:
        sys.exit(f'Push blocked: the sanitized gate has not passed for {unchecked[0][:12]}. '
                 'Push with tools/gate.sh ship, which runs the gate and then pushes.')


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('stage', choices=('commit', 'push', 'prepush', 'ship', 'release', 'fix'))
    parser.add_argument('push_args', nargs=argparse.REMAINDER)
    arguments = parser.parse_args()
    stage = arguments.stage
    os.chdir(ROOT)
    if not os.environ.get('BEEF_DEV_SHELL'):
        sys.exit('Run tools/gate.sh to enter the pinned Nix environment.')
    if stage == 'fix':
        run(os.environ.get('CLANG_FORMAT', 'clang-format'), '-i', *working_sources())
        return
    if stage == 'ship' and not tree_matches_head():
        sys.exit('Ship blocked: commit or stash changes first; the gate tests the working tree.')
    check_sources(staged_sources() if stage == 'commit' else working_sources())
    if stage == 'commit':
        return
    if stage == 'prepush':
        check_push_recorded(pushed_commits(sys.stdin.read().splitlines()))
        return
    run('cmake', '--preset', 'native-sanitized')
    run('cmake', '--build', '--preset', 'native-sanitized')
    run('ctest', '--preset', 'native-sanitized')
    record_pass()
    if stage == 'ship':
        run('git', 'push', *arguments.push_args)
    if stage == 'release':
        run('cmake', '--preset', 'windows-release')
        run('cmake', '--build', '--preset', 'windows-release', '--target', 'all')
        run(sys.executable, 'tools/tidy.py', '--check')


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
    except (OSError, UnicodeDecodeError) as error:
        sys.exit(str(error))
