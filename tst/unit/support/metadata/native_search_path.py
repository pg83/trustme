from pathlib import Path
from struct import pack, unpack_from
from compression import zstd


BASE = "own_native_search_contract.metadata"
DERIVED = "own_native_search_with_missing_directory.metadata"


def with_missing_directory():
    data = zstd.decompress(Path(__file__).with_name(BASE).read_bytes())
    strings = []
    at = 4
    for _ in range(unpack_from("<I", data)[0]):
        size = unpack_from("<I", data, at)[0]
        at += 4
        strings.append(data[at:at + size])
        at += size
    start = len(data) - 18
    assert start >= at and data[start] == 0xFD
    name = strings[unpack_from("<I", data, start + 1)[0]]
    assert b"vector" in name and b"basic_string" in name
    assert data[start + 5:] == b"\0" * 4 + b"\xff" + b"\0" * 8
    directory = b"/dev/null"
    replacement = pack("<II", 1, len(directory)) + directory
    changed = data[:start + 5] + replacement + data[start + 9:]
    assert changed[:start + 5] == data[:start + 5]
    assert changed[start + 5 + len(replacement):] == data[start + 9:]
    return changed


if __name__ == "__main__":
    Path(__file__).with_name(DERIVED).write_bytes(zstd.compress(with_missing_directory()))
