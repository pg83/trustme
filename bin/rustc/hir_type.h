#pragma once

#include "span.h"
#include "hir_path.h"
#include "hir_expr_ptr.h"
#include "hir_type_ref.h"
#include "hir_generic_ref.h"
#include "hir_generic_params.h"

#include <memory>
#include <unordered_map>

constexpr const char* CLOSURE_PATH_PREFIX = "closure#";
constexpr const char* GENERATOR_PATH_PREFIX = "generator#";
constexpr const char* PATH_PREFIX_FUTURE = "future#";
constexpr const char* ATY_PREFIX_ERASED = "erased#";

namespace stl {
    class ObjPool;
}

struct HIRTraitMarkings;
class HIRExternType;
class HIRStruct;
class HIRUnion;
class HIREnum;
class HIRFunction;
class HIRItemPath;
struct HIRExprNodeClosure;
struct HIRExprNodeGenerator;

enum class HIRCoreType {
    Usize,
    Isize,
    U8,
    I8,
    U16,
    I16,
    U32,
    I32,
    U64,
    I64,
    U128,
    I128,

    F16,
    F32,
    F64,
    F128,

    Bool,
    Char,
    Str,
};
extern std::ostream& operator<<(std::ostream& os, const HIRCoreType& ct);

bool isInteger(const HIRCoreType& v);

bool isFloat(const HIRCoreType& v);

enum class HIRBorrowType {
    Shared,
    Unique,
    Owned,
};
extern std::ostream& operator<<(std::ostream& os, const HIRBorrowType& bt);

#include "hir_type_binding_tu.h"

extern std::ostream& operator<<(std::ostream& os, const HIRArraySize& x);

struct HIRTypeDataPath {
    HIRPath path;
    HIRTypePathBinding binding;

    bool isClosure() const {
        return path.data.is_Generic() && path.data.as_Generic().path.components().back().size() > 8 && path.data.as_Generic().path.components().back().compare(0, strlen(CLOSURE_PATH_PREFIX), CLOSURE_PATH_PREFIX) == 0;
    }

    bool isGenerator() const {
        return path.data.is_Generic() && path.data.as_Generic().path.components().back().size() > 8 && path.data.as_Generic().path.components().back().compare(0, strlen(GENERATOR_PATH_PREFIX), GENERATOR_PATH_PREFIX) == 0;
    }

    bool isFuture() const {
        return path.data.is_Generic() && path.data.as_Generic().path.components().back().size() > 8 && path.data.as_Generic().path.components().back().compare(0, strlen(PATH_PREFIX_FUTURE), PATH_PREFIX_FUTURE) == 0;
    }
};

struct HIRTypeDataTraitObject {
    HIRTraitPath trait;
    std::vector<HIRGenericPath> markers;

    RcString lifetimeIdentity;

    bool lifetimeIdentityHasFree = false;
};

struct HIRTypeDataErasedTypeAliasInner {
    HIRGenericParams generics;
    HIRSimplePath path;
    HIRTypeRef type;

    HIRTypeDataErasedTypeAliasInner(const HIRItemPath& p, const HIRGenericParams& paramsOuter, const HIRGenericParams* paramsInner = nullptr);
    bool isLocalTo(const HIRSimplePath& p) const;
};

#include "hir_type_erased_tu.h"

extern Ordering ord(const TypeDataErasedTypeInner& a, const TypeDataErasedTypeInner& b);

static inline bool operator==(const TypeDataErasedTypeInner& a, const TypeDataErasedTypeInner& b) {
    return ord(a, b) == OrdEqual;
}

static inline bool operator!=(const TypeDataErasedTypeInner& a, const TypeDataErasedTypeInner& b) {
    return ord(a, b) != OrdEqual;
}

struct HIRTypeDataErasedType {
    bool isSized;
    std::vector<HIRTraitPath> traits;
    TypeDataErasedTypeInner inner;

    HIRPathParams use;

    enum class Use {
        OmittedOld,

        Omitted2024,

        Present,
    } usePresent;
};

struct HIRTypeDataFunctionPointer {
    bool isUnsafe;
    bool isVariadic;
    RcString abi;
    HIRTypeRef rettype;
    std::vector<HIRTypeRef> argTypes;

    bool trackCaller = false;

    RcString lifetimeIdentity;
    bool lifetimeIdentityHasFree = false;
};

struct HIRTypePatternRange {
    bool hasStart;
    HIRConstGeneric start;
    bool hasEnd;
    HIRConstGeneric end;
    bool endInclusive;

    HIRTypePatternRange clone() const;
    Ordering ord(const HIRTypePatternRange& x) const;
    void fmt(std::ostream& os) const;
};

struct HIRTypePattern {
    std::vector<HIRTypePatternRange> alternatives;

    HIRTypePattern clone() const;
    Ordering ord(const HIRTypePattern& x) const;
    void fmt(std::ostream& os) const;
};

struct HIRTypeDataInfer {
    unsigned int index;
    HIRInferClass tyClass;

    bool isLit() const {
        switch (this->tyClass) {
            case HIRInferClass::None: {
                return false;
            }
            case HIRInferClass::Integer:
            case HIRInferClass::Float: {
                return true;
            }
        }
        UNREACHABLE();
    }
};

class HIRTypeInterner;

struct HIRTypeDataNamedFunction {
    HIRPath path;
    HIRTypeDataNamedFunctionTy def;

    HIRTypeDataFunctionPointer decay(HIRTypeInterner& types, const Span& sp) const;
};

#include "hir_type_tu.h"

class HIRTypeInterner {
    stl::ObjPool& pool;
    u32& id;
    std::unordered_multimap<size_t, HIRTypeRef> nodes;

public:
    HIRTypeInterner(stl::ObjPool& pool, u32& id);

    stl::ObjPool& objectPool() const {
        return pool;
    }

    HIRTypeRef intern(HIRTypeData data);
    HIRTypeRef infer(unsigned int idx = ~0u, HIRInferClass tyClass = HIRInferClass::None);
    unsigned newAliasInputInfer();
    HIRTypeRef primitive(HIRCoreType ct);
    HIRTypeRef generic(HIRGenericRef generic);
    HIRTypeRef generic(RcString name, unsigned int slot);
    HIRTypeRef self();
    HIRTypeRef unit();
    HIRTypeRef diverge();
    HIRTypeRef borrow(HIRBorrowType bt, HIRTypeRef inner);
    HIRTypeRef pointer(HIRBorrowType bt, HIRTypeRef inner);
    HIRTypeRef tuple(std::vector<HIRTypeRef> types);
    HIRTypeRef slice(HIRTypeRef inner);
    HIRTypeRef array(HIRTypeRef inner, HIRArraySize size);
    HIRTypeRef array(HIRTypeRef inner, u64 size);
    HIRTypeRef array(HIRTypeRef inner, HIRConstGeneric size);
    HIRTypeRef path(HIRPath path, HIRTypePathBinding binding);
    HIRTypeRef function(HIRTypeDataFunctionPointer ft);
    HIRTypeRef closure(HIRExprNodeClosure* node);
    HIRTypeRef generator(HIRExprNodeGenerator* node);
    HIRTypeRef asyncBlock(HIRExprNodeAsyncBlock* node);
};

inline bool operator==(HIRTypeRef ty, HIRCoreType ct) {
    return ty && ty->is_Primitive() && ty->as_Primitive() == ct;
}

inline bool operator!=(HIRTypeRef ty, HIRCoreType ct) {
    return !(ty == ct);
}

extern std::ostream& operator<<(std::ostream& os, const HIRTypeData* ty);
