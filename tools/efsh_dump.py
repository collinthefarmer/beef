#!/usr/bin/env python3
"""Dump EFSH records from a Skyrim plugin as importer fixture JSON.

Usage: efsh_dump.py <plugin.esm> [--edid GLOB] [--out DIR]

One JSON file per record, named by the record's editor ID, with the DATA
fields the importer reads.
"""

import argparse
import fnmatch
import json
import struct
import sys
import zlib
from pathlib import Path

FIELDS = [
    (0x010, "fillColorKey1", "c"),
    (0x014, "fillAlphaFadeInTime", "f"),
    (0x018, "fillFullAlphaTime", "f"),
    (0x01C, "fillAlphaFadeOutTime", "f"),
    (0x020, "fillPersistentAlphaRatio", "f"),
    (0x024, "fillAlphaPulseAmplitude", "f"),
    (0x028, "fillAlphaPulseFrequency", "f"),
    (0x02C, "fillTextureAnimationSpeedU", "f"),
    (0x030, "fillTextureAnimationSpeedV", "f"),
    (0x034, "edgeFallOff", "f"),
    (0x038, "edgeColor", "c"),
    (0x03C, "edgeAlphaFadeInTime", "f"),
    (0x040, "edgeFullAlphaTime", "f"),
    (0x044, "edgeAlphaFadeOutTime", "f"),
    (0x048, "edgePersistentAlphaRatio", "f"),
    (0x04C, "edgeAlphaPulseAmplitude", "f"),
    (0x050, "edgeAlphaPulseFrequency", "f"),
    (0x054, "fillFullAlphaRatio", "f"),
    (0x058, "edgeFullAlphaRatio", "f"),
    (0x138, "fillColorKey2", "c"),
    (0x13C, "fillColorKey3", "c"),
    (0x140, "fillColorKey1Scale", "f"),
    (0x144, "fillColorKey2Scale", "f"),
    (0x148, "fillColorKey3Scale", "f"),
    (0x14C, "fillColorKey1Time", "f"),
    (0x150, "fillColorKey2Time", "f"),
    (0x154, "fillColorKey3Time", "f"),
    (0x158, "colorScale", "f"),
    (0x180, "flags", "u"),
    (0x184, "fillTextureScaleU", "f"),
    (0x188, "fillTextureScaleV", "f"),
]

DATA_MIN = 0x15C

def read_fields(data: bytes) -> dict:
    out = {}
    for offset, name, kind in FIELDS:
        if offset + 4 > len(data):
            out[name] = 1.0 if kind == "f" else (0 if kind == "u" else [0, 0, 0])
            continue
        if kind == "f":
            out[name] = round(struct.unpack_from("<f", data, offset)[0], 6)
        elif kind == "u":
            out[name] = struct.unpack_from("<I", data, offset)[0]
        else:
            r, g, b, _a = struct.unpack_from("<4B", data, offset)
            out[name] = [r, g, b]
    return out

def iter_records(blob: bytes):
    """Yield (type, form_id, flags, payload) for every record, descending into groups."""
    pos = 0
    end = len(blob)
    while pos + 24 <= end:
        rtype = blob[pos:pos + 4]
        size = struct.unpack_from("<I", blob, pos + 4)[0]
        if rtype == b"GRUP":
            yield from iter_records(blob[pos + 24:pos + size])
            pos += size
            continue
        flags, form_id = struct.unpack_from("<II", blob, pos + 8)
        payload = blob[pos + 24:pos + 24 + size]
        if flags & 0x40000:
            payload = zlib.decompress(payload[4:])
        yield rtype.decode("ascii"), form_id, flags, payload
        pos += 24 + size

def subrecords(payload: bytes):
    pos = 0
    while pos + 6 <= len(payload):
        stype = payload[pos:pos + 4].decode("ascii", "replace")
        size = struct.unpack_from("<H", payload, pos + 4)[0]
        yield stype, payload[pos + 6:pos + 6 + size]
        pos += 6 + size

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("plugin", type=Path)
    ap.add_argument("--edid", default="EnchArmor*FXS", help="editor ID glob (default: EnchArmor*FXS)")
    ap.add_argument("--out", type=Path, default=Path("tests/fixtures/efsh"))
    args = ap.parse_args()

    blob = args.plugin.read_bytes()
    plugin_file = args.plugin.name
    args.out.mkdir(parents=True, exist_ok=True)
    count = 0
    for rtype, form_id, _flags, payload in iter_records(blob):
        if rtype != "EFSH":
            continue
        edid = ""
        icon = ""
        data = b""
        for stype, body in subrecords(payload):
            if stype == "EDID":
                edid = body.rstrip(b"\0").decode("ascii", "replace")
            elif stype == "ICON":
                icon = body.rstrip(b"\0").decode("ascii", "replace")
            elif stype == "DATA":
                data = body
        if not fnmatch.fnmatch(edid, args.edid) or len(data) < DATA_MIN:
            continue
        record = {
            "formKey": f"0x{form_id & 0x00FFFFFF:X}~{plugin_file}",
            "editorId": edid,
            "fillTexture": icon,
            **read_fields(data),
        }
        path = args.out / f"{edid}.json"
        path.write_text(json.dumps(record, indent=2) + "\n")
        print(f"{path}  {form_id:08X}  {icon}")
        count += 1
    print(f"{count} records", file=sys.stderr)
    return 0 if count else 1

if __name__ == "__main__":
    sys.exit(main())
