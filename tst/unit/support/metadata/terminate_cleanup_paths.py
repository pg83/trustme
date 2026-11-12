from compression import zstd
from pathlib import Path
from struct import pack, unpack_from
import hashlib
import json
import sys


def terminate_cleanup_paths(seed):
    data = zstd.decompress(seed)
    strings = []
    at = 4
    for _ in range(unpack_from("<I", data)[0]):
        length = unpack_from("<I", data, at)[0]
        at += 4
        strings.append(data[at:at + length])
        at += length
    assert b"own_unwind_term_seed" in strings and b"C" in strings

    def marker(name):
        return b"\xfd" + pack("<I", strings.index(name))

    body = data.index(marker(b"HIR::ExprPtr"), at) + 10
    length = unpack_from("<I", data, body - 4)[0]
    mir = bytearray(data[body:body + length])
    blocks = next(s for s in strings if b"vectorI13MIRBasicBlock" in s)
    statements = next(s for s in strings if b"vectorI12MIRStatement" in s)
    empty = marker(statements) + pack("<I", 0) + b"\xff"
    first = mir.index(marker(blocks)) + 5
    assert unpack_from("<I", mir, first)[0] == 5
    cleanup = b"\x0b" + pack("<I", 3) + b"\x02" + pack("<I", 4)
    calls = []
    start = 0
    while (start := mir.find(cleanup, start)) >= 0:
        calls.append(start)
        start += len(cleanup)
    assert len(calls) == 2
    mir[calls[1] + 6:calls[1] + 10] = pack("<I", 5)
    assert mir.endswith(empty + b"\x03\x01\xff")
    tail = len(mir) - len(empty) - 3
    goto = empty + b"\x06" + pack("<I", 6) + b"\x01"
    terminate = empty + b"\x04\x01"
    mir[tail:] = goto + goto + terminate + b"\xff"
    mir[first:first + 4] = pack("<I", 7)
    result = data[:body - 4] + pack("<I", len(mir)) + mir + data[body + length:]
    proof = dict(function="guarded", abi="C", original_blocks=5, final_blocks=7, call_cleanup_targets=[4, 5], cleanup_goto_target=6, target_terminator="UnwindTerminate", rust_value_operations_unchanged=True, pulse_body_unchanged=True)
    return zstd.compress(result), proof


if __name__ == "__main__":
    source, target = map(Path, sys.argv[1:])
    seed = source.read_bytes()
    result, proof = terminate_cleanup_paths(seed)
    target.write_bytes(result)
    print(json.dumps(dict(seed_sha256=hashlib.sha256(seed).hexdigest(), fixture_sha256=hashlib.sha256(result).hexdigest(), transformation=proof), indent=2))
