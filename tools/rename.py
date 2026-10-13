#!/usr/bin/env python3
"""Rename C++ symbols across the tree through clangd.

    tools/rename.py Old New [--kind Class] [--apply]
    tools/rename.py src/studio/Panels.cpp:40:12 New [--apply]

Old is a symbol name, qualified when several symbols share it
(Studio::SlotRow), or a path:line:column position for a symbol the index
does not list, such as a local or a parameter; the leftover search then
covers only the edited file. clangd reports a struct as a Class. Run one
rename at a time: clangd's index keys references by the names in a
symbol's scope and signature, so a second rename in the same session
misses references that the first rename changed.

Without --apply the tool prints the diff and writes nothing. With --apply
it writes the changed files only after every edit applies. clang-format
runs on each changed C++ file that was clang-format clean before. Both
modes list the places that still spell an old name afterwards: comments,
strings, docs, files outside the compile database, or another symbol with
the same name.

Needs build/clangd/compile_commands.json (python3 tools/compile-db.py
writes it) and clangd and clang-format on PATH, so run it inside
`nix develop`. The first run builds clangd's background index under
build/clangd/.cache/clangd, which takes a few minutes; later runs reuse it.
"""
import argparse
from collections import deque
from dataclasses import dataclass, replace
import difflib
from enum import Enum
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import threading
import time
from typing import BinaryIO
from urllib.parse import unquote, urlparse

ROOT = Path(__file__).resolve().parents[1]
COMPILE_DB = ROOT / 'build/clangd/compile_commands.json'
WARM_UP_DOCUMENT = Path('src/rename-warm-up.cpp')
UNTOUCHABLE = ('.claude/', 'build/', 'decompiled/', 'reference/', 'src/extern/')
UNFORMATTED = ('src/extern/', 'src/cs/')
LEFTOVER_EXCLUDES = ('.claude', 'decompiled', 'reference', 'docs/history', 'docs/checkpoints', 'src/extern')
CPP_SUFFIXES = ('.h', '.hpp', '.cpp')
IDENTIFIER = re.compile(r'[A-Za-z_]\w*')
POSITION = re.compile(r'(.+):(\d+):(\d+)')
LINE_BREAK = re.compile(r'\r\n|\r|\n')
INDEX_PROGRESS_TOKEN = 'backgroundIndexProgress'
QUIET_SPELL_SECONDS = 15.0
INDEX_DEADLINE_SECONDS = 900.0
RENAME_TIMEOUT_SECONDS = 300.0
LEFTOVER_LINES_SHOWN = 40
SYMBOL_KINDS = {
    2: 'Module', 3: 'Namespace', 5: 'Class', 6: 'Method', 7: 'Property', 8: 'Field', 9: 'Constructor',
    10: 'Enum', 11: 'Interface', 12: 'Function', 13: 'Variable', 14: 'Constant', 22: 'EnumMember',
    23: 'Struct', 26: 'TypeParameter',
}

JsonObject = dict[str, object]


class RenameError(Exception):
    pass


@dataclass(frozen=True)
class Position:
    line: int
    character: int


@dataclass(frozen=True)
class ByName:
    qualifier: tuple[str, ...]
    name: str
    kind: str | None


@dataclass(frozen=True)
class ByPosition:
    path: Path
    position: Position


Target = ByName | ByPosition


@dataclass(frozen=True)
class RenameRequest:
    target: Target
    new: str
    where: str


@dataclass(frozen=True)
class Symbol:
    name: str
    container: str
    kind: str
    path: Path
    position: Position


@dataclass(frozen=True)
class Declaration:
    path: Path
    position: Position


@dataclass(frozen=True)
class TextEdit:
    start: Position
    end: Position
    text: str


@dataclass(frozen=True)
class RenameResult:
    symbol: Symbol
    edit_count: int
    texts: dict[Path, str]


@dataclass(frozen=True)
class FileChange:
    path: Path
    before: str
    after: str


@dataclass(frozen=True)
class Leftover:
    path: Path
    line: int
    text: str


class Indexing(Enum):
    UNSEEN = 'unseen'
    RUNNING = 'running'
    DONE = 'done'


def expect[T](value: object, kind: type[T], what: str) -> T:
    if not isinstance(value, kind):
        raise RenameError(f'clangd sent a malformed {what}')
    return value


def at(where: str, message: str) -> str:
    return f'{where}: {message}' if where else message


def repo_path(path: Path) -> Path | None:
    if not path.is_relative_to(ROOT):
        return None
    relative = path.relative_to(ROOT)
    return None if relative.as_posix().startswith(UNTOUCHABLE) else relative


def path_of_uri(uri: str) -> Path:
    parsed = urlparse(uri)
    if parsed.scheme != 'file':
        raise RenameError(f'clangd named a non-file location {uri}')
    return Path(unquote(parsed.path))


def uri_of(path: Path) -> str:
    return (ROOT / path).as_uri()


def parse_identifier(text: str, where: str) -> str:
    if not IDENTIFIER.fullmatch(text):
        raise RenameError(at(where, f'{text!r} is not a C++ identifier'))
    return text


def parse_qualified(text: str, where: str) -> tuple[tuple[str, ...], str]:
    names = tuple(parse_identifier(part, where) for part in text.removeprefix('::').split('::'))
    return names[:-1], names[-1]


def parse_target(text: str, kind: str | None, where: str, base: Path) -> Target:
    match = POSITION.fullmatch(text)
    if match is None:
        if kind is not None and kind not in SYMBOL_KINDS.values():
            raise RenameError(at(where, f'unknown kind {kind}; use one of {", ".join(sorted(SYMBOL_KINDS.values()))}'))
        qualifier, name = parse_qualified(text, where)
        return ByName(qualifier, name, kind)
    if kind is not None:
        raise RenameError(at(where, 'a kind picks among symbols found by name; a position already names one'))
    path = repo_path((base / match[1]).resolve())
    if path is None:
        raise RenameError(at(where, f'{match[1]} is outside the files this tool may change'))
    line, column = int(match[2]), int(match[3])
    if line < 1 or column < 1:
        raise RenameError(at(where, 'lines and columns count from 1'))
    return ByPosition(path, Position(line - 1, column - 1))


def parse_request(old: str, new: str, kind: str | None, where: str, base: Path) -> RenameRequest:
    target = parse_target(old, kind, where, base)
    new_qualifier, new_name = parse_qualified(new, where)
    if new_qualifier and (not isinstance(target, ByName) or new_qualifier != target.qualifier):
        raise RenameError(at(where, f'{new} moves the symbol to another scope; clangd only renames in place'))
    return RenameRequest(target, new_name, where)


def display_name(target: ByName) -> str:
    return '::'.join((*target.qualifier, target.name))


def describe(symbol: Symbol) -> str:
    qualified = f'{symbol.container}::{symbol.name}' if symbol.container else symbol.name
    return f'{symbol.kind} {qualified} at {symbol.path}:{symbol.position.line + 1}'


def position_from_lsp(value: object) -> Position:
    position = expect(value, dict, 'position')
    return Position(expect(position.get('line'), int, 'position line'),
                    expect(position.get('character'), int, 'position character'))


def symbol_from_lsp(value: object) -> Symbol | None:
    entry = expect(value, dict, 'symbol')
    location = expect(entry.get('location'), dict, 'symbol location')
    path = repo_path(path_of_uri(expect(location.get('uri'), str, 'symbol uri')))
    if path is None:
        return None
    return Symbol(name=expect(entry.get('name'), str, 'symbol name'),
                  container=expect(entry.get('containerName', ''), str, 'symbol container'),
                  kind=SYMBOL_KINDS.get(expect(entry.get('kind'), int, 'symbol kind'), 'Other'),
                  path=path,
                  position=position_from_lsp(expect(location.get('range'), dict, 'symbol range').get('start')))


def matches(symbol: Symbol, target: ByName) -> bool:
    wanted = '::'.join(target.qualifier)
    return (symbol.name == target.name
            and (not wanted or symbol.container == wanted or symbol.container.endswith('::' + wanted))
            and (target.kind is None or symbol.kind == target.kind))


def choose_symbol(symbols: list[Symbol], target: ByName, where: str) -> Symbol:
    found = list(dict.fromkeys(symbol for symbol in symbols if matches(symbol, target)))
    if not found and target.kind is not None:
        kinds = sorted({symbol.kind for symbol in symbols if matches(symbol, replace(target, kind=None))})
        if kinds:
            raise RenameError(at(where, f'{display_name(target)} is a {" or ".join(kinds)}, not a {target.kind}'))
    if not found:
        raise RenameError(at(where, f'no symbol named {display_name(target)} in the index'))
    if len(found) > 1:
        listing = '\n'.join('  ' + describe(symbol) for symbol in found)
        raise RenameError(at(where, f'{len(found)} symbols match {display_name(target)}; '
                                    f'qualify the name or give a kind:\n{listing}'))
    return found[0]


def declarations_from_lsp(value: object) -> list[Declaration]:
    declarations = []
    for entry in expect(value if value is not None else [], list, 'symbol info'):
        found = expect(entry, dict, 'symbol details').get('declarationRange')
        if found is None:
            continue
        location = expect(found, dict, 'declaration range')
        declarations.append(Declaration(path_of_uri(expect(location.get('uri'), str, 'declaration uri')),
                                        position_from_lsp(expect(location.get('range'), dict, 'declaration range').get('start'))))
    return declarations


def is_alias(symbol: Symbol, declarations: list[Declaration]) -> bool:
    return any(declaration != Declaration(ROOT / symbol.path, symbol.position) for declaration in declarations)


def line_starts(text: str) -> list[int]:
    return [0] + [match.end() for match in LINE_BREAK.finditer(text)]


def line_at(text: str, starts: list[int], line: int) -> str:
    end = LINE_BREAK.search(text, starts[line])
    return text[starts[line]:end.start() if end else len(text)]


def offset_of(text: str, starts: list[int], position: Position, where: str) -> int:
    if not 0 <= position.line < len(starts):
        raise RenameError(at(where, f'line {position.line + 1} is past the end of the file'))
    if not 0 <= position.character <= len(line_at(text, starts, position.line)):
        raise RenameError(at(where, f'column {position.character + 1} is past the end of line {position.line + 1}'))
    return starts[position.line] + position.character


def word_pattern(word: str) -> re.Pattern[str]:
    return re.compile(rf'(?<!\w){re.escape(word)}(?!\w)')


def settle_position(text: str, position: Position, name: str) -> Position | None:
    starts = line_starts(text)
    if not 0 <= position.line < len(starts):
        return None
    columns = [match.start() for match in word_pattern(name).finditer(line_at(text, starts, position.line))]
    if not columns:
        return None
    return Position(position.line, min(columns, key=lambda column: abs(column - position.character)))


def word_at(text: str, position: Position, where: str) -> str:
    starts = line_starts(text)
    offset_of(text, starts, position, where)
    line = line_at(text, starts, position.line)
    for match in IDENTIFIER.finditer(line):
        if match.start() <= position.character < match.end():
            return match[0]
    raise RenameError(at(where, f'no identifier at line {position.line + 1}, column {position.character + 1}'))


def apply_text_edits(text: str, edits: list[TextEdit], where: str) -> str:
    starts = line_starts(text)
    spans = sorted((offset_of(text, starts, edit.start, where), offset_of(text, starts, edit.end, where), edit.text)
                   for edit in edits)
    pieces = []
    cursor = 0
    for start, end, replacement in spans:
        if start < cursor or end < start:
            raise RenameError(at(where, 'clangd sent overlapping edits'))
        pieces += [text[cursor:start], replacement]
        cursor = end
    return ''.join(pieces) + text[cursor:]


def text_edit_from_lsp(value: object) -> TextEdit:
    edit = expect(value, dict, 'text edit')
    edit_range = expect(edit.get('range'), dict, 'edit range')
    return TextEdit(position_from_lsp(edit_range.get('start')), position_from_lsp(edit_range.get('end')),
                    expect(edit.get('newText'), str, 'edit text'))


def edits_from_workspace_edit(value: object) -> dict[Path, list[TextEdit]]:
    workspace_edit = expect(value, dict, 'workspace edit')
    if 'documentChanges' in workspace_edit:
        raise RenameError('clangd sent document changes, which this tool does not request')
    edits = {}
    for uri, file_edits in expect(workspace_edit.get('changes', {}), dict, 'change list').items():
        path = repo_path(path_of_uri(uri))
        if path is None:
            raise RenameError(f'clangd would edit {path_of_uri(uri)}, outside the files this tool may change')
        edits[path] = [text_edit_from_lsp(edit) for edit in expect(file_edits, list, 'edit list')]
    return edits


def find_word(text: str, word: str) -> list[tuple[int, str]]:
    pattern = word_pattern(word)
    return [(number, line.strip()) for number, line in enumerate(text.splitlines(), 1) if pattern.search(line)]


def changed_files(originals: dict[Path, str], finals: dict[Path, str]) -> list[FileChange]:
    return [FileChange(path, originals[path], finals[path]) for path in sorted(finals)
            if finals[path] != originals[path]]


def render_diff(changes: list[FileChange]) -> str:
    lines = []
    for change in changes:
        lines += difflib.unified_diff(change.before.splitlines(), change.after.splitlines(),
                                      f'a/{change.path.as_posix()}', f'b/{change.path.as_posix()}',
                                      n=1, lineterm='')
    return '\n'.join(lines)


def render_leftovers(name: str, leftovers: list[Leftover]) -> str:
    shown = [f'  {leftover.path}:{leftover.line}: {leftover.text[:120]}' for leftover in leftovers[:LEFTOVER_LINES_SHOWN]]
    hidden = len(leftovers) - len(shown)
    return '\n'.join([f'{name} is still spelled in {len(leftovers)} place(s) clangd did not rename:', *shown,
                      *([f'  ({hidden} more)'] if hidden else [])])


def unindexed_sources(database_files: list[Path], tracked: list[Path]) -> list[Path]:
    indexed = set(database_files)
    return [path for path in tracked
            if path.suffix == '.cpp' and not path.as_posix().startswith(UNTOUCHABLE) and path not in indexed]


def formattable(path: Path) -> bool:
    return path.suffix in CPP_SUFFIXES and not path.as_posix().startswith(UNFORMATTED)


def read_message(stream: BinaryIO) -> JsonObject | None:
    length = None
    while True:
        line = stream.readline()
        if not line:
            return None
        if not line.strip():
            break
        key, _, value = line.decode('ascii').partition(':')
        if key.strip().lower() == 'content-length':
            length = int(value)
    if length is None:
        raise ValueError('a message without a Content-Length header')
    message = json.loads(stream.read(length))
    if not isinstance(message, dict):
        raise ValueError('a message that is not a JSON object')
    return message


class Clangd:
    def __init__(self, executable: str) -> None:
        self.process = subprocess.Popen(
            [executable, f'--compile-commands-dir={COMPILE_DB.parent}', '--background-index',
             '--background-index-priority=normal', '-j=4', '--log=error', '--rename-file-limit=0',
             '--limit-results=0'],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=ROOT)
        self.condition = threading.Condition()
        self.sending = threading.Lock()
        self.responses: dict[int, JsonObject] = {}
        self.next_id = 1
        self.indexing = Indexing.UNSEEN
        self.index_percentage = 0
        self.exit_reason: str | None = None
        self.stderr_tail: deque[str] = deque(maxlen=20)
        self.documents: dict[Path, tuple[int, str]] = {}
        threading.Thread(target=self._read_messages, daemon=True).start()
        threading.Thread(target=self._read_stderr, daemon=True).start()

    def __enter__(self) -> 'Clangd':
        return self

    def __exit__(self, *_: object) -> None:
        self.process.kill()
        self.process.wait()

    def _read_messages(self) -> None:
        stdout = self.process.stdout
        assert stdout is not None
        try:
            while (message := read_message(stdout)) is not None:
                self._dispatch(message)
            reason = 'clangd exited'
        except (OSError, ValueError) as error:
            reason = f'clangd sent an unreadable message ({error})'
        with self.condition:
            self.exit_reason = reason
            self.condition.notify_all()

    def _read_stderr(self) -> None:
        stderr = self.process.stderr
        assert stderr is not None
        for line in stderr:
            self.stderr_tail.append(line.decode(errors='replace').rstrip())

    def _dispatch(self, message: JsonObject) -> None:
        method = message.get('method')
        request_id = message.get('id')
        if method is None and isinstance(request_id, int):
            with self.condition:
                self.responses[request_id] = message
                self.condition.notify_all()
        elif method is not None and request_id is not None:
            self._send({'jsonrpc': '2.0', 'id': request_id, 'result': None})
        elif method == '$/progress':
            self._progress(message.get('params'))

    def _progress(self, params: object) -> None:
        if not isinstance(params, dict) or params.get('token') != INDEX_PROGRESS_TOKEN:
            return
        value = params.get('value')
        if not isinstance(value, dict):
            return
        with self.condition:
            match value.get('kind'):
                case 'begin':
                    self.indexing = Indexing.RUNNING
                case 'report' if isinstance(value.get('percentage'), int):
                    self.index_percentage = value['percentage']
                case 'end':
                    self.indexing = Indexing.DONE
            self.condition.notify_all()

    def _check_alive(self) -> None:
        if self.exit_reason is not None:
            details = '\n'.join(self.stderr_tail)
            raise RenameError(self.exit_reason + (f':\n{details}' if details else ''))

    def _send(self, message: JsonObject) -> None:
        body = json.dumps(message).encode()
        stdin = self.process.stdin
        assert stdin is not None
        try:
            with self.sending:
                stdin.write(f'Content-Length: {len(body)}\r\n\r\n'.encode() + body)
                stdin.flush()
        except OSError:
            with self.condition:
                self._check_alive()
            raise RenameError('clangd stopped reading its input')

    def notify(self, method: str, params: object) -> None:
        self._send({'jsonrpc': '2.0', 'method': method, 'params': params})

    def request(self, method: str, params: object, timeout: float = 120.0) -> object:
        with self.condition:
            request_id = self.next_id
            self.next_id += 1
        self._send({'jsonrpc': '2.0', 'id': request_id, 'method': method, 'params': params})
        deadline = time.monotonic() + timeout
        with self.condition:
            while request_id not in self.responses:
                self._check_alive()
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise RenameError(f'clangd did not answer {method} within {timeout:.0f} s')
                self.condition.wait(timeout=remaining)
            response = self.responses.pop(request_id)
        error = response.get('error')
        if error is not None:
            message = error.get('message') if isinstance(error, dict) else error
            raise RenameError(f'clangd refused {method}: {message}')
        return response.get('result')

    def initialize(self) -> None:
        result = expect(self.request('initialize', {
            'processId': os.getpid(),
            'rootUri': ROOT.as_uri(),
            'capabilities': {
                'general': {'positionEncodings': ['utf-32']},
                'offsetEncoding': ['utf-32'],
                'window': {'workDoneProgress': True},
            },
        }), dict, 'initialize result')
        capabilities = expect(result.get('capabilities', {}), dict, 'capability list')
        if 'utf-32' not in (result.get('offsetEncoding'), capabilities.get('positionEncoding')):
            raise RenameError('clangd did not agree to count columns in code points (utf-32)')
        self.notify('initialized', {})

    def sync(self, path: Path, text: str) -> None:
        version, synced = self.documents.get(path, (0, None))
        if synced == text:
            return
        version += 1
        self.documents[path] = (version, text)
        if version == 1:
            self.notify('textDocument/didOpen', {'textDocument': {
                'uri': uri_of(path), 'languageId': 'cpp', 'version': version, 'text': text}})
        else:
            self.notify('textDocument/didChange', {
                'textDocument': {'uri': uri_of(path), 'version': version}, 'contentChanges': [{'text': text}]})

    def wait_until_indexed(self) -> None:
        start = time.monotonic()
        shown = 0
        with self.condition:
            while True:
                self._check_alive()
                elapsed = time.monotonic() - start
                if self.indexing is Indexing.DONE:
                    return
                if self.indexing is Indexing.UNSEEN and elapsed > QUIET_SPELL_SECONDS:
                    return
                if elapsed > INDEX_DEADLINE_SECONDS:
                    raise RenameError(f'clangd did not finish indexing within {INDEX_DEADLINE_SECONDS:.0f} s')
                if self.indexing is Indexing.RUNNING and self.index_percentage // 10 != shown:
                    shown = self.index_percentage // 10
                    print(f'indexing {self.index_percentage}%', flush=True)
                self.condition.wait(timeout=1.0)


def read_source(path: Path) -> str:
    try:
        with open(ROOT / path, encoding='utf-8', newline='') as file:
            return file.read()
    except UnicodeDecodeError:
        raise RenameError(f'{path} is not UTF-8')
    except OSError as error:
        raise RenameError(f'cannot read {path}: {error.strerror}')


def write_source(path: Path, text: str) -> None:
    with open(path, 'w', encoding='utf-8', newline='') as file:
        file.write(text)


def required_tool(name: str, override: str | None = None) -> str:
    found = shutil.which(os.environ.get(override, name) if override else name)
    if found is None:
        raise RenameError(f'{name} is not on PATH; run nix develop first')
    return found


def git_lines(*args: str) -> list[str]:
    result = subprocess.run(['git', *args], cwd=ROOT, capture_output=True, text=True)
    if result.returncode not in (0, 1):
        raise RenameError(f'git {args[0]} failed: {result.stderr.strip()}')
    return result.stdout.splitlines()


def database_files() -> list[Path]:
    try:
        entries = json.loads(COMPILE_DB.read_text())
    except (OSError, ValueError) as error:
        raise RenameError(f'cannot read {COMPILE_DB.relative_to(ROOT)} ({error}); run python3 tools/compile-db.py')
    files = []
    for entry in entries if isinstance(entries, list) else []:
        file = entry.get('file') if isinstance(entry, dict) else None
        path = repo_path(Path(file).resolve()) if isinstance(file, str) else None
        if path is not None:
            files.append(path)
    if not files:
        raise RenameError(f'{COMPILE_DB.relative_to(ROOT)} lists no source in this tree; run python3 tools/compile-db.py')
    return files


def warn_unindexed(indexed: list[Path]) -> None:
    missing = unindexed_sources(indexed, [Path(name) for name in git_lines('ls-files', '--', 'src', 'tests')])
    if missing:
        named = ', '.join(str(path) for path in missing[:3]) + (f' and {len(missing) - 3} more' if len(missing) > 3 else '')
        print(f'warning: {len(missing)} source file(s) are not in {COMPILE_DB.relative_to(ROOT)}, so clangd '
              f'renames nothing inside them: {named}; python3 tools/compile-db.py rewrites the database',
              file=sys.stderr)


def names_another_symbol(clangd: Clangd, symbol: Symbol) -> bool:
    text = read_source(symbol.path)
    position = settle_position(text, symbol.position, symbol.name)
    if position is None:
        return False
    clangd.sync(symbol.path, text)
    info = clangd.request('textDocument/symbolInfo', {
        'textDocument': {'uri': uri_of(symbol.path)},
        'position': {'line': position.line, 'character': position.character},
    }, timeout=RENAME_TIMEOUT_SECONDS)
    return is_alias(replace(symbol, position=position), declarations_from_lsp(info))


def locate(clangd: Clangd, request: RenameRequest) -> Symbol:
    match request.target:
        case ByPosition(path=path, position=position):
            name = word_at(read_source(path), position, at(request.where, str(path)))
            return Symbol(name, '', 'Symbol', path, position)
        case ByName() as target:
            listed = expect(clangd.request('workspace/symbol', {'query': target.name}) or [], list, 'symbol list')
            named = [found for found in map(symbol_from_lsp, listed) if found is not None and found.name == target.name]
            if sum(matches(found, target) for found in named) > 1:
                named = [found for found in named
                         if not (matches(found, target) and names_another_symbol(clangd, found))]
            symbol = choose_symbol(named, target, request.where)
            settled = settle_position(read_source(symbol.path), symbol.position, symbol.name)
            if settled is None:
                raise RenameError(at(request.where, f'the index places {symbol.name} at {symbol.path}:'
                                                    f'{symbol.position.line + 1}, which no longer spells it'))
            return replace(symbol, position=settled)


def rename(clangd: Clangd, request: RenameRequest) -> RenameResult:
    symbol = locate(clangd, request)
    clangd.sync(symbol.path, read_source(symbol.path))
    result = clangd.request('textDocument/rename', {
        'textDocument': {'uri': uri_of(symbol.path)},
        'position': {'line': symbol.position.line, 'character': symbol.position.character},
        'newName': request.new,
    }, timeout=RENAME_TIMEOUT_SECONDS)
    if result is None:
        raise RenameError(at(request.where, f'clangd found nothing to rename at {describe(symbol)}'))
    edits = edits_from_workspace_edit(result)
    edited = {file: apply_text_edits(read_source(file), file_edits, at(request.where, str(file)))
              for file, file_edits in edits.items()}
    return RenameResult(symbol, sum(map(len, edits.values())), edited)


def format_source(formatter: str, path: Path, text: str) -> str:
    result = subprocess.run([formatter, f'--assume-filename={ROOT / path}'], input=text.encode(),
                            capture_output=True, cwd=ROOT)
    if result.returncode != 0:
        raise RenameError(f'clang-format failed on {path}: {result.stderr.decode(errors="replace").strip()}')
    return result.stdout.decode()


def formatted(formatter: str, originals: dict[Path, str], texts: dict[Path, str]) -> tuple[dict[Path, str], list[Path]]:
    finals = dict(texts)
    skipped = []
    for path, text in texts.items():
        if not formattable(path):
            continue
        if format_source(formatter, path, originals[path]) != originals[path]:
            skipped.append(path)
            continue
        finals[path] = format_source(formatter, path, text)
    return finals, skipped


def commit(changes: list[FileChange]) -> None:
    for change in changes:
        if read_source(change.path) != change.before:
            raise RenameError(f'{change.path} changed on disk during the rename; nothing was written')
    staged: list[tuple[Path, Path]] = []
    try:
        for change in changes:
            target = ROOT / change.path
            temporary = target.with_name(f'.{target.name}.rename')
            staged.append((temporary, target))
            write_source(temporary, change.after)
            shutil.copymode(target, temporary)
    except OSError as error:
        for temporary, _ in staged:
            temporary.unlink(missing_ok=True)
        raise RenameError(f'cannot write {error.filename}: {error.strerror}; nothing was written')
    for temporary, target in staged:
        temporary.replace(target)


def find_leftovers(name: str, finals: dict[Path, str], whole_tree: bool) -> list[Leftover]:
    excludes = [f':(exclude){folder}' for folder in LEFTOVER_EXCLUDES]
    candidates = {Path(line) for line in git_lines('grep', '-l', '-I', '-w', '--untracked', '-e', name, '--', '.', *excludes)
                  } if whole_tree else set()
    leftovers = []
    for path in sorted(candidates | set(finals)):
        text = finals[path] if path in finals else read_source(path)
        leftovers += [Leftover(path, number, line) for number, line in find_word(text, name)]
    return leftovers


def run(request: RenameRequest, apply: bool) -> None:
    clangd_path = required_tool('clangd')
    formatter = required_tool('clang-format', 'CLANG_FORMAT')
    indexed = database_files()
    warn_unindexed(indexed)
    with Clangd(clangd_path) as clangd:
        clangd.initialize()
        clangd.sync(WARM_UP_DOCUMENT, '')
        print("waiting for clangd's index ...", flush=True)
        clangd.wait_until_indexed()
        result = rename(clangd, request)
    print(f'renaming {describe(result.symbol)} to {request.new}: '
          f'{result.edit_count} edit(s) in {len(result.texts)} file(s)', flush=True)
    originals = {path: read_source(path) for path in result.texts}
    finals, unformatted = formatted(formatter, originals, result.texts)
    changes = changed_files(originals, finals)
    if apply:
        commit(changes)
    else:
        print(render_diff(changes))
    for path in unformatted:
        print(f'not formatted: {path} was not clang-format clean before the rename')
    leftovers = find_leftovers(result.symbol.name, finals, isinstance(request.target, ByName))
    if leftovers:
        print(render_leftovers(result.symbol.name, leftovers))
    print(f'{"changed" if apply else "would change"} {len(changes)} file(s)')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('old')
    parser.add_argument('new')
    parser.add_argument('--kind', choices=sorted(SYMBOL_KINDS.values()))
    parser.add_argument('--apply', action='store_true', help='write the edits; otherwise print the diff')
    arguments = parser.parse_args()
    try:
        run(parse_request(arguments.old, arguments.new, arguments.kind,
                          f'{arguments.old} -> {arguments.new}', Path.cwd()), arguments.apply)
    except RenameError as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
