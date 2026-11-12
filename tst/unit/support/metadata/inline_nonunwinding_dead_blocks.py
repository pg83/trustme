from compression import zstd
from pathlib import Path
from struct import pack, unpack_from
import hashlib
import json
import sys


def inline_nonunwinding_dead_blocks(seed):
    data = zstd.decompress(seed)
    strings = []
    at = 4
    for _ in range(unpack_from("<I", data)[0]):
        length = unpack_from("<I", data, at)[0]
        at += 4
        strings.append(data[at:at + length])
        at += length
    assert b"own_unwind_term_seed" in strings

    def marker(name):
        return b"\xfd" + pack("<I", strings.index(name))

    marking = marker(b"HIR::Function::Markings")
    first = data.index(marking, at)
    second = data.index(marking, first + len(marking))
    assert data.find(marking, second + len(marking)) < 0
    hint = second + len(marking)
    assert data[hint] == 0xFD and unpack_from("<I", data, hint + 5)[0] == 0
    assert data[hint + 9] == 0xFF
    hint += 10
    assert data[hint] == 0 and unpack_from("<I", data, hint + 1)[0] == strings.index(b"")
    assert data[hint + 5:hint + 11] == bytes(6)
    hint += 11
    assert data[hint:hint + 5] == bytes(5)
    hint += 5
    assert data[hint:hint + 2] == b"\x01\xff"
    data = data[:hint] + b"\x03" + data[hint + 1:]

    body = data.index(marker(b"HIR::ExprPtr"), at) + 10
    length = unpack_from("<I", data, body - 4)[0]
    mir = bytearray(data[body:body + length])
    blocks = next(s for s in strings if b"vectorI13MIRBasicBlock" in s)
    statements = next(s for s in strings if b"vectorI12MIRStatement" in s)
    block_count = mir.index(marker(blocks)) + 5
    assert unpack_from("<I", mir, block_count)[0] == 7
    calls = []
    for target in (4, 5):
        pattern = b"\x0b" + pack("<I", 3) + b"\x02" + pack("<I", target)
        start = mir.index(pattern)
        calls.append(start)
    assert len(set(calls)) == 2
    for start in sorted(calls, reverse=True):
        mir[start + 5:start + 10] = b"\x04"
    assert mir[-1] == 0xFF
    dead = marker(statements) + pack("<I", 0) + b"\xff\x01\x00"
    mir[-1:] = dead + b"\xff"
    mir[block_count:block_count + 4] = pack("<I", 8)
    result = data[:body - 4] + pack("<I", len(mir)) + mir + data[body + length:]
    proof = dict(function="guarded", old_blocks=7, final_blocks=8, dead_block=7, dead_block_terminator="Incomplete", dead_block_has_no_predecessor=True, pulse_inlining_hint="Always", pulse_value_operations_unchanged=True, call_unwind_actions=["Unreachable", "Unreachable"], no_unwind_reason="pulse consists of size_of<T>, unchecked usize addition and unit return; it borrows T and invokes no user code.")
    return zstd.compress(result), proof


if __name__ == "__main__":
    source, target = map(Path, sys.argv[1:])
    seed = source.read_bytes()
    result, proof = inline_nonunwinding_dead_blocks(seed)
    target.write_bytes(result)
    print(json.dumps(dict(seed_sha256=hashlib.sha256(seed).hexdigest(), fixture_sha256=hashlib.sha256(result).hexdigest(), transformation=proof), indent=2))
