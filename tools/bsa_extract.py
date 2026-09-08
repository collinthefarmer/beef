#!/usr/bin/env python3
"""Extract files from Skyrim SE BSA archives (version 105, LZ4 compression).

Usage: bsa_extract.py --out DIR --archive A.bsa [--archive B.bsa ...] PATH [PATH ...]
Paths are matched case-insensitively against the archive's folder\\file names,
e.g. textures\\effects\\cloudtile.dds. Needs the `lz4` package.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

try:
    import lz4.frame
except ImportError:
    sys.exit("pip install lz4")

ARCHIVE_COMPRESSED = 0x4
EMBED_FILE_NAMES = 0x100
FILE_COMPRESSION_TOGGLE = 1 << 30
SIZE_MASK = 0x3FFFFFFF

def bzstring(data: bytes, pos: int) -> tuple[str, int]:
    n = data[pos]
    return data[pos + 1 : pos + n].rstrip(b"\0").decode("cp1252", errors="replace"), pos + 1 + n

def read_index(data: bytes) -> tuple[dict[str, tuple[int, int, bool]], bool]:
    magic, version, folder_offset, flags, folder_count, file_count, _fn_len, _f_len, _file_flags = struct.unpack_from("<4sIIIIIIII", data, 0)
    if magic != b"BSA\0" or version != 105:
        raise ValueError(f"not an SSE BSA (magic {magic!r}, version {version})")
    default_compressed = bool(flags & ARCHIVE_COMPRESSED)
    embed_names = bool(flags & EMBED_FILE_NAMES)
    folders = []
    pos = folder_offset
    for _ in range(folder_count):
        _hash, count, _pad, offset, _pad2 = struct.unpack_from("<QIIII", data, pos)
        folders.append((count, offset))
        pos += 24
    total_name_len = struct.unpack_from("<I", data, 28)[0]
    records = []
    for count, offset in folders:
        pos = offset - total_name_len
        name, pos = bzstring(data, pos)
        for _ in range(count):
            _hash, size, data_offset = struct.unpack_from("<QII", data, pos)
            records.append((name, size, data_offset))
            pos += 16
    names = data[pos : pos + total_name_len].split(b"\0")
    index = {}
    for (folder, size, offset), raw_name in zip(records, names):
        compressed = default_compressed != bool(size & FILE_COMPRESSION_TOGGLE)
        key = (folder + "\\" + raw_name.decode("cp1252", errors="replace")).lower()
        index[key] = (offset, size & SIZE_MASK, compressed)
    return index, embed_names

def extract(data: bytes, entry: tuple[int, int, bool], embed_names: bool) -> bytes:
    offset, size, compressed = entry
    pos = offset
    if embed_names:
        n = data[pos]
        pos += 1 + n
        size -= 1 + n
    if compressed:
        _unpacked = struct.unpack_from("<I", data, pos)[0]
        return lz4.frame.decompress(data[pos + 4 : pos + size])
    return data[pos : pos + size]

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--list", action="store_true", help="print matching names instead of extracting (PATHs are substrings)")
    parser.add_argument("--archive", type=Path, action="append", required=True, help="BSA to search (repeatable)")
    parser.add_argument("paths", nargs="+", help="archive paths such as textures\\effects\\cloudtile.dds")
    args = parser.parse_args()
    archives = args.archive
    wanted = [w.lower().replace("/", "\\") for w in args.paths]
    found = set()
    for archive in archives:
        data = archive.read_bytes()
        index, embed = read_index(data)
        for key, entry in index.items():
            if args.list:
                if any(w in key for w in wanted):
                    print(f"{archive.name}: {key}")
                continue
            if key in wanted and key not in found:
                out = args.out / key.replace("\\", "/")
                out.parent.mkdir(parents=True, exist_ok=True)
                out.write_bytes(extract(data, entry, embed))
                print(f"{archive.name}: {key} -> {out}")
                found.add(key)
    missing = [w for w in wanted if w not in found]
    if missing and not args.list:
        print("not found: " + ", ".join(missing), file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
