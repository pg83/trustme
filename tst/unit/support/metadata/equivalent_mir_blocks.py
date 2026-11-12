from compression import zstd
from pathlib import Path
from struct import pack, unpack_from
import hashlib
import json
import sys


def equivalent_blocks(seed):
    data = zstd.decompress(seed)
    strings = []
    at = 4
    for _ in range(unpack_from("<I", data)[0]):
        length = unpack_from("<I", data, at)[0]
        at += 4
        strings.append(data[at:at + length])
        at += length

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

    def if_argument(index):
        return bytes([7]) + place(4 * (index + 1)) + pack("<II", 1, 2)

    expr_marker = marker(b"HIR::ExprPtr")
    bodies = []
    start = at
    while (start := data.find(expr_marker, start)) >= 0:
        start += len(expr_marker)
        assert data[start] == 1
        length = unpack_from("<I", data, start + 1)[0]
        body = start + 5
        bodies.append((body, body + length))
        start = body + length
    assert len(bodies) == 3

    edits = []
    proof = []
    for name, (body, end) in zip(("data", "borrowed", "observe"), bodies):
        if name == "observe":
            continue
        start = data.index(marker(blocks), body, end)
        assert data[end - 1] == 0xFF
        if name == "data":
            assert unpack_from("<I", data, start + 5)[0] == 1
            statement = start + 9 + len(marker(statements)) + 4
            assert data[statement:statement + 5] == marker(b"MIR::Statement")
            assert data[end - 5:end] == b"\xff\xff\x02\x00\xff"
            assignment = data[statement:end - 4]
            prefix = marker(b"MIR::Statement") + b"\x00" + place(0)
            assert assignment.startswith(prefix + b"\x05" + place(4))
            assert len(assignment) == len(prefix) + 1 + len(place(4)) + 4 + 1
            assignment = prefix + b"\x09" + place(4) + b"\xff"
            same = block([assignment], b"\x06" + pack("<I", 3))
            replacements = [block([], if_argument(1)), same, same, block([], b"\x02")]
            explanation = "Normalize the slice-to-element-pointer cast into its equivalent DstPtr, then duplicate it into both flag branches; both write Return and jump to the same Return block."
        else:
            assert unpack_from("<I", data, start + 5)[0] == 5
            args = data.index(marker(parameters), start, end)
            assert unpack_from("<I", data, args + 5)[0] == 2
            old_args = vector(parameters, [b"\x01" + place(4), b"\x01" + place(1)])
            assert data[args:args + len(old_args)] == old_args
            before = data.rfind(b"\x0b" + pack("<I", 3) + b"\x02" + pack("<I", 4), start, args)
            assert before >= 0
            target = before + 10 + len(place(5))
            assert data[before + 10:target] == place(5)
            assert data[target] == 2
            source = args + len(old_args)
            new_args = vector(parameters, [b"\x02\x00" + place(4, True), b"\x02\x01" + place(8, True)])
            call = b"\x0b" + pack("<I", 3) + b"\x02" + pack("<I", 4) + place(0) + data[target:args] + new_args + data[source:source + 13]
            same = block([], call)
            replacements = [block([], if_argument(2)), same, same, block([], b"\x02"), block([], b"\x03", True)]
            explanation = "Replace temporary reborrows with the same shared/unique Borrow arguments; discard unused unit temporaries and share the existing empty return/unwind continuations."
        replacement = data[body:start] + vector(blocks, replacements)
        edits.append((body - 4, end, pack("<I", len(replacement)) + replacement))
        proof.append(dict(function=name, original_bytes=end - body, final_bytes=len(replacement), equivalent_branches=replacements[1] == replacements[2], semantics=explanation))
    for start, end, replacement in sorted(edits, reverse=True):
        data = data[:start] + replacement + data[end:]
    return zstd.compress(data), proof


if __name__ == "__main__":
    source, target = map(Path, sys.argv[1:])
    seed = source.read_bytes()
    result, proof = equivalent_blocks(seed)
    target.write_bytes(result)
    print(json.dumps(dict(seed_sha256=hashlib.sha256(seed).hexdigest(), fixture_sha256=hashlib.sha256(result).hexdigest(), transformations=proof), indent=2))
