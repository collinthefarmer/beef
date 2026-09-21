import argparse
import pathlib
import struct


def presenter_dds():
    header = [124, 0x100F, 1, 1, 4, 0, 0] + [0] * 11
    header += [32, 0x41, 0, 32, 0xFF, 0xFF00, 0xFF0000, 0xFF000000]
    header += [0x1000, 0, 0, 0, 0]
    return b'DDS ' + struct.pack('<31I', *header) + bytes([0, 0, 0, 255])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--count', type=int, default=512)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    data = presenter_dds()
    kept = {f'slot_{index:02}.dds' for index in range(args.count)}
    for index in range(args.count):
        path = args.output / f'slot_{index:02}.dds'
        if not path.exists() or path.read_bytes() != data:
            path.write_bytes(data)
    for stale in args.output.glob('slot_*.dds'):
        if stale.name not in kept:
            stale.unlink()


if __name__ == '__main__':
    main()
