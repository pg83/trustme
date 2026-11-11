from pathlib import Path
from struct import unpack_from
from compression import zstd


BASE = "own_control_codec_seed.metadata"
DERIVED = "own_truncated_string_count.metadata"


def truncated_count():
    original = zstd.decompress(Path(__file__).with_name(BASE).read_bytes())
    assert len(original) > 4 and unpack_from("<I", original)[0] > 0
    return original[:3]


if __name__ == "__main__":
    Path(__file__).with_name(DERIVED).write_bytes(zstd.compress(truncated_count()))
