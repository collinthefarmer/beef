"""Write ZIP archives whose bytes depend only on their file names and contents."""
from pathlib import Path
from typing import Iterable
import zipfile

FIXED_TIME = (1980, 1, 2, 0, 0, 0)
UNIX_SYSTEM = 3
FILE_MODE = 0o100644 << 16


def write_zip(path: Path, files: Iterable[tuple[str, bytes]]) -> None:
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(files):
            info = zipfile.ZipInfo(name, FIXED_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = UNIX_SYSTEM
            info.external_attr = FILE_MODE
            archive.writestr(info, data)
