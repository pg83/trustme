from pathlib import Path
from struct import pack, unpack_from
from compression import zstd


BASE = "own_nested_codec_seed.metadata"


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

    def objects(name):
        marker = bytes([0xFD]) + pack("<I", strings.index(name))
        start = body_start
        result = []
        while (start := data.find(marker, start)) >= 0:
            result.append(start + len(marker))
            start += len(marker)
        return result

    def vector(start, empty=False):
        assert data[start] == 0xFD
        index, count = unpack_from("<II", data, start + 1)
        assert index < len(strings)
        if empty:
            assert count == 0 and data[start + 9] == 0xFF
        return start + 10, count

    def empty_generics(start):
        assert unpack_from("<I", data, start)[0] == 0
        start += 4
        for _ in range(3):
            start, _ = vector(start, empty=True)
        return start

    def scalar_type_end(start):
        index = unpack_from("<I", data, start)[0]
        if index != 0xFFFFFFFF:
            return start + 4
        marker = bytes([0xFD]) + pack("<I", strings.index(b"HIR::TypeData"))
        assert data[start + 4:start + 9] == marker
        assert data[start + 9] == 3 and data[start + 11] == 0xFF
        return start + 12

    type_tags = objects(b"HIR::TypeData")
    assert type_tags
    path_tag = next(pos + 1 for pos in type_tags if data[pos] == 4)
    assert data[path_tag] in (0, 1, 2)
    array_inner = next(pos + 1 for pos in type_tags if data[pos] == 8)
    array_size = scalar_type_end(array_inner)
    struct_start = empty_generics(objects(b"HIR::Struct")[0])
    enum_start = empty_generics(objects(b"HIR::Enum")[0])
    statements = objects(b"MIR::Statement")
    assert statements
    rvalues = []
    for pos in statements:
        if data[pos] != 0:
            continue
        lvalue = pos + 1
        if unpack_from("<I", data, lvalue)[0] == 3:
            continue
        after_wrappers, count = vector(lvalue + 4, empty=True)
        assert count == 0
        rvalues.append(after_wrappers)
    assert rvalues
    constant = next(pos + 1 for pos in rvalues if data[pos] == 3)
    parameter = None
    for index, name in enumerate(strings):
        if b"MIRParam" not in name or b"vector" not in name:
            continue
        for start in objects(name):
            count = unpack_from("<I", data, start)[0]
            if count:
                parameter = start + 4
                break
        if parameter is not None:
            break
    assert parameter is not None and data[parameter] in (1, 2, 3)
    locations = [
        ("invalid_type", type_tags[0], 255),
        ("invalid_path", path_tag, 255),
        ("invalid_array_size", array_size, 255),
        ("invalid_struct_data", struct_start + 1, 255),
        ("invalid_enum_class", enum_start + 7, 255),
        ("invalid_mir_statement", statements[0], 255),
        ("obsolete_mir_drop", statements[0], 1),
        ("invalid_mir_rvalue", rvalues[0], 255),
        ("invalid_mir_constant", constant, 255),
        ("invalid_mir_parameter", parameter, 255),
    ]
    result = {}
    for name, offset, replacement in locations:
        assert body_start <= offset < len(data) and data[offset] != replacement
        changed = bytearray(data)
        changed[offset] = replacement
        assert sum(a != b for a, b in zip(data, changed)) == 1
        result[name + ".metadata"] = (bytes(changed), offset, data[offset], replacement)
    return result


if __name__ == "__main__":
    for name, (payload, _, _, _) in mutations().items():
        Path(__file__).with_name(name).write_bytes(zstd.compress(payload))
