from compression import zstd
from pathlib import Path
from struct import pack, unpack_from
import hashlib
import json
import sys


def mutable_reborrow_drops(seed):
    data = zstd.decompress(seed)
    strings = []
    at = 4
    for _ in range(unpack_from("<I", data)[0]):
        length = unpack_from("<I", data, at)[0]
        at += 4
        strings.append(data[at:at + length])
        at += length
    assert b"own_mutable_reborrow_seed" in strings

    def marker(name):
        return b"\xfd" + pack("<I", strings.index(name))

    def vector(name, values):
        return marker(name) + pack("<I", len(values)) + b"".join(values) + b"\xff"

    wrappers = next(s for s in strings if b"MIRLValue7Wrapper" in s)
    statements = next(s for s in strings if b"vectorI12MIRStatement" in s)
    blocks = next(s for s in strings if b"vectorI13MIRBasicBlock" in s)
    parameters = next(s for s in strings if b"vectorI8MIRParam" in s)

    def place(root, dereference=False):
        return pack("<I", root) + vector(wrappers, [pack("<I", 0)] if dereference else [])

    def assignment(destination, source):
        return marker(b"MIR::Statement") + b"\x00" + place(destination) + b"\x02\x01\x00" + place(source, True) + b"\xff"

    def drop(root, target):
        return b"\x0a\x01" + place(root) + pack("<II", 0xFFFFFFFF, target) + b"\x04"

    def block(items, terminator, cleanup=False):
        return vector(statements, items) + terminator + bytes([cleanup])

    expr = marker(b"HIR::ExprPtr")
    bodies = []
    pos = at
    while (pos := data.find(expr, pos)) >= 0:
        pos += len(expr)
        assert data[pos] == 1
        length = unpack_from("<I", data, pos + 1)[0]
        body = pos + 5
        bodies.append((body, body + length))
        pos = body + length
    assert len(bodies) == 2
    body, end = bodies[0]
    mir = data[body:end]
    start = mir.index(marker(blocks))
    assert unpack_from("<I", mir, start + 5)[0] == 4
    pos = start + 9
    rewritten = []
    for source, counter in [(1, 5), (13, 17)]:
        original_statements = vector(statements, [assignment(source, 4), assignment(counter, 8)])
        assert mir[pos:pos + len(original_statements)] == original_statements
        call_start = pos + len(original_statements)
        args = mir.index(marker(parameters), call_start)
        original_args = vector(parameters, [b"\x01" + place(source), b"\x01" + place(counter)])
        assert mir[args:args + len(original_args)] == original_args
        call_end = args + len(original_args) + 14
        replacement_args = vector(parameters, [b"\x02\x01" + place(1, True), b"\x01" + place(counter)])
        call = mir[call_start:args] + replacement_args + mir[args + len(original_args):call_end]
        items = ([assignment(1, 4)] if source == 1 else []) + [assignment(counter, 8)]
        rewritten.append(vector(statements, items) + call)
        pos = call_end
    cleanup = block([], b"\x03", True)
    assert mir.endswith(cleanup + b"\xff")
    returning = mir[pos:-len(cleanup) - 1]
    assert returning.endswith(b"\x02\x00")
    rewritten += [returning[:-2] + drop(1, 4) + b"\x00", cleanup, block([], drop(4, 5)), block([], b"\x02")]
    replacement = mir[:start] + vector(blocks, rewritten)
    result = data[:body - 4] + pack("<I", len(replacement)) + replacement + data[end:]
    proof = dict(function="twice", original_blocks=4, final_blocks=6, mutable_argument=0, mutable_reborrow_local=0, discarded_reference_roots=[1, 4], observation_calls_unchanged=True, semantics="Keep one mutable reborrow for both observation calls, reborrow it separately at each call, then discard the temporary and original reference. Dropping either reference does not drop its referent; the two counter updates and cleanup edges are preserved.")
    return zstd.compress(result), proof


if __name__ == "__main__":
    source, target = map(Path, sys.argv[1:])
    seed = source.read_bytes()
    result, proof = mutable_reborrow_drops(seed)
    target.write_bytes(result)
    print(json.dumps(dict(seed_sha256=hashlib.sha256(seed).hexdigest(), fixture_sha256=hashlib.sha256(result).hexdigest(), transformation=proof), indent=2))
