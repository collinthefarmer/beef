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
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    data = presenter_dds()
    for index in range(512):
        path = args.output / f'slot_{index:02}.dds'
        if not path.exists() or path.read_bytes() != data:
            path.write_bytes(data)


if __name__ == '__main__':
    main()
