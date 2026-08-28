#pragma once

#include "int128.h"
#include "hir_asm.h"
#include "hir_path.h"
#include "hir_type.h"
#include "settings.h"
#include "ast_edition.h"
#include "hir_pattern.h"
#include "hir_expr_ptr.h"
#include "target_version.h"
#include "hir_generic_params.h"
#include "hir_encoded_literal.h"
#include "macro_rules_macro_rules_ptr.h"

#include <set>
#include <memory>
#include <vector>
#include <cassert>
#include <optional>
#include <unordered_map>

class Monomorphiser;

namespace stl {
    class ObjPool;
}

class HIRCrate;
class HIRModule;

class HIRFunction;
class HIRStatic;

class HIRValueItem;
class HIRTypeItem;
class HIRMacroItem;

class HIRItemPath;
struct WireBoard;

class HIRPublicity {
    enum class Kind {
        Global,
        None,
        Restricted,
    };

    Kind kind;
    ::std::shared_ptr<HIRSimplePath> visPath;

    HIRPublicity(Kind kind, ::std::shared_ptr<HIRSimplePath> p);

public:
    static HIRPublicity newGlobal() {
        return HIRPublicity(Kind::Global, {});
    }

    static HIRPublicity newNone() {
        return HIRPublicity(Kind::None, {});
    }

    static HIRPublicity newPriv(HIRSimplePath p);

    bool isGlobal() const {
        return kind == Kind::Global;
    }

    bool isVisible(const HIRSimplePath& p) const;

    friend ::std::ostream& operator<<(::std::ostream& os, const HIRPublicity& x);
};

enum class HIRConstEvalState {
    None,
    Active,
    Complete,
};

template <typename Ent>
struct HIRVisEnt {
    HIRPublicity publicity;
    Ent ent;
};

struct HIRLinkage {
    enum class Type {
        Auto,
        Weak,
        External,
        ExternWeak,
    };

    Type type = Type::Auto;

    ::std::string name;

    ::std::string section;
};

class HIRStatic {
public:
    HIRGenericParams params;

    HIRLinkage linkage;
    bool isMut;
    HIRTypeRef type;

    size_t explicitAlignment = 0;

    HIRExprPtr value;

    EncodedLiteral valueRes;
    bool valueGenerated = false;

    bool valueEvaluating = false;
    bool saveLiteral = false;
    bool noEmitValue = false;

    bool isPromoted = false;

    mutable ::std::map<HIRPath, EncodedLiteral> monomorphCache;

    HIRStatic(HIRLinkage linkage, bool isMut, HIRTypeRef type, HIRExprPtr value);
};

class HIRConstant {
public:
    HIRGenericParams params;

    HIRTypeRef type;
    HIRExprPtr value;

    EncodedLiteral valueRes;
    enum class ValueState {
        Unknown,

        InProgress,
        Generic,
        Known
    } valueState = ValueState::Unknown;

    // TODO: Wait, how?
    mutable ::std::map<HIRPath, EncodedLiteral> monomorphCache;

    HIRConstant();

    HIRConstant(HIRGenericParams params, HIRTypeRef type, HIRExprPtr value);
};

class HIRFunction {
public:
    enum class Receiver {
        Free,
        Value,
        BorrowOwned,
        BorrowUnique,
        BorrowShared,

        Box,
        Custom,
    };

    typedef ::std::vector<::std::pair<HIRPattern, HIRTypeRef>> argsT;

    bool saveCode = false;
    HIRLinkage linkage;

    Receiver receiver = Receiver::Free;
    ::std::optional<HIRTypeRef> receiverType;
    RcString abi = RcString::newInterned(ABI_RUST);
    bool unsafe = false;
    bool isConst = false;

    HIRGenericParams params;

    argsT args;
    bool variadic = false;
    bool hasNamedVariadic = false;
    HIRTypeRef returnType;

    ::std::optional<HIRTypeRef> traitReturnType;

    SourceLocation source;
    HIRExprPtr code;

    std::vector<HIRSimplePath> defineOpaque;

    struct Markings {
        std::vector<unsigned> rustcLegacyConstGenerics;
        bool trackCaller = false;

        bool mustUse = false;
        bool isNaked = false;

        u64 alignment = 0;

        bool isRustcIntrinsic = false;
        bool isRustcPromotable = false;

        enum Inline {
            Auto,
            Never,
            Normal,
            Always
        } inlineType = Inline::Auto;

        LintLevelOverrides lintLevels;
    } markings;

    HIRFunction();

    HIRFunction(Receiver receiver, HIRGenericParams params, argsT args, HIRTypeRef retTy, HIRExprPtr code);

    size_t fixedArgCount() const {
        assert(!hasNamedVariadic || (variadic && !args.empty()));
        return args.size() - hasNamedVariadic;
    }

    HIRTypeRef makePtrTy(const Span& sp, const Monomorphiser& ms) const;
};

struct HIRTypeAlias {
    HIRGenericParams params;
    HIRTypeRef type;
};

struct HIRTraitAlias {
    HIRGenericParams params;
    ::std::vector<HIRTraitPath> traits;
};

typedef ::std::vector<HIRVisEnt<HIRTypeRef>> tTupleFields;

struct HIRStructField {
    RcString name;
    HIRPublicity vis;
    HIRTypeRef ty;

    ::std::unique_ptr<HIRGenericPath> defaultValue;
};

typedef ::std::vector<HIRStructField> tStructFields;

extern HIRTypeRef fnPtrTupleConstructor(const Span& sp, const Monomorphiser& ms, HIRTypeRef retTy, const tTupleFields& types);

struct HIRTraitMarkings {
    bool hasADeref = false;

    bool hasDropImpl = false;

    bool hasConstDropImpl = false;

    bool isCopy = false;

    struct AutoMarking {
        ::std::vector<HIRTypeRef> conditions;

        bool isImpled;
    };

    mutable ::std::map<HIRSimplePath, AutoMarking> autoImpls;
};

struct HIRStructMarkings {
    bool isAsyncDropGlue = false;

    bool isFundamental = false;

    bool canUnsize = false;

    unsigned int unsizedParam = ~0u;

    // TODO: This would have to be changed for custom DSTs
    enum class DstType {
        None,
        Possible,
        Slice,
        TraitObject,
        Projection,
    } dstType = DstType::None;
    unsigned int unsizedField = ~0u;

    enum class Coerce {
        None,
        Passthrough,
        Pointer,
    } coerceUnsized = Coerce::None;

    unsigned int coerceUnsizedIndex = ~0u;

    unsigned int coerceParam = ~0u;

    bool isNonzero = false;

    bool isNoNiche = false;

    bool boundedMax = false;
    U128 boundedMaxValue;
};

class HIRExternType {
public:
    // TODO: do extern types need any associated data?
    HIRTraitMarkings markings;
};

struct HIREnumDataVariant {
    RcString name;
    bool isStruct;
    HIRTypeRef type;

    HIRExprPtr discriminantExpr;

    U128 discriminantValue = U128(0);

    bool valueKnown = false;
};

struct HIREnumValueVariant {
    RcString name;
    HIRExprPtr expr;
    // TODO: Signed.
    U128 val = U128(0);

    bool valueKnown = false;
};

#include "hir_hir_enum_tu.h"

class HIREnum {
public:
    using DataVariant = HIREnumDataVariant;
    using ValueVariant = HIREnumValueVariant;
    using Class = HIREnumClass;

    enum class Repr {
        Auto,
        Usize,
        U8,
        U16,
        U32,
        U64,
        Isize,
        I8,
        I16,
        I32,
        I64,
        U128,
        I128,
    };

    HIRGenericParams params;
    bool isCRepr;
    Repr tagRepr;
    Class data;

    bool discriminantsEvaluated;

    HIRTraitMarkings markings;

    unsigned forcedAlignment = 0;

    size_t numVariants() const {
        return (data.is_Data() ? data.as_Data().size() : data.as_Value().variants.size());
    }

    size_t findVariant(const RcString&) const;

    bool isValue() const;

    U128 getValue(size_t variant) const;

    U128 getDiscriminant(size_t variant) const;

    static HIRCoreType getReprType(Repr r);

    bool mustUse = false;
};

#include "hir_hir_struct_tu.h"

class HIRStruct {
public:
    enum class Repr {
        Rust,
        C,
        Simd,
        Transparent,
    };
    using Data = HIRStructData;

    struct FieldDefault {
        size_t index;
        HIRExprPtr expr;
        EncodedLiteral valueRes;
        HIRConstant::ValueState state = HIRConstant::ValueState::Unknown;

        FieldDefault(size_t index, HIRExprPtr v);
    };

    HIRStruct(HIRGenericParams params, Repr repr, Data data);

    HIRStruct(HIRGenericParams params, Repr repr, Data data, unsigned align, HIRTraitMarkings tm, HIRStructMarkings sm);

    HIRGenericParams params;
    Repr repr;
    Data data;
    unsigned forcedAlignment = 0;
    unsigned maxFieldAlignment = 0;

    HIRTraitMarkings markings;
    HIRStructMarkings structMarkings;

    HIRConstEvalState constEvalState = HIRConstEvalState::None;

    bool mustUse = false;
};

extern ::std::ostream& operator<<(::std::ostream& os, const HIRStruct::Repr& x);

class HIRUnion {
public:
    enum class Repr {
        Rust,
        C,
        Transparent,
    };

    HIRGenericParams params;
    Repr repr;
    tStructFields variants;

    HIRTraitMarkings markings;

    unsigned forcedAlignment = 0;

    unsigned maxFieldAlignment = 0;

    bool mustUse = false;
};

struct HIRAssociatedType {
    HIRGenericParams generics;
    bool isSized;
    ::std::vector<HIRTraitPath> traitBounds;
    bool hasDefault;
    HIRTypeRef defaultValue;

    HIRAssociatedType(HIRGenericParams generics, bool isSized, ::std::vector<HIRTraitPath> traitBounds, HIRTypeRef defaultType);
};

#include "hir_hir_trait_value_tu.h"

class HIRTrait {
public:
    HIRGenericParams params;

    ::std::vector<HIRTraitPath> parentTraits;

    bool isMarker;
    bool isConst;

    bool isCoinductive;

    bool isFundamental;

    bool skipArrayDuringMethodDispatch;
    bool skipBoxedSliceDuringMethodDispatch;

    ::std::unordered_map<RcString, HIRAssociatedType> types;
    ::std::unordered_map<RcString, HIRTraitValueItem> values;

    // - TODO: Find an easier way of having this be `(GenericPath,RcString) -> unsigned`
    ::std::unordered_multimap<RcString, ::std::pair<unsigned int, HIRGenericPath>> valueIndexes;

    ::std::unordered_map<RcString, unsigned int> typeIndexes;

    unsigned vtableParentTraitsStart;

    ::std::vector<HIRTraitPath> allParentTraits;

    HIRSimplePath vtablePath;

    HIRTrait(HIRGenericParams gps, ::std::vector<HIRTraitPath> parents);

    HIRTypeRef getVtableType(const Span& sp, const HIRCrate& crate, const HIRTypeData::Data_TraitObject& te) const;
    unsigned getVtableValueIndex(const HIRGenericPath& traitPath, const RcString& name) const;
    unsigned getVtableParentIndex(HIRTypeInterner& types, const Span& sp, const HIRPathParams& thisParams, const HIRGenericPath& traitPath) const;
    ::std::pair<const HIRAssociatedType*, const HIRPathParams*> getAtyDef(const RcString& name) const;

    bool mustUse = false;
};

class HIRProcMacro {
public:
    enum class Ty {
        Function,
        Derive,
        Attribute,
    } ty;

    RcString name;

    HIRSimplePath path;

    ::std::vector<::std::string> attributes;
};

#include "hir_hir_asm_tu.h"

class HIRGlobalAssembly {
public:
    Span span;
    ::std::vector<AsmLine> lines;
    ::std::vector<HIRGlobalAsmOperand> operands;
    AsmOptions options;

    HIRGlobalAssembly() = default;
    HIRGlobalAssembly(const HIRGlobalAssembly&) = delete;
    HIRGlobalAssembly(HIRGlobalAssembly&&) noexcept = default;
    HIRGlobalAssembly& operator=(const HIRGlobalAssembly&) = delete;
    HIRGlobalAssembly& operator=(HIRGlobalAssembly&&) noexcept = default;
};

class HIRModule {
public:
    LintLevelOverrides lintLevels;

    ::std::vector<HIRSimplePath> traits;

    ::std::unordered_map<RcString, HIRVisEnt<HIRValueItem>*> valueItems;

    ::std::unordered_map<RcString, HIRVisEnt<HIRTypeItem>*> modItems;

    ::std::unordered_map<RcString, HIRVisEnt<HIRMacroItem>*> macroItems;

    ::std::vector<HIRGlobalAssembly> globalAsm;

    ::std::vector<::std::pair<RcString, std::unique_ptr<HIRStatic>>> inlineStatics;

    HIRModule();

    HIRModule(const HIRModule&) = delete;
    HIRModule(HIRModule&& x) = default;
    HIRModule& operator=(const HIRModule&) = delete;
    HIRModule& operator=(HIRModule&&) = default;
};

#include "hir_hir_items_tu.h"

class HIRTypeImpl {
public:
    template <typename T>
    struct VisImplEnt {
        HIRPublicity publicity;
        bool isSpecialisable;
        T data;
    };

    HIRGenericParams params;
    HIRTypeRef type;

    ::std::map<RcString, VisImplEnt<HIRFunction>> methods;
    ::std::map<RcString, VisImplEnt<HIRConstant>> constants;
    ::std::map<RcString, VisImplEnt<HIRTypeAlias>> types;

    HIRSimplePath srcModule;

    bool matchesType(const HIRTypeData* tr, tCbResolveType tyRes, class HIRImplMatcherScratch& scratch) const;

    bool matchesType(const HIRTypeData* tr) const {
        return matchesType(tr, HIRResolvePlaceholdersNop());
    }

    bool matchesType(const HIRTypeData* tr, tCbResolveType tyRes) const;
};

class HIRTraitImpl {
public:
    template <typename T>
    struct ImplEnt {
        bool isSpecialisable;
        T data;
    };

    HIRGenericParams params;
    HIRPathParams traitArgs;
    HIRTypeRef type;

    ::std::map<RcString, ImplEnt<HIRFunction>> methods;
    ::std::map<RcString, ImplEnt<HIRConstant>> constants;
    ::std::map<RcString, ImplEnt<HIRStatic>> statics;

    ::std::map<RcString, ImplEnt<HIRTypeRef>> types;

    HIRSimplePath srcModule;
    bool isConst = false;

    bool isReservation = false;

    bool matchesType(const HIRTypeData* tr, tCbResolveType tyRes, class HIRImplMatcherScratch& scratch) const;

    bool matchesType(const HIRTypeData* tr) const {
        return matchesType(tr, HIRResolvePlaceholdersNop());
    }

    bool matchesType(const HIRTypeData* tr, tCbResolveType tyRes) const;

    bool moreSpecificThan(HIRTypeInterner& types, const HIRTraitImpl& x) const;

    bool overlapsWith(const HIRCrate& crate, const HIRTraitImpl& other) const;
};

class HIRMarkerImpl {
public:
    HIRGenericParams params;
    HIRPathParams traitArgs;
    bool isPositive;
    HIRTypeRef type;

    HIRSimplePath srcModule;

    bool matchesType(const HIRTypeData* tr, tCbResolveType tyRes, class HIRImplMatcherScratch& scratch) const;

    bool matchesType(const HIRTypeData* tr) const {
        return matchesType(tr, HIRResolvePlaceholdersNop());
    }

    bool matchesType(const HIRTypeData* tr, tCbResolveType tyRes) const;
};

class HIRImplMatcherScratch {
public:
    stl::Vector<HIRTypeRef> buffers[8];
    unsigned depth = 0;
};

class HIRExternCrate {
public:
    HIRCrate* data = nullptr;
    ::std::string basename;
    ::std::string path;
    RcString objectPath;
    bool isProcMacro = false;
};

class HIRExternLibrary {
public:
    ::std::string name;
};

struct HIRTraitImplCallback {
    virtual bool visit(const HIRTraitImpl& impl) = 0;
};

template <typename F>
struct HIRTraitImplCb final: HIRTraitImplCallback {
    F f;

    explicit HIRTraitImplCb(F f)
        : f(f)
    {
    }

    bool visit(const HIRTraitImpl& impl) override {
        return f(impl);
    }
};

struct HIRMarkerImplCallback {
    virtual bool visit(const HIRMarkerImpl& impl) = 0;
};

template <typename F>
struct HIRMarkerImplCb final: HIRMarkerImplCallback {
    F f;

    explicit HIRMarkerImplCb(F f)
        : f(f)
    {
    }

    bool visit(const HIRMarkerImpl& impl) override {
        return f(impl);
    }
};

struct HIRTypeImplCallback {
    virtual bool visit(const HIRTypeImpl& impl) = 0;
};

template <typename F>
struct HIRTypeImplCb final: HIRTypeImplCallback {
    F f;

    explicit HIRTypeImplCb(F f)
        : f(f)
    {
    }

    bool visit(const HIRTypeImpl& impl) override {
        return f(impl);
    }
};

struct HIRLocalItemTypeNamePath {
    HIRSimplePath modulePath;
    const HIRPath* ownerPath;
    const HIRLocalItemTypeNamePath* next;

    HIRLocalItemTypeNamePath(HIRSimplePath modulePath, const HIRPath* ownerPath, const HIRLocalItemTypeNamePath* next)
        : modulePath(modulePath)
        , ownerPath(ownerPath)
        , next(next)
    {
    }
};

class HIRCrate {
public:
    stl::ObjPool* pool;
    HIRTypeInterner& types;

    mutable HIRValueItem intrinsicOffsetof;

    mutable HIRImplMatcherScratch implMatcherScratch;
    mutable HIRFunction::argsT emptyMirArgs;
    HIRSimplePath emptyLangItemPath;
    RcString crateName;

    RcString crateNameDisplay;
    ASTEdition edition;

    bool isNoCore = false;
    bool noMain = false;

    ::std::set<RcString> features;

    HIRModule rootModule;

    mutable std::vector<std::pair<RcString, HIRVisEnt<HIRTypeItem>*>> newTypes;
    mutable std::vector<std::pair<RcString, HIRVisEnt<HIRValueItem>*>> newValues;

    std::map<HIRSimplePath, std::vector<HIRPath>> opaqueTypeDefiners;

    const HIRLocalItemTypeNamePath* localItemTypeNamePaths = nullptr;

    bool isOpaqueAliasNamedBy(const HIRTypeDataErasedTypeAliasInner& alias, const HIRSimplePath* names, size_t nameCount) const;

    template <typename T>
    struct ImplGroup {
        typedef ::std::vector<T> listT;
        ::std::map<HIRSimplePath, listT> named;
        listT nonNamed; // TODO: use a map of HIR::ASTType*::Data::Tag
        listT generic;

        const listT* getListForType(const HIRTypeData* ty) const {
            if (const auto* p = ty->getSortPath()) {
                auto it = named.find(*p);
                if (it != named.end()) {
                    return &it->second;
                } else {
                    return nullptr;
                }
            } else {
                // TODO: Sort these by type tag, use the `Primitive` group if `ty` is Infer
                return &nonNamed;
            }
        }

        listT& getListForTypeMut(const HIRTypeData* ty) {
            if (const auto* p = ty->getSortPath()) {
                return named[*p];
            } else {
                // TODO: Ivars match with core types
                return nonNamed;
            }
        }
    };

    ImplGroup<::std::unique_ptr<HIRTypeImpl>> typeImpls;

    ::std::map<HIRSimplePath, ImplGroup<::std::unique_ptr<HIRTraitImpl>>> traitImpls;
    ::std::map<HIRSimplePath, ImplGroup<::std::unique_ptr<HIRMarkerImpl>>> markerImpls;

    ImplGroup<const HIRTypeImpl*> allTypeImpls;
    ::std::map<HIRSimplePath, ImplGroup<const HIRTraitImpl*>> allTraitImpls;
    ::std::map<HIRSimplePath, ImplGroup<const HIRMarkerImpl*>> allMarkerImpls;

    std::vector<RcString> exportedMacroNames;

    ::std::unordered_map<::std::string, HIRSimplePath> langItems;

    ::std::vector<RcString> extCratesOrdered;

    ::std::unordered_map<RcString, HIRExternCrate> extCrates;

    ::std::vector<HIRExternLibrary> extLibs;

    ::std::vector<::std::string> linkPaths;

    HIRCrate(stl::ObjPool* pool, HIRTypeInterner& types);

    void postLoadUpdate(const RcString& loadedName);

    const HIRSimplePath& getLangItemPath(const Span& sp, const char* name) const;
    const HIRSimplePath& getLangItemPathOpt(const char* name) const;

    bool featureEnabled(const char* name) const {
        return features.count(RcString::newInterned(name)) != 0;
    }

    const HIRMacroItem& getMacroitemByPath(const Span& sp, const HIRSimplePath& path, bool ignoreCrateName = false, bool ignoreLastNode = false) const;

    const HIRTypeItem& getTypeitemByPath(const Span& sp, const HIRSimplePath& path, bool ignoreCrateName = false, bool ignoreLastNode = false) const;

    const HIRTypeItem* getTypeitemByPathOpt(const HIRSimplePath& path) const;
    const HIRTrait& getTraitByPath(const Span& sp, const HIRSimplePath& path) const;
    ::std::optional<size_t> findMostSpecificTrait(const Span& sp, const ::std::vector<HIRSimplePath>& candidates) const;
    const HIRStruct& getStructByPath(const Span& sp, const HIRSimplePath& path) const;
    const HIRUnion& getUnionByPath(const Span& sp, const HIRSimplePath& path) const;
    const HIREnum& getEnumByPath(const Span& sp, const HIRSimplePath& path, bool ignoreCrateName = false, bool ignoreLastNode = false) const;
    const HIRModule& getModByPath(const Span& sp, const HIRSimplePath& path, bool ignoreLastNode = false, bool ignoreCrateName = false) const;

    const HIRValueItem& getValitemByPath(const Span& sp, const HIRSimplePath& path, bool ignoreCrateName = false) const;
    const HIRFunction& getFunctionByPath(const Span& sp, const HIRSimplePath& path) const;
    bool functionTracksCaller(const Span& sp, const HIRPath& path, const HIRFunction& function) const;

    const HIRStatic& getStaticByPath(const Span& sp, const HIRSimplePath& path) const;

    const HIRConstant& getConstantByPath(const Span& sp, const HIRSimplePath& path) const;

    bool findTraitImplsCb(const HIRSimplePath& path, const HIRTypeData* type, tCbResolveType tyRes, HIRTraitImplCallback& callback) const;
    bool findAutoTraitImplsCb(const HIRSimplePath& path, const HIRTypeData* type, tCbResolveType tyRes, HIRMarkerImplCallback& callback) const;
    bool findTypeImplsCb(const HIRTypeData* type, tCbResolveType tyRes, HIRTypeImplCallback& callback) const;

    template <typename F>
    bool findTraitImpls(const HIRSimplePath& path, const HIRTypeData* type, tCbResolveType tyRes, F f) const {
        HIRTraitImplCb<F> cb(f);
        return findTraitImplsCb(path, type, tyRes, cb);
    }

    template <typename F>
    bool findAutoTraitImpls(const HIRSimplePath& path, const HIRTypeData* type, tCbResolveType tyRes, F f) const {
        HIRMarkerImplCb<F> cb(f);
        return findAutoTraitImplsCb(path, type, tyRes, cb);
    }

    template <typename F>
    bool findTypeImpls(const HIRTypeData* type, tCbResolveType tyRes, F f) const {
        HIRTypeImplCb<F> cb(f);
        return findTypeImplsCb(type, tyRes, cb);
    }

    const MIRFunction* getOrGenMir(const WireBoard& wb, const HIRItemPath& ip, const HIRExprPtr& ep, const HIRFunction::argsT& args, HIRTypeRef& retTy) const;

    const MIRFunction* getOrGenMir(const WireBoard& wb, const HIRItemPath& ip, const HIRFunction& fcn) const;

    const MIRFunction* getOrGenMir(const WireBoard& wb, const HIRItemPath& ip, const HIRExprPtr& ep, HIRTypeRef& expTy) const;
};

const HIRStruct& patternGetStruct(const Span& sp, const HIRPath& path, const HIRPattern::PathBinding& binding, bool isTuple);
const tTupleFields& patternGetTuple(const Span& sp, const HIRPath& path, const HIRPattern::PathBinding& binding);
const tStructFields& patternGetNamed(const Span& sp, const HIRPath& path, const HIRPattern::PathBinding& binding);
