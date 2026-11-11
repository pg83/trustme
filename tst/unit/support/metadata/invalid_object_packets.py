from pathlib import Path
from struct import pack
from compression import zstd
from invalid_dependency_packets import crate_prefix


def packets():
    anonymous = crate_prefix("own_anon_module") + bytes([0, 0xFE])
    wrong_name = crate_prefix("own_wrong_class")
    wrong_name = wrong_name.replace(b"HIR::Module", b"HIR::Struct")
    wrong_name += bytes([0, 0xFD]) + pack("<I", 1)
    return {
        "own_anon_module.metadata": anonymous,
        "own_wrong_class.metadata": wrong_name,
    }


if __name__ == "__main__":
    for name, payload in packets().items():
        Path(__file__).with_name(name).write_bytes(zstd.compress(payload))
