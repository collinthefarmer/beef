#!/usr/bin/env python3
"""Rename a C++ symbol across the tree through clangd.

    tools/rename.py OldName NewName [--apply] [--kind Struct|Enum|Function|...]

Without --apply the edits are listed and nothing is written. The symbol is
found by workspace/symbol on its unqualified name; when several symbols
share it (Studio::SlotRow and WornEnchantmentPBR::SlotRow), pass the
qualified name (Studio::SlotRow) or --kind to pick one. Renames go through
textDocument/rename, so references in strings and comments are left
alone and a file rename is applied when clangd asks for one.

Needs build/clangd/compile_commands.json (tools/compile-db.sh writes it
from a configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON, keeping this
repo's own sources) and clangd on PATH:

    nix shell nixpkgs#llvmPackages.clang-tools -c tools/rename.py Old New --apply

The first run builds clangd's background index under .cache/clangd, which
takes a few minutes; later runs reuse it.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path
from urllib.parse import unquote, urlparse

ROOT = Path(__file__).resolve().parent.parent
COMPILE_DB_DIR = ROOT / "build" / "clangd"


class Clangd:
    """A minimal JSON-RPC client over clangd's stdio."""

    def __init__(self):
        self.proc = subprocess.Popen(
            [
                # nixpkgs' wrapped clangd adds the host's glibc and libstdc++
                # include paths, which shadow the Windows SDK's; the unwrapped
                # binary reads the database's flags alone.
                shutil.which("clangd-unwrapped") or "clangd",
                f"--compile-commands-dir={COMPILE_DB_DIR}",
                "--background-index",
                "--background-index-priority=normal",
                "-j=4",
                "--log=error",
                "--rename-file-limit=0",
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            cwd=ROOT,
        )
        self.next_id = 1
        self.responses = {}
        self.indexing_tokens = set()
        self.indexing_seen = False
        self.lock = threading.Condition()
        self.reader = threading.Thread(target=self._read_loop, daemon=True)
        self.reader.start()

    def _read_loop(self):
        out = self.proc.stdout
        while True:
            headers = {}
            line = out.readline()
            if not line:
                return
            while line and line.strip():
                key, _, value = line.decode().partition(":")
                headers[key.strip().lower()] = value.strip()
                line = out.readline()
            length = int(headers.get("content-length", "0"))
            if length == 0:
                continue
            message = json.loads(out.read(length))
            with self.lock:
                if "id" in message and "method" not in message:
                    self.responses[message["id"]] = message
                elif message.get("method") == "$/progress":
                    if os.environ.get("RENAME_DEBUG"):
                        print("progress:", json.dumps(message["params"])[:160], file=sys.stderr, flush=True)
                    self._progress(message["params"])
                elif message.get("method") == "window/workDoneProgress/create":
                    self._reply(message["id"], None)
                self.lock.notify_all()

    def _progress(self, params):
        token = params.get("token")
        value = params.get("value", {})
        kind = value.get("kind")
        if kind == "begin" and "index" in str(value.get("title", "")).lower():
            self.indexing_tokens.add(token)
            self.indexing_seen = True
        elif kind == "end" and (token in self.indexing_tokens or token == "backgroundIndexProgress"):
            # A warm cache ends without a begin.
            self.indexing_tokens.discard(token)
            self.indexing_seen = True

    def _send(self, message):
        body = json.dumps(message).encode()
        self.proc.stdin.write(f"Content-Length: {len(body)}\r\n\r\n".encode() + body)
        self.proc.stdin.flush()

    def _reply(self, request_id, result):
        self._send({"jsonrpc": "2.0", "id": request_id, "result": result})

    def notify(self, method, params):
        self._send({"jsonrpc": "2.0", "method": method, "params": params})

    def request(self, method, params, timeout=600):
        request_id = self.next_id
        self.next_id += 1
        self._send({"jsonrpc": "2.0", "id": request_id, "method": method, "params": params})
        deadline = time.time() + timeout
        with self.lock:
            while request_id not in self.responses:
                if not self.lock.wait(timeout=max(0.0, deadline - time.time())):
                    raise TimeoutError(f"{method} timed out")
            response = self.responses.pop(request_id)
        if "error" in response:
            raise RuntimeError(f"{method}: {response['error']}")
        return response.get("result")

    def wait_for_index(self, quiet_seconds=20):
        """Background indexing has no completion event beyond its progress token
        ending; wait for that, or for a quiet spell if no indexing was ever announced."""
        start = time.time()
        with self.lock:
            while True:
                if self.indexing_seen and not self.indexing_tokens:
                    return
                if not self.indexing_seen and time.time() - start > quiet_seconds:
                    return
                self.lock.wait(timeout=1.0)

    def close(self):
        try:
            self.request("shutdown", None, timeout=10)
            self.notify("exit", None)
        except Exception:
            pass
        self.proc.terminate()


def uri_of(path: Path) -> str:
    return path.resolve().as_uri()


def path_of(uri: str) -> Path:
    return Path(unquote(urlparse(uri).path))


def open_document(client: Clangd, path: Path):
    client.notify(
        "textDocument/didOpen",
        {"textDocument": {"uri": uri_of(path), "languageId": "cpp", "version": 1, "text": path.read_text()}},
    )


def find_symbol(client: Clangd, name: str, kind: str | None):
    query = name.split("::")[-1]
    results = client.request("workspace/symbol", {"query": query}) or []
    wanted_container = "::".join(name.split("::")[:-1])
    matches = []
    for symbol in results:
        if symbol["name"] != query:
            continue
        container = symbol.get("containerName", "")
        if wanted_container and not container.endswith(wanted_container):
            continue
        if kind and SYMBOL_KINDS.get(symbol.get("kind"), "?") != kind:
            continue
        location = symbol["location"]
        if not str(path_of(location["uri"])).startswith(str(ROOT)):
            continue
        matches.append(symbol)
    return matches


SYMBOL_KINDS = {
    2: "Module", 3: "Namespace", 5: "Class", 6: "Method", 7: "Property", 8: "Field", 9: "Constructor",
    10: "Enum", 11: "Interface", 12: "Function", 13: "Variable", 14: "Constant", 22: "EnumMember",
    23: "Struct", 26: "TypeParameter",
}


def describe(symbol):
    location = symbol["location"]
    path = path_of(location["uri"]).relative_to(ROOT)
    line = location["range"]["start"]["line"] + 1
    container = symbol.get("containerName", "")
    kind = SYMBOL_KINDS.get(symbol.get("kind"), str(symbol.get("kind")))
    return f"{kind} {container}::{symbol['name']} at {path}:{line}"


def apply_edit(workspace_edit, apply: bool):
    """Apply a WorkspaceEdit: text edits per document, then file renames.
    Returns {path: edit count}."""
    counts = {}
    changes = {}
    renames = []
    if "documentChanges" in workspace_edit:
        for change in workspace_edit["documentChanges"]:
            if "kind" in change:
                if change["kind"] == "rename":
                    renames.append((path_of(change["oldUri"]), path_of(change["newUri"])))
                continue
            changes.setdefault(change["textDocument"]["uri"], []).extend(change["edits"])
    for uri, edits in workspace_edit.get("changes", {}).items():
        changes.setdefault(uri, []).extend(edits)
    for uri, edits in changes.items():
        path = path_of(uri)
        counts[str(path.relative_to(ROOT))] = len(edits)
        if not apply:
            continue
        lines = path.read_text().split("\n")
        # Apply from the end so earlier offsets stay valid.
        for edit in sorted(edits, key=lambda e: (e["range"]["start"]["line"], e["range"]["start"]["character"]), reverse=True):
            start, end = edit["range"]["start"], edit["range"]["end"]
            before = lines[start["line"]][: start["character"]]
            after = lines[end["line"]][end["character"] :]
            lines[start["line"] : end["line"] + 1] = [before + edit["newText"] + after]
        path.write_text("\n".join(lines))
    for old, new in renames:
        counts[f"{old.relative_to(ROOT)} -> {new.relative_to(ROOT)}"] = 1
        if apply:
            old.rename(new)
    return counts


def report_leftovers(old: str):
    """Comments, strings and documents still spelling the old name, for a hand pass."""
    targets = [str(p) for p in [ROOT / "src", ROOT / "tests"]] + [str(ROOT / f) for f in ("README.md", "ARCHITECTURE.md", "NOTES.md", "CLAUDE.md")]
    result = subprocess.run(["grep", "-rnw", "--include=*.h", "--include=*.cpp", "--include=*.md", old] + targets, capture_output=True, text=True)
    lines = [line.replace(str(ROOT) + "/", "") for line in result.stdout.splitlines()]
    if lines:
        print(f"still spelled in {len(lines)} place(s) clangd does not rename (comments, strings, docs):")
        for line in lines:
            print("  " + line[:160])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("old")
    parser.add_argument("new")
    parser.add_argument("--apply", action="store_true", help="write the edits; otherwise list them")
    parser.add_argument("--kind", help="Struct, Enum, Function, Field, EnumMember, Variable ...")
    args = parser.parse_args()

    if not (COMPILE_DB_DIR / "compile_commands.json").exists():
        sys.exit(f"no compile database at {COMPILE_DB_DIR}; configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON")

    client = Clangd()
    try:
        client.request(
            "initialize",
            {
                "processId": os.getpid(),
                "rootUri": uri_of(ROOT),
                "capabilities": {
                    "workspace": {"workspaceEdit": {"documentChanges": True, "resourceOperations": ["rename"]}},
                    "window": {"workDoneProgress": True},
                },
            },
        )
        client.notify("initialized", {})
        # clangd loads a compile database, and starts indexing it, when a
        # file it covers is first opened.
        open_document(client, ROOT / "src" / "Recipe.cpp")
        print("waiting for clangd's index ...", flush=True)
        client.wait_for_index()

        matches = find_symbol(client, args.old, args.kind)
        if not matches:
            sys.exit(f"no symbol named {args.old} in the index")
        if len(matches) > 1:
            print("more than one symbol matches; qualify the name or pass --kind:")
            for symbol in matches:
                print("  " + describe(symbol))
            sys.exit(2)
        symbol = matches[0]
        print("renaming " + describe(symbol))
        location = symbol["location"]
        path = path_of(location["uri"])
        open_document(client, path)
        edit = client.request(
            "textDocument/rename",
            {
                "textDocument": {"uri": uri_of(path)},
                "position": location["range"]["start"],
                "newName": args.new.split("::")[-1],
            },
        )
        if not edit:
            sys.exit("clangd returned no edit")
        counts = apply_edit(edit, args.apply)
        total = 0
        for file, count in sorted(counts.items()):
            print(f"  {count:4d}  {file}")
            total += count
        print(f"{'applied' if args.apply else 'would apply'} {total} edit(s) in {len(counts)} file(s)")
        if args.apply:
            report_leftovers(args.old.split("::")[-1])
    finally:
        client.close()


if __name__ == "__main__":
    main()
