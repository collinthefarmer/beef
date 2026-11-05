"""Report whether Address Library databases hold given IDs, and at which offset."""
import argparse
import struct
from pathlib import Path

MAX_DENSE_IDS = 1 << 24


def read_v2(data: bytes, at: int) -> tuple[list[int], dict[int, int]]:
    def take(fmt: str) -> int:
        nonlocal at
        value = struct.unpack_from('<' + fmt, data, at)[0]
        at += struct.calcsize('<' + fmt)
        return value

    version = [take('i') for _ in range(4)]
    name_length = take('i')
    at += name_length
    pointer = take('i')
    count = take('i')
    offsets: dict[int, int] = {}
    previous_id = 0
    previous_offset = 0
    for _ in range(count):
        kind = take('B')
        low, high = kind & 0xF, kind >> 4
        identifier = {
            0: lambda: take('Q'), 1: lambda: previous_id + 1,
            2: lambda: previous_id + take('B'), 3: lambda: previous_id - take('B'),
            4: lambda: previous_id + take('H'), 5: lambda: previous_id - take('H'),
            6: lambda: take('H'), 7: lambda: take('I'),
        }[low]()
        base = previous_offset // pointer if high & 8 else previous_offset
        offset = {
            0: lambda: take('Q'), 1: lambda: base + 1,
            2: lambda: base + take('B'), 3: lambda: base - take('B'),
            4: lambda: base + take('H'), 5: lambda: base - take('H'),
            6: lambda: take('H'), 7: lambda: take('I'),
        }[high & 7]()
        if high & 8:
            offset *= pointer
        offsets[identifier] = offset
        previous_id, previous_offset = identifier, offset
    return version, offsets


def read_v5(data: bytes, at: int) -> tuple[list[int], dict[int, int]]:
    version = list(struct.unpack_from('<4I', data, at))
    at += 16 + 64 + 8
    count = struct.unpack_from('<i', data, at)[0]
    at += 4
    if not 0 <= count <= MAX_DENSE_IDS or len(data) < at + count * 4:
        raise ValueError('the dense offset table is truncated or too large')
    dense = struct.unpack_from(f'<{count}I', data, at)
    return version, {identifier: offset for identifier, offset in enumerate(dense) if offset}


def read_database(path: Path) -> tuple[list[int], dict[int, int]]:
    data = path.read_bytes()
    form = struct.unpack_from('<i', data, 0)[0]
    readers = {1: read_v2, 2: read_v2, 5: read_v5}
    if form not in readers:
        raise ValueError(f'{path.name}: unknown Address Library format {form}')
    return readers[form](data, 4)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ids', required=True, help='comma-separated IDs, e.g. 70967,208040')
    parser.add_argument('databases', nargs='+', type=Path, help='version-*.bin (SE) or versionlib-*.bin (AE) files')
    args = parser.parse_args()
    wanted = [int(text) for text in args.ids.split(',')]
    for path in args.databases:
        version, offsets = read_database(path)
        found = ', '.join(f'{identifier}={hex(offsets[identifier])}' if identifier in offsets
                          else f'{identifier}=MISSING' for identifier in wanted)
        print(f'{".".join(map(str, version))}: {found}')


if __name__ == '__main__':
    main()
