from pathlib import Path
from struct import pack
from compression import zstd


def crate_prefix(name):
    strings = [name.encode(), b"HIR::Module"]
    table = pack("<I", len(strings))
    table += b"".join(pack("<I", len(value)) + value for value in strings)
    return table + pack("<I", 0) + bytes([2])


def packets():
    invalid_bool = crate_prefix("own_invalid_bool") + bytes([2])
    invalid_close = crate_prefix("own_invalid_close") + bytes([0, 0xFD])
    invalid_close += pack("<I", 1) + pack("<III", 0, 0, 0) + bytes([0])
    return {
        "own_invalid_bool.metadata": invalid_bool,
        "own_invalid_close.metadata": invalid_close,
    }


if __name__ == "__main__":
    for name, payload in packets().items():
        Path(__file__).with_name(name).write_bytes(zstd.compress(payload))
