from compression import zstd
from pathlib import Path
from struct import pack, unpack_from
import hashlib
import json
import sys


def shared_reborrow_drops(seed):
    data = zstd.decompress(seed)
    strings = []
    at = 4
    for _ in range(unpack_from("<I", data)[0]):
        length = unpack_from("<I", data, at)[0]
        at += 4
        strings.append(data[at:at + length])
        at += length
    assert b"own_equivalent_mir_seed" in strings

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

    def block(items, terminator, cleanup=False):
        return vector(statements, items) + terminator + bytes([cleanup])

    def drop(root, target):
        return b"\x0a\x01" + place(root) + pack("<II", 0xFFFFFFFF, target) + b"\x04"

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
    assert len(bodies) == 3
    body, end = bodies[1]
    mir = data[body:end]
    start = mir.index(marker(blocks))
    assert unpack_from("<I", mir, start + 5)[0] == 5
    head = block([], b"\x07" + place(12) + pack("<II", 1, 2))
    assert mir[start + 9:start + 9 + len(head)] == head
    first = start + 9 + len(head)
    args = mir.index(marker(parameters), first)
    old_args = vector(parameters, [b"\x02\x00" + place(4, True), b"\x02\x01" + place(8, True)])
    assert mir[args:args + len(old_args)] == old_args
    call_end = args + len(old_args) + 14
    original_call_block = mir[first:call_end]
    assert original_call_block.startswith(vector(statements, []) + b"\x0b")
    assert mir[call_end:call_end + len(original_call_block)] == original_call_block
    assert mir[call_end + len(original_call_block):] == block([], b"\x02") + block([], b"\x03", True) + b"\xff"
    new_args = vector(parameters, [b"\x01" + place(1), b"\x02\x01" + place(8, True)])
    call = mir[first:args] + new_args + mir[args + len(old_args):call_end]
    assignment = marker(b"MIR::Statement") + b"\x00" + place(1) + b"\x02\x00\x00" + place(4, True) + b"\xff"
    new_head = block([assignment], b"\x07" + place(12) + pack("<II", 1, 2))
    replacement = mir[:start] + vector(blocks, [new_head, call, call, block([], drop(1, 5)), block([], b"\x03", True), block([], drop(4, 6)), block([], b"\x02")])
    result = data[:body - 4] + pack("<I", len(replacement)) + replacement + data[end:]
    proof = dict(function="borrowed", original_blocks=5, final_blocks=7, shared_argument=0, shared_reborrow_local=0, discarded_reference_roots=[1, 4], observation_calls_unchanged=True, semantics="Materialize the existing shared reborrow once and discard both shared reference values after the observation; dropping a shared reference does not drop its referent.")
    return zstd.compress(result), proof


if __name__ == "__main__":
    source, target = map(Path, sys.argv[1:])
    seed = source.read_bytes()
    result, proof = shared_reborrow_drops(seed)
    target.write_bytes(result)
    print(json.dumps(dict(seed_sha256=hashlib.sha256(seed).hexdigest(), fixture_sha256=hashlib.sha256(result).hexdigest(), transformation=proof), indent=2))
