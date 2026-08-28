struct Settings;

namespace stl {
    class ObjPool;
}

#pragma once

#include "hir_type.h"
#include "hir_typeck_static.h"

#include <cstddef>

struct TargetArch {
    ::std::string name;
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
    ::std::string cCompiler;
    ::std::vector<::std::string> compilerOpts;
    ::std::vector<::std::string> linkerOptsPre;
    ::std::vector<::std::string> linkerOptsPost;
};

struct TargetSpec {
    ::std::string family;
    ::std::string osName;
    ::std::string envName;

    BackendOptsC backendC;
    TargetArch arch;
};

struct TypeReprFieldPath {
    enum : size_t {
        ARRAY_ELEMENT = static_cast<size_t>(-1),
    };

    size_t index;
    size_t size;
    ::std::vector<size_t> subFields;
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
    ::std::vector<U128> values;

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
        HIRTypeRef ty;
    };

    ::std::vector<Field> fields;

    size_t getOffset(const Span& sp, const StaticTraitResolve& resolve, const FieldPath& path) const;

    std::pair<unsigned, bool> getEnumVariant(const Span& sp, const StaticTraitResolve& resolve, const EncodedLiteralSlice& lit) const;
};

std::ostream& operator<<(std::ostream& os, const TypeRepr::FieldPath& x);

struct WireBoard;
class TargetLayoutContext;
extern TargetLayoutContext* TargetCreateLayoutContext(stl::ObjPool& pool);
extern const TargetSpec& TargetGetCurSpec(const WireBoard& wb);
extern void TargetSetCfg(WireBoard& wb, const ::std::string& targetName);
extern void TargetExportCurSpec(const WireBoard& wb, const ::std::string& filename);

static inline unsigned TargetGetPointerBits() {
    return 64;
}

extern bool TargetGetSizeOf(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* ty, size_t& outSize);
extern bool TargetGetAlignOf(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* ty, size_t& outAlign);
extern bool TargetGetSizeAndAlignOf(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* ty, size_t& outSize, size_t& outAlign);

extern bool TargetCapsMemberAlignment();

extern bool TargetTypeHasUserAlignment(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* ty);

extern const TypeRepr* TargetGetTypeRepr(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* ty);

extern bool TargetTypesAreTransmutable(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* src, const HIRTypeData* dst, bool assumeAlignment, bool assumeLifetimes, bool assumeSafety, bool assumeValidity);

extern const HIRTypeData* TargetGetInnerType(const Span& sp, const StaticTraitResolve& resolve, const TypeRepr& repr, size_t idx, const ::std::vector<size_t>& subFields = {}, size_t ofs = 0);
