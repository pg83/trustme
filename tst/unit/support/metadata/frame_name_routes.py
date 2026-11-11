from pathlib import Path
from struct import pack
from compression import zstd


BASE = "own_frame_wrapper.metadata"
DEPENDENCY = b"libown_control_codec_seed.rlib"
ROUTES = {
    "frame_wrapper_valid.metadata": "frame_padded_valid.metadata",
    "frame_wrapper_incomplete.metadata": "frame_incomplete_name.metadata",
    "frame_wrapper_oversized.metadata": "frame_oversized_name.metadata",
    "frame_wrapper_checksum.metadata": "frame_early_checksum.metadata",
}


def frames():
    payload = zstd.decompress(Path(__file__).with_name(BASE).read_bytes())
    old = pack("<I", len(DEPENDENCY)) + DEPENDENCY
    assert payload.count(old) == 1
    result = {}
    for name, filename in ROUTES.items():
        encoded = filename.encode()
        changed = payload.replace(old, pack("<I", len(encoded)) + encoded, 1)
        result[name] = zstd.compress(changed)
    return result


if __name__ == "__main__":
    for name, data in frames().items():
        Path(__file__).with_name(name).write_bytes(data)
