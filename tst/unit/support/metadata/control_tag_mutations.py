from pathlib import Path
from struct import pack, unpack_from
from compression import zstd


BASE = "own_control_codec_seed.metadata"


def mutations():
    data = zstd.decompress(Path(__file__).with_name(BASE).read_bytes())
    strings = []
    at = 4
    for _ in range(unpack_from("<I", data)[0]):
        size = unpack_from("<I", data, at)[0]
        at += 4
        strings.append(data[at:at + size])
        at += size
    body_start = at

    def count(start):
        return unpack_from("<I", data, start)[0]

    def objects(name):
        marker = b"\xfd" + pack("<I", strings.index(name))
        result = []
        start = body_start
        while (start := data.find(marker, start)) >= 0:
            result.append(start + len(marker))
            start += len(marker)
        return result

    def vector_objects(fragment):
        names = [name for name in strings if fragment in name and b"vector" in name]
        assert len(names) == 1
        return objects(names[0])

    def first_element(fragment):
        return next(start + 4 for start in vector_objects(fragment) if count(start))

    def vector_end(start, element_size, empty=False):
        assert data[start] == 0xFD and count(start + 1) < len(strings)
        length = count(start + 5)
        if empty:
            assert length == 0
        end = start + 9 + length * element_size
        assert data[end] == 0xFF
        return end + 1

    def lvalue_end(start):
        assert count(start) != 3
        return vector_end(start + 4, 4)

    def path_end(start):
        return vector_end(start + 4, 4) if count(start) == 0xFFFFFFFF else start + 4

    def trait_end(start):
        assert data[start:start + 5] == b"\xfd" + pack("<I", strings.index(b"HIR::TraitPath"))
        start = path_end(start + 5)
        for _ in range(2):
            start = vector_end(start, 0, empty=True)
        assert count(start) == count(start + 4) == 0
        start += 9
        assert data[start] == 0xFF
        return start + 1

    terms = []
    for start in vector_objects(b"MIRStatement"):
        if count(start) == 0:
            assert data[start + 4] == 0xFF
            terms.append(start + 5)
    call = next(start for start in terms if data[start] == 11)
    unwind = call + 5
    assert data[unwind] in (1, 2, 3, 4)
    returned = unwind + (5 if data[unwind] == 2 else 1)
    call_target = lvalue_end(returned)
    assert data[call_target] in (1, 2, 3)

    switch = next(start for start in terms if data[start] == 9)
    switch_targets = lvalue_end(switch + 1) + 4
    switch_values = vector_end(switch_targets, 4)
    assert data[switch_values] in (1, 2, 3)

    bound = first_element(b"HIRGenericBound")
    assert data[bound] == 2
    types = objects(b"HIR::TypeData")
    array = next(start for start in types if data[start] == 8)
    assert count(array + 1) != 0xFFFFFFFF and data[array + 5] == 1
    const_generic = array + 6
    assert data[const_generic] == 3

    erased = next(start for start in types if data[start] == 7)
    traits = erased + 2
    assert data[erased + 1] in (0, 1) and data[traits] == 0xFD
    start = traits + 9
    for _ in range(count(traits + 5)):
        start = trait_end(start)
    assert data[start] == 0xFF
    start += 1
    for _ in range(2):
        start = vector_end(start, 0, empty=True)
    erased_inner = start + 1
    assert data[erased_inner] == 1

    asm = first_element(b"MIRAsmParam")
    assert data[asm] == 3 and data[asm + 2] in (1, 2)
    pattern = first_element(b"SimplePatEnt")
    assert data[pattern] == 7
    expansion = first_element(b"MacroExpansionEnt")
    assert data[expansion] == 0 and data[expansion + 2] == 1
    locations = [
        ("invalid_mir_terminator", terms[0]),
        ("invalid_mir_unwind_action", unwind),
        ("invalid_mir_switch_values", switch_values),
        ("invalid_mir_call_target", call_target),
        ("invalid_generic_bound", bound),
        ("invalid_const_generic", const_generic),
        ("invalid_erased_inner", erased_inner),
        ("invalid_asm_register", asm + 2),
        ("invalid_mir_asm_parameter", asm),
        ("invalid_macro_pattern", pattern),
        ("invalid_macro_expansion", expansion),
        ("invalid_token_data", expansion + 2),
    ]
    result = {}
    for name, offset in locations:
        assert body_start <= offset < len(data) and data[offset] != 255
        changed = bytearray(data)
        changed[offset] = 255
        assert sum(a != b for a, b in zip(data, changed)) == 1
        result[name + ".metadata"] = (bytes(changed), offset, data[offset], 255)
    return result


if __name__ == "__main__":
    for name, (payload, _, _, _) in mutations().items():
        Path(__file__).with_name("control_" + name).write_bytes(zstd.compress(payload))
