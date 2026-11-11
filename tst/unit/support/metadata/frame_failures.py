from pathlib import Path
from struct import pack, unpack_from
from compression import zstd


BASE = "own_control_codec_seed.metadata"


def frames():
    original = Path(__file__).with_name(BASE).read_bytes()
    payload = zstd.decompress(original)
    count = unpack_from("<I", payload)[0]
    end = 4
    for _ in range(count):
        size = unpack_from("<I", payload, end)[0]
        end += 4 + size
    padding = b"own_unused_frame_padding_" * 3200
    padded = (
        pack("<I", count + 1)
        + payload[4:end]
        + pack("<I", len(padding))
        + padding
        + payload[end:]
    )
    assert len(padded) > 65536

    checksum = {zstd.CompressionParameter.checksum_flag: 1}
    valid = zstd.compress(padded, options=checksum)

    def corrupt_checksum(data):
        changed = bytearray(data)
        changed[-1] ^= 0x80
        return bytes(changed)

    first_size = unpack_from("<I", payload, 4)[0]
    oversized_name = b"own_frame_name_" + b"x" * 65536
    oversized = (
        payload[:4]
        + pack("<I", len(oversized_name))
        + oversized_name
        + payload[8 + first_size:]
    )
    return {
        "frame_padded_valid.metadata": valid,
        "frame_unknown_size.metadata": zstd.compress(
            padded, options={zstd.CompressionParameter.content_size_flag: 0}
        ),
        "frame_late_checksum.metadata": corrupt_checksum(valid),
        "frame_early_checksum.metadata": corrupt_checksum(
            zstd.compress(payload, options=checksum)
        ),
        "frame_incomplete_name.metadata": zstd.compress(payload[:7]),
        "frame_oversized_name.metadata": zstd.compress(oversized),
        "frame_short_magic.metadata": original[:3],
    }


if __name__ == "__main__":
    for name, data in frames().items():
        Path(__file__).with_name(name).write_bytes(data)
