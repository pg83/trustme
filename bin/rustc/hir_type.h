#pragma once

#include "span.h"
#include "output.h"
#include "hir_path.h"
#include "hir_expr_ptr.h"
#include "hir_type_ref.h"
#include "hir_generic_ref.h"
#include "hir_generic_params.h"

#include <std/lib/vector.h>

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
bool isInteger(const HIRCoreType& v);

bool isFloat(const HIRCoreType& v);

constexpr u32 SIMPLIFIED_TYPE_TAG_BASE = 1;
constexpr u32 SIMPLIFIED_TYPE_PRIMITIVE_BASE = 32;
constexpr u32 SIMPLIFIED_TYPE_COUNT = SIMPLIFIED_TYPE_PRIMITIVE_BASE + static_cast<u32>(HIRCoreType::Str) + 1;

enum class HIRBorrowType {
    Shared,
    Unique,
    Owned,
};
#include "hir_type_binding_tu.h"

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
    const HIRType* type;

    HIRTypeDataErasedTypeAliasInner(const HIRItemPath& p, const HIRGenericParams& paramsOuter, const HIRGenericParams* paramsInner = nullptr);
    bool isLocalTo(const HIRSimplePath& p) const;
};

#include "hir_type_erased_tu.h"

Ordering ord(const TypeDataErasedTypeInner& a, const TypeDataErasedTypeInner& b);

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
    const HIRType* rettype;
    stl::Vector<const HIRType*> argTypes;

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
    void fmt(stl::ZeroCopyOutput& os) const;
};

struct HIRTypePattern {
    std::vector<HIRTypePatternRange> alternatives;

    HIRTypePattern clone() const;
    Ordering ord(const HIRTypePattern& x) const;
    void fmt(stl::ZeroCopyOutput& os) const;
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

struct HIRTypeInterner;

struct HIRTypeDataNamedFunction {
    HIRPath path;
    HIRTypeDataNamedFunctionTy def;

    HIRTypeDataFunctionPointer decay(HIRTypeInterner& types, const Span& sp) const;
};

#include "hir_type_tu.h"

class HIRMatchGenerics {
protected:
    struct BorrowMatchedValues {};

    explicit HIRMatchGenerics(BorrowMatchedValues)
        : retainedValuePool(nullptr)
    {
    }

    explicit HIRMatchGenerics(stl::ObjPool& retainedValuePool)
        : retainedValuePool(&retainedValuePool)
    {
    }

public:
    HIRCompare cmpPath(const Span& sp, const HIRPath& tyL, const HIRPath& tyR, tCbResolveType resolveCb);
    virtual HIRCompare cmpType(const Span& sp, const HIRType* tyL, const HIRType* tyR, tCbResolveType resolveCb);

    virtual HIRCompare matchTy(const HIRGenericRef& g, const HIRType* ty, tCbResolveType resolveCb) = 0;
    virtual HIRCompare matchVal(const HIRGenericRef& g, const HIRConstGeneric& sz) = 0;

private:
    stl::ObjPool* retainedValuePool;
};

struct HIRTypeInterner {
    const HIRType* selfParam = nullptr;

    virtual stl::ObjPool& objectPool() const = 0;
    virtual const HIRType* intern(HIRType data) = 0;
    virtual const HIRType* pathType(const HIRPath& shape, const HIRType* const* children, const HIRTypePathBinding& binding) = 0;
    virtual unsigned newAliasInputInfer() = 0;

    const HIRType* internFolded(const HIRType* original, HIRType data);

    const HIRType* infer(unsigned int idx = ~0u, HIRInferClass tyClass = HIRInferClass::None);
    const HIRType* primitive(HIRCoreType ct);
    const HIRType* generic(HIRGenericRef generic);
    const HIRType* generic(RcString name, unsigned int slot);
    const HIRType* self();
    const HIRType* unit();
    const HIRType* diverge();
    const HIRType* borrow(HIRBorrowType bt, const HIRType* inner);
    const HIRType* pointer(HIRBorrowType bt, const HIRType* inner);
    const HIRType* tuple(stl::Vector<const HIRType*> types);
    const HIRType* slice(const HIRType* inner);
    const HIRType* array(const HIRType* inner, HIRArraySize size);
    const HIRType* array(const HIRType* inner, u64 size);
    const HIRType* array(const HIRType* inner, HIRConstGeneric size);
    const HIRType* path(HIRPath path, HIRTypePathBinding binding);
    const HIRType* function(HIRTypeDataFunctionPointer ft);
    const HIRType* closure(HIRExprNodeClosure* node);
    const HIRType* generator(HIRExprNodeGenerator* node);
    const HIRType* asyncBlock(HIRExprNodeAsyncBlock* node);

    static HIRTypeInterner* create(stl::ObjPool& pool, u32& id);
};

bool hirPathParamsIdentical(const HIRPathParams& a, const HIRPathParams& b);

bool hirTraitPathIdentical(const HIRTraitPath& a, const HIRTraitPath& b);

size_t hirTraitPathHash(const HIRTraitPath& path);

inline bool operator==(const HIRType* ty, HIRCoreType ct) {
    return ty && ty->is_Primitive() && ty->as_Primitive() == ct;
}

inline bool operator!=(const HIRType* ty, HIRCoreType ct) {
    return !(ty == ct);
}
