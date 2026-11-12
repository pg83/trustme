from compression import zstd
from pathlib import Path
from struct import pack, unpack_from
import hashlib
import json
import sys


def inline_checked_false_call(seed):
    data = zstd.decompress(seed)
    strings = []
    at = 4
    for _ in range(unpack_from("<I", data)[0]):
        length = unpack_from("<I", data, at)[0]
        at += 4
        strings.append(data[at:at + length])
        at += length
    assert b"own_checked_inline_seed" in strings

    def marker(name):
        return b"\xfd" + pack("<I", strings.index(name))

    statements = next(s for s in strings if b"vectorI12MIRStatement" in s)
    parameters = next(s for s in strings if b"vectorI8MIRParam" in s)
    wrappers = next(s for s in strings if b"MIRLValue7Wrapper" in s)
    place = lambda root: pack("<I", root) + marker(wrappers) + pack("<I", 0) + b"\xff"
    function = marker(b"HIR::Function")
    start = at
    entries = []
    while (start := data.find(function, start)) >= 0:
        assert data[start - 2:start] == b"\x01\x04"
        if unpack_from("<I", data, start - 6)[0] == strings.index(b"entry"):
            entries.append(start)
        start += len(function)
    assert len(entries) == 1
    body = data.index(marker(b"HIR::ExprPtr"), entries[0]) + 10
    length = unpack_from("<I", data, body - 4)[0]
    mir = bytearray(data[body:body + length])
    block = mir.index(marker(statements))
    call = block + 10
    assert unpack_from("<I", mir, block + 5)[0] == 0
    assert mir[call:call + 10] == b"\x0b" + pack("<I", 1) + b"\x02" + pack("<I", 2)
    args = mir.index(marker(parameters), call)
    expected = marker(parameters) + pack("<I", 3) + b"\x01" + place(4) + b"\x01" + place(8) + b"\x03\x04\x00\xff"
    assert mir[args:args + len(expected)] == expected
    paths = next(s for s in strings if b"ThinVectorI8RcString" in s)
    path = mir.index(marker(paths), call, args)
    assert unpack_from("<III", mir, path + 5) == (2, 0, strings.index(b"may_fail"))
    mir[path + 13:path + 17] = pack("<I", strings.index(b"stable"))
    forwarded = marker(parameters) + pack("<I", 2) + b"\x01" + place(4) + b"\x01" + place(8) + b"\xff"
    mir[args:args + len(expected)] = forwarded
    mir[call + 5:call + 10] = b"\x04"
    result = data[:body - 4] + pack("<I", len(mir)) + mir + data[body + length:]
    proof = dict(callee="stable", caller="entry", inner_callee="may_fail", supplied_fail_argument=False, unwind_action="Unreachable", inlining_hints_unchanged=True, eligible_callee_shape="Call, Return, UnwindResume", callee_value_operations_unchanged=True, caller_original_source_call_restored=True, no_unwind_reason="Restore entry's original call to stable,which passes constant false to may_fail;the only panic branch is guarded by fail and the value update uses wrapping addition.")
    return zstd.compress(result), proof


if __name__ == "__main__":
    source, target = map(Path, sys.argv[1:])
    seed = source.read_bytes()
    result, proof = inline_checked_false_call(seed)
    target.write_bytes(result)
    print(json.dumps(dict(seed_sha256=hashlib.sha256(seed).hexdigest(), fixture_sha256=hashlib.sha256(result).hexdigest(), transformation=proof), indent=2))
