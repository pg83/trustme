#include "coretypes.h"

#include "trace.h"

#include <cstring>

using namespace stl;

namespace {
    const struct {
        const char* name;
        enum eCoreType type;
    } CORETYPES[] = {
        {"_", CORETYPE_ANY},
        {"bool", CORETYPE_BOOL},
        {"char", CORETYPE_CHAR},
        {"f128", CORETYPE_F128},
        {"f16", CORETYPE_F16},
        {"f32", CORETYPE_F32},
        {"f64", CORETYPE_F64},
        {"i128", CORETYPE_I128},
        {"i16", CORETYPE_I16},
        {"i32", CORETYPE_I32},
        {"i64", CORETYPE_I64},
        {"i8", CORETYPE_I8},
        {"isize", CORETYPE_INT},
        {"str", CORETYPE_STR},
        {"u128", CORETYPE_U128},
        {"u16", CORETYPE_U16},
        {"u32", CORETYPE_U32},
        {"u64", CORETYPE_U64},
        {"u8", CORETYPE_U8},
        {"usize", CORETYPE_UINT},
    };
}

enum eCoreType coretypeFromstring(const char* name) {
    for (unsigned int i = 0; i < sizeof(CORETYPES) / sizeof(CORETYPES[0]); i++) {
        int cmp = strcmp(name, CORETYPES[i].name);
        if (cmp < 0) {
            break;
        }
        if (cmp == 0) {
            return CORETYPES[i].type;
        }
    }
    return CORETYPE_INVAL;
}

const char* coretypeName(const eCoreType ct) {
    switch (ct) {
        case CORETYPE_INVAL:
            return "INVAL";
        case CORETYPE_ANY:
            return "_/*CORETYPE_ANY*/";
        case CORETYPE_CHAR:
            return "char";
        case CORETYPE_STR:
            return "str";
        case CORETYPE_BOOL:
            return "bool";
        case CORETYPE_UINT:
            return "usize";
        case CORETYPE_INT:
            return "isize";
        case CORETYPE_U8:
            return "u8";
        case CORETYPE_I8:
            return "i8";
        case CORETYPE_U16:
            return "u16";
        case CORETYPE_I16:
            return "i16";
        case CORETYPE_U32:
            return "u32";
        case CORETYPE_I32:
            return "i32";
        case CORETYPE_U64:
            return "u64";
        case CORETYPE_I64:
            return "i64";
        case CORETYPE_U128:
            return "u128";
        case CORETYPE_I128:
            return "i128";
        case CORETYPE_F16:
            return "f16";
        case CORETYPE_F32:
            return "f32";
        case CORETYPE_F64:
            return "f64";
        case CORETYPE_F128:
            return "f128";
    }
    DEBUG(StringView("Unknown core type?! ") << static_cast<unsigned>(ct));
    return "NFI";
}

template <>
void stl::output<ZeroCopyOutput, eCoreType>(ZeroCopyOutput& os, const eCoreType ct) {
    os << coretypeName(ct);
}
