#!/usr/bin/env python3
"""Build the opt-in Arcane Circuit fixture from the user's Skyrim.esm."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

PLUGIN = 'BetterEnchantmentEffectsDemo.esp'
IDS = {'keyword': 0x01000800, 'effect': 0x01000801,
       'enchantment': 0x01000802, 'armor': 0x01000803}
SOURCES = {0x1394D: b'ARMO', 0x49504: b'MGEF', 0x49508: b'ENCH'}


def records(data, start=0, end=None, depth=0):
    end = len(data) if end is None else end
    if depth > 16 or end > len(data):
        raise ValueError('Invalid group bounds')
    while start < end:
        if end - start < 24:
            raise ValueError('Truncated record header')
        kind, size, flags, form = struct.unpack_from('<4sIII', data, start)
        stop = start + (size if kind == b'GRUP' else 24 + size)
        if stop > end or stop < start + 24:
            raise ValueError('Invalid record size')
        if kind == b'GRUP':
            yield from records(data, start + 24, stop, depth + 1)
        else:
            body = data[start + 24:stop]
            if flags & 0x40000:
                if len(body) < 4:
                    raise ValueError('Truncated compressed record')
                expected, = struct.unpack_from('<I', body)
                if expected > 16 * 1024 * 1024:
                    raise ValueError('Oversized compressed record')
                decoder = zlib.decompressobj()
                body = decoder.decompress(body[4:], expected + 1)
                if len(body) != expected or not decoder.eof or decoder.unused_data:
                    raise ValueError('Invalid compressed record')
            yield kind, form, flags, body
        start = stop


def subrecords(body):
    pos = 0
    extended = None
    out = []
    while pos < len(body):
        if len(body) - pos < 6:
            raise ValueError('Truncated subrecord header')
        kind, size = struct.unpack_from('<4sH', body, pos)
        pos += 6
        if kind == b'XXXX':
            if size != 4 or pos + 4 > len(body) or extended is not None:
                raise ValueError('Invalid extended size')
            extended, = struct.unpack_from('<I', body, pos)
            pos += 4
            continue
        size = size if extended is None else extended
        extended = None
        if pos + size > len(body):
            raise ValueError('Truncated subrecord')
        out.append((kind, body[pos:pos + size]))
        pos += size
    if extended is not None:
        raise ValueError('Dangling extended size')
    return out


def text(value):
    return value.encode('ascii') + b'\0'


def u32(value):
    return struct.pack('<I', value)


def replace(parts, changes):
    if not set(changes).issubset(k for k, _ in parts):
        raise ValueError('Source record is missing required fields')
    return [(kind, changes.get(kind, value)) for kind, value in parts]


def record(kind, form, parts):
    body = b''.join(struct.pack('<4sH', key, len(value)) + value
                    for key, value in parts)
    return struct.pack('<4sIIIIHH', kind, len(body), 0, form, 0, 44, 0) + body


def group(kind, content):
    return struct.pack('<4sI4sIII', b'GRUP', len(content) + 24, kind, 0, 0, 0) + content


def build(master):
    selected = {}
    for kind, form, flags, body in records(master):
        if form in SOURCES:
            if SOURCES[form] != kind or form in selected or flags & 0x20:
                raise ValueError('Unexpected source record')
            selected[form] = subrecords(body)
    if selected.keys() != SOURCES.keys():
        raise ValueError('Missing Skyrim source records')
    armor = replace(selected[0x1394D], {
        b'EDID': text('BEEFDemoArcaneCircuitCuirass'),
        b'FULL': text('Arcane Circuit - Dwarven Armor'),
        b'DESC': text(''),
        b'KSIZ': u32(5),
        b'KWDA': dict(selected[0x1394D])[b'KWDA'] + u32(IDS['keyword']),
    })
    if len(dict(armor)[b'KWDA']) != 20:
        raise ValueError('Unexpected vanilla armor keywords')
    full = next(i for i, (key, _) in enumerate(armor) if key == b'FULL')
    armor.insert(full + 1, (b'EITM', u32(IDS['enchantment'])))
    effect = replace(selected[0x49504], {
        b'EDID': text('BEEFDemoArcaneCircuitEffect'),
        b'FULL': text('Arcane Circuit'),
        b'DNAM': text('Increases your Magicka by <mag> points.'),
    })
    enchantment = replace(selected[0x49508], {
        b'EDID': text('BEEFDemoArcaneCircuitEnchantment'),
        b'FULL': text('Arcane Circuit'),
        b'ENIT': struct.pack('<6If2I', 0, 0, 0, 0, 0, 6, 0.0, 0, 0),
    })
    enchantment = [(k, v) for k, v in enchantment if k not in (b'EFID', b'EFIT')]
    enchantment += [(b'EFID', u32(0x493AA)),
                    (b'EFIT', struct.pack('<fII', 100.0, 0, 0)),
                    (b'EFID', u32(IDS['effect'])),
                    (b'EFIT', struct.pack('<fII', 25.0, 0, 0))]
    keyword = [(b'EDID', text('BEEFDemoCollection'))]
    header = record(b'TES4', 0, [
        (b'HEDR', struct.pack('<fII', 1.7, 8, 0x804)),
        (b'CNAM', text('BetterEnchantmentEffects')),
        (b'SNAM', text('Opt-in Arcane Circuit demo.')),
        (b'MAST', text('Skyrim.esm')), (b'DATA', b'\0' * 8),
    ])
    output = header
    for kind, form, parts in [(b'KYWD', IDS['keyword'], keyword),
                              (b'MGEF', IDS['effect'], effect),
                              (b'ENCH', IDS['enchantment'], enchantment),
                              (b'ARMO', IDS['armor'], armor)]:
        output += group(kind, record(kind, form, parts))
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--master', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    master = args.master.read_bytes()
    output = build(master)
    args.output.mkdir(parents=True, exist_ok=True)
    path = args.output / PLUGIN
    path.write_bytes(output)
    report = {'master_sha256': hashlib.sha256(master).hexdigest(),
              'plugin_sha256': hashlib.sha256(output).hexdigest(),
              'records': {k: f'0x{v & 0xFFFFFF:X}~{PLUGIN}' for k, v in IDS.items()}}
    (args.output / 'records.json').write_text(json.dumps(report, indent=2) + '\n')
    print(path)


if __name__ == '__main__':
    main()
