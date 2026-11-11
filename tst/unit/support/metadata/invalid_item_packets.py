from pathlib import Path
from struct import pack
from compression import zstd


def item_prefix(name, map_index, item):
    strings = [name.encode(), b"HIR::Module", b"ITEM", b"HIR::MacroItem"]
    table = pack("<I", len(strings))
    table += b"".join(pack("<I", len(value)) + value for value in strings)
    data = table + pack("<I", 0) + bytes([2, 0, 0xFD]) + pack("<I", 1)
    data += pack("<I", 0) * map_index
    return data + pack("<II", 1, 2) + bytes([1]) + item


def packets():
    return {
        "own_bad_value_item.metadata": item_prefix(
            "own_bad_value_item", 0, bytes([255])
        ),
        "own_bad_type_item.metadata": item_prefix(
            "own_bad_type_item", 1, bytes([255])
        ),
        "own_bad_macro_item.metadata": item_prefix(
            "own_bad_macro_item", 2, bytes([0xFD]) + pack("<I", 3) + bytes([255])
        ),
    }


if __name__ == "__main__":
    for name, payload in packets().items():
        Path(__file__).with_name(name).write_bytes(zstd.compress(payload))
