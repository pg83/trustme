#pragma once

#include "hir_type.h"
#include "hir_typeck_static.h"

#include <std/lib/vector.h>

#include <cstddef>

struct Settings;

namespace stl {
    class ObjPool;
}

struct TargetArch {
    std::string name;
    unsigned pointerBits;
    bool bigEndian;

    struct Atomics {
        bool u8 = true;
        bool u16 = true;
        bool u32 = true;
        bool u64 = false;
        bool ptr = true;

        Atomics(bool u8 = true, bool u16 = true, bool u32 = true, bool u64 = false, bool ptr = true);
    } atomics;

    struct Alignments {
        u8 u16;
        u8 u32;
        u8 u64;
        u8 u128;
        u8 f32;
        u8 f64;
        u8 ptr;

        Alignments(u8 u16 = 2, u8 u32 = 4, u8 u64 = 8, u8 u128 = 16, u8 f32 = 4, u8 f64 = 8, u8 ptr = 4);
    } alignments;
};

struct BackendOptsC {
    bool emulatedI128;
    std::string cCompiler;
    std::vector<std::string> compilerOpts;
    std::vector<std::string> linkerOptsPre;
    std::vector<std::string> linkerOptsPost;
};

struct TargetSpec {
    std::string family;
    std::string osName;
    std::string envName;

    BackendOptsC backendC;
    TargetArch arch;
};

struct TypeReprFieldPath {
    enum : size_t {
        ARRAY_ELEMENT = static_cast<size_t>(-1),
    };

    size_t index;
    size_t size;
    stl::Vector<size_t> subFields;
};

struct TypeReprVariantLinear {
    TypeReprFieldPath field;

    size_t offset;
    size_t numVariants;

    bool usesNiche() const {
        return !field.subFields.empty();
    }

    bool isNiche(unsigned varIdx) const {
        return usesNiche() && varIdx == field.index;
    }

    bool isTag(unsigned varIdx) const {
        return !usesNiche() && varIdx == field.index;
    }

    size_t nicheVariantStart() const;
    size_t nicheVariantCount() const;
    size_t tagValue(unsigned varIdx) const;
    unsigned decodeTag(U128 tag) const;
};

struct TypeReprVariantValues {
    TypeReprFieldPath field;
    stl::Vector<U128> values;

    bool isTag(unsigned varIdx) const {
        return varIdx == field.index;
    }
};

#include "trans_target_tu.h"

struct TypeRepr {
    size_t align = 0;
    size_t size = 0;

    bool userAlign = false;

    using FieldPath = TypeReprFieldPath;

    using VariantMode = TypeReprVariantMode;
    VariantMode variants;

    struct Field {
        size_t offset;
        const HIRTypeData* ty;
    };

    std::vector<Field> fields;

    size_t getOffset(const Span& sp, const StaticTraitResolve& resolve, const FieldPath& path) const;

    std::pair<unsigned, bool> getEnumVariant(const Span& sp, const StaticTraitResolve& resolve, const EncodedLiteralSlice& lit) const;
};

struct WireBoard;
void TargetCreateLayoutContext(WireBoard& wb, stl::ObjPool& pool);
const TargetSpec& TargetGetCurSpec(const WireBoard& wb);
void TargetSetCfg(WireBoard& wb, const std::string& targetName);
void TargetExportCurSpec(const WireBoard& wb, const std::string& filename);

static inline unsigned TargetGetPointerBits() {
    return 64;
}

bool TargetGetSizeOf(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* ty, size_t& outSize);
bool TargetGetAlignOf(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* ty, size_t& outAlign);
bool TargetGetSizeAndAlignOf(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* ty, size_t& outSize, size_t& outAlign);

bool TargetCapsMemberAlignment();

bool TargetTypeHasUserAlignment(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* ty);

const TypeRepr* TargetGetTypeRepr(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* ty);

bool TargetTypesAreTransmutable(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* src, const HIRTypeData* dst, bool assumeAlignment, bool assumeLifetimes, bool assumeSafety, bool assumeValidity);

const HIRTypeData* TargetGetInnerType(const Span& sp, const StaticTraitResolve& resolve, const TypeRepr& repr, size_t idx, const stl::Vector<size_t>& subFields = {}, size_t ofs = 0);
