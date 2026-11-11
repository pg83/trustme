from pathlib import Path
from struct import pack, unpack_from
from compression import zstd


def frames():
    folder = Path(__file__).parent
    payload = zstd.decompress((folder / "own_control_codec_seed.metadata").read_bytes())
    old_size = unpack_from("<I", payload, 4)[0]
    name = b"own_frame_name_" + b"x" * (128 * 1024 + 17)
    changed = payload[:4] + pack("<I", len(name)) + name + payload[8 + old_size:]
    wrapper = zstd.decompress((folder / "own_frame_wrapper.metadata").read_bytes())
    old = b"libown_control_codec_seed.rlib"
    new = b"frame_beyond_header.metadata"
    old = pack("<I", len(old)) + old
    assert wrapper.count(old) == 1
    wrapper = wrapper.replace(old, pack("<I", len(new)) + new, 1)
    return {
        "frame_beyond_header.metadata": zstd.compress(changed),
        "frame_wrapper_beyond_header.metadata": zstd.compress(wrapper),
    }


if __name__ == "__main__":
    for name, data in frames().items():
        Path(__file__).with_name(name).write_bytes(data)
