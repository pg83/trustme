#pragma once

#include "int128.h"
#include "hir_asm.h"
#include "hir_path.h"
#include "hir_type.h"
#include "ast_edition.h"
#include "hir_pattern.h"
#include "hir_expr_ptr.h"
#include "settings.h"
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
    static ::std::shared_ptr<HIRSimplePath> nonePath;
    ::std::shared_ptr<HIRSimplePath> visPath;

    HIRPublicity(::std::shared_ptr<HIRSimplePath> p);

public:
    static HIRPublicity newGlobal() {
        return HIRPublicity({});
    }

    static HIRPublicity newNone() {
        return HIRPublicity(nonePath);
    }

    static HIRPublicity newPriv(HIRSimplePath p);

    bool isGlobal() const {
        return !visPath;
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

// --------------------------------------------------------------------
// Value structures
// --------------------------------------------------------------------
struct HIRLinkage {
    enum class Type {
        Auto,       // Default
        Weak,       // Weak linkage (multiple definitions are allowed
        External,   // Force the symbol to be externally visible
        ExternWeak, // A reference to a weak symbol
    };

    // Linkage type
    Type type = Type::Auto;

    // External symbol name
    ::std::string name;
    // Target section
    ::std::string section;
};

class HIRStatic {
public:
    // NOTE: The generics can't influence the value of this `const`
    HIRGenericParams params;

    HIRLinkage linkage;
    bool isMut;
    HIRTypeRef type;
    // Stronger alignment requested for compiler-generated storage.  This is
    // zero for Rust statics, whose alignment is determined by type.
    size_t explicitAlignment = 0;

    HIRExprPtr value;

    EncodedLiteral valueRes;
    bool valueGenerated = false;
    bool saveLiteral = false;
    bool noEmitValue = false;
    /// Storage the compiler made for a promoted borrow, not a `static` the
    /// program wrote. A zero-sized one holds nothing, so its address is the
    /// alignment rather than a place in the data section.
    bool isPromoted = false;

    mutable ::std::map<HIRPath, EncodedLiteral> monomorphCache;

    HIRStatic(HIRLinkage linkage, bool isMut, HIRTypeRef type, HIRExprPtr value);
};

class HIRConstant {
public:
    // NOTE: The generics can't influence the value of this `const`
    HIRGenericParams params;

    HIRTypeRef type;
    HIRExprPtr value;

    EncodedLiteral valueRes;
    enum class ValueState {
        Unknown,
        Generic,
        Known
    } valueState = ValueState::Unknown;

    // A cache of monomorphised versions when the `const` depends on generics for its value
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
        //PointerMut,
        //PointerConst,
        Box,
        Custom,
    };

    typedef ::std::vector<::std::pair<HIRPattern, HIRTypeRef>> argsT;

    bool saveCode = false; // Filled by enumerate, defaults to false
    HIRLinkage linkage;

    Receiver receiver = Receiver::Free;
    ::std::optional<HIRTypeRef> receiverType; // Present only for a custom receiver
    RcString abi = RcString::newInterned(ABI_RUST);
    bool unsafe = false;
    bool isConst = false;

    HIRGenericParams params;

    argsT args;
    bool variadic = false;
    bool hasNamedVariadic = false;
    HIRTypeRef returnType;
    // The trait-declared return used to typecheck a refining RPITIT impl.
    // `returnType` remains the impl's public, possibly concrete signature.
    ::std::optional<HIRTypeRef> traitReturnType;

    SourceLocation source;
    HIRExprPtr code;
    // Exact aliases named by this current-crate function's
    // #[define_opaque(...)] attribute.
    std::vector<HIRSimplePath> defineOpaque;

    struct Markings {
        std::vector<unsigned> rustcLegacyConstGenerics;
        bool trackCaller = false;
        /// `#[must_use]`: reported at the call site, which may be in another
        /// crate, so it travels with the item.
        bool mustUse = false;
        bool isNaked = false;
        /// Requested function entry alignment from `#[rustc_align(N)]`.
        u64 alignment = 0;
        // Calls to functions with #[rustc_intrinsic] must remain visible to
        // CTFE even when the function also provides a runtime fallback body.
        bool isRustcIntrinsic = false;
        bool isRustcPromotable = false;

        enum Inline {
            Auto,   // no annotation
            Never,  // #[inline(never)]
            Normal, // #[inline]
            Always  // #[inline(always)]
        } inlineType = Inline::Auto;

        /// Lint levels set on this function by `#[allow]` and friends, by exact
        /// name and by group. NOTE: not serialised, so only meaningful for a
        /// function defined in the crate being compiled.
        ::std::map<RcString, CfgLintLevel> lintLevels;
        ::std::map<RcString, CfgLintLevel> lintGroupLevels;
    } markings;

    HIRFunction();

    HIRFunction(Receiver receiver, HIRGenericParams params, argsT args, HIRTypeRef retTy, HIRExprPtr code);

    size_t fixedArgCount() const {
        assert(!hasNamedVariadic || (variadic && !args.empty()));
        return args.size() - hasNamedVariadic;
    }

    HIRTypeRef makePtrTy(const Span& sp, const Monomorphiser& ms) const;
};

// --------------------------------------------------------------------
// Type structures
// --------------------------------------------------------------------
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
    /// @brief Default value for this field
    ::std::unique_ptr<HIRGenericPath> defaultValue;
};

typedef ::std::vector<HIRStructField> tStructFields;

extern HIRTypeRef fnPtrTupleConstructor(const Span& sp, const Monomorphiser& ms, HIRTypeRef retTy, const tTupleFields& types);

/// Cache of the state of various language traits on an enum/struct
struct HIRTraitMarkings {
    /// Indicates that there is at least one Deref impl
    bool hasADeref = false;

    /// Indicates that there is a Drop impl
    /// - If there is an impl, there must be an applicable impl to every instance.
    bool hasDropImpl = false;

    /// `true` if the Drop impl is `impl const Drop`, so the destructor may run
    /// during constant evaluation.
    bool hasConstDropImpl = false;

    /// `true` if there is a Copy impl
    bool isCopy = false;

    struct AutoMarking {
        // If present, this impl is conditionally true based on the listed type parameters
        ::std::vector<HIRTypeRef> conditions;
        // Implementation state
        bool isImpled;
    };

    // General auto trait impls
    mutable ::std::map<HIRSimplePath, AutoMarking> autoImpls;
};

// Trait implementations relevant only to structs
struct HIRStructMarkings {
    /// Compiler-owned future returned by the `async_drop_in_place` lang item.
    bool isAsyncDropGlue = false;

    /// `#[fundamental]`: orphan checking may look through this type's
    /// generic arguments when searching for a local key parameter.
    bool isFundamental = false;

    /// This type has a tail field whose type can be unsized.
    bool canUnsize = false;
    /// Index of the single ?Sized parameter controlling the tail.
    /// Associated-type tails can depend on multiple parameters and leave this unset.
    unsigned int unsizedParam = ~0u;

    // TODO: This would have to be changed for custom DSTs
    enum class DstType {
        None,        // Sized
        Possible,    // A ?Sized parameter
        Slice,       // [T]
        TraitObject, // (Trait)
        Projection,  // A potentially-unsized associated type
    } dstType = DstType::None;
    unsigned int unsizedField = ~0u;

    enum class Coerce {
        None,        // No CoerceUnsized impl
        Passthrough, // Is generic over T: CoerceUnsized
        Pointer,     // Contains a pointer to a ?Sized type
    } coerceUnsized = Coerce::None;

    // If populated, indicates the field that is the coercable pointer.
    unsigned int coerceUnsizedIndex = ~0u;
    // Index of the parameter that controls the CoerceUnsized (either a T: ?Sized, or a T: CoerceUnsized)
    unsigned int coerceParam = ~0u;

    // #[rustc_nonnull_optimization_guaranteed]
    bool isNonzero = false;

    // UnsafeCell and UnsafePinned suppress all niches inherited from their fields.
    bool isNoNiche = false;

    // #[rustc_layout_scalar_valid_range_end]
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
    bool isStruct; // Indicates that the variant does not show up in the value namespace
    HIRTypeRef type;

    /// Optional explicit descriminant value, only valid when repr isn't Repr::Auto
    HIRExprPtr discriminantExpr;
    // Constant-evaluated descriminant value
    U128 discriminantValue = U128(0);
};

struct HIREnumValueVariant {
    RcString name;
    HIRExprPtr expr;
    // TODO: Signed.
    U128 val = U128(0);
};

// Definitions generated from hir_hir_enum.tu.
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

    // Flag indicating that constant evaluation has completed
    bool discriminantsEvaluated;
    /// Set while the discriminants are being evaluated. A variant's expression
    /// may name another variant of the same enum, and asking for the values then
    /// must read what is known rather than start again.
    mutable bool discriminantsEvaluating = false;

    HIRTraitMarkings markings;

    /// `#[repr(align(N))]`, which raises the alignment without changing the tag.
    unsigned forcedAlignment = 0;

    size_t numVariants() const {
        return (data.is_Data() ? data.as_Data().size() : data.as_Value().variants.size());
    }

    size_t findVariant(const RcString&) const;

    /// Returns true if this enum is a C-like enum (has values only)
    bool isValue() const;
    /// Returns the value for the given variant (onlu for value enums)
    U128 getValue(size_t variant) const;
    /// The discriminant of a variant, whether or not the enum is a value enum:
    /// a variant written `Tuple()` or `Struct{}` holds nothing, so the enum can
    /// still be cast to an integer.
    U128 getDiscriminant(size_t variant) const;

    /// Get a type for the given repr value
    static HIRCoreType getReprType(Repr r);

    /// `#[must_use]`: reported at the use site, which may be in another crate,
    /// so it travels with the item.
    bool mustUse = false;
};

// Definitions generated from hir_hir_struct.tu.
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
    unsigned maxFieldAlignment = 0; // for packed

    HIRTraitMarkings markings;
    HIRStructMarkings structMarkings;

    HIRConstEvalState constEvalState = HIRConstEvalState::None;

    /// `#[must_use]`: reported at the use site, which may be in another crate,
    /// so it travels with the item.
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

    /// `#[repr(align(N))]`, which raises the alignment past any member's.
    unsigned forcedAlignment = 0;

    /// `#[repr(packed(N))]`, which caps it below any member's.
    unsigned maxFieldAlignment = 0;

    /// `#[must_use]`: reported at the use site, which may be in another crate,
    /// so it travels with the item.
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

// Definitions generated from hir_hir_trait_value.tu.
#include "hir_hir_trait_value_tu.h"

class HIRTrait {
public:
    HIRGenericParams params;
    // NOTE: Not serialised!
    ::std::vector<HIRTraitPath> parentTraits;

    bool isMarker; // aka auto trait/OIBIT
    bool isConst;
    /// Auto traits and traits carrying `#[rustc_coinductive]` admit
    /// productive recursive goals in the trait solver.
    bool isCoinductive;
    /// `#[fundamental]`: absence of an impl for this trait can be used by
    /// coherence once downstream impls have been excluded.
    bool isFundamental;
    /// Compatibility exclusions requested by
    /// `#[rustc_skip_during_method_dispatch(array, boxed_slice)]`.
    bool skipArrayDuringMethodDispatch;
    bool skipBoxedSliceDuringMethodDispatch;

    ::std::unordered_map<RcString, HIRAssociatedType> types;
    ::std::unordered_map<RcString, HIRTraitValueItem> values;

    // Indexes into the vtable for each present method and value
    // - TODO: Find an easier way of having this be `(GenericPath,RcString) -> unsigned`
    ::std::unordered_multimap<RcString, ::std::pair<unsigned int, HIRGenericPath>> valueIndexes;
    // Indexes in the vtable parameter list for each associated type
    ::std::unordered_map<RcString, unsigned int> typeIndexes;
    /// Index of the first vtable entry for parent traits
    unsigned vtableParentTraitsStart;

    // Flattend set of parent traits (monomorphised and associated types fixed)
    ::std::vector<HIRTraitPath> allParentTraits;
    // VTable path
    HIRSimplePath vtablePath;

    HIRTrait(HIRGenericParams gps, ::std::vector<HIRTraitPath> parents);

    HIRTypeRef getVtableType(const Span& sp, const HIRCrate& crate, const HIRTypeData::Data_TraitObject& te) const;
    unsigned getVtableValueIndex(const HIRGenericPath& traitPath, const RcString& name) const;
    unsigned getVtableParentIndex(HIRTypeInterner& types, const Span& sp, const HIRPathParams& thisParams, const HIRGenericPath& traitPath) const;
    ::std::pair<const HIRAssociatedType*, const HIRPathParams*> getAtyDef(const RcString& name) const;

    /// `#[must_use]`: reported at the use site, which may be in another crate,
    /// so it travels with the item.
    bool mustUse = false;
};

class HIRProcMacro {
public:
    enum class Ty {
        Function,
        Derive,
        Attribute,
    } ty;
    // Name of the macro
    RcString name;
    // Path to the handler
    HIRSimplePath path;
    // A list of attributes to hand to the handler
    ::std::vector<::std::string> attributes;
};

// Definitions generated from hir_hir_asm.tu.
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
    // List of in-scope traits in this module
    ::std::vector<HIRSimplePath> traits;

    // Contains all values and functions (including type constructors)
    ::std::unordered_map<RcString, HIRVisEnt<HIRValueItem>*> valueItems;
    // Contains types, traits, and modules
    ::std::unordered_map<RcString, HIRVisEnt<HIRTypeItem>*> modItems;
    // Macros!
    ::std::unordered_map<RcString, HIRVisEnt<HIRMacroItem>*> macroItems;

    // Global assembly is module-scoped: its operands resolve in this module.
    ::std::vector<HIRGlobalAssembly> globalAsm;

    ::std::vector<::std::pair<RcString, std::unique_ptr<HIRStatic>>> inlineStatics;

    HIRModule();

    HIRModule(const HIRModule&) = delete;
    HIRModule(HIRModule&& x) = default;
    HIRModule& operator=(const HIRModule&) = delete;
    HIRModule& operator=(HIRModule&&) = default;
};

// --------------------------------------------------------------------

// Definitions generated from hir_hir_items.tu.
#include "hir_hir_items_tu.h"

// --------------------------------------------------------------------

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

    bool matchesType(const HIRTypeData* tr, tCbResolveType tyRes) const;

    bool matchesType(const HIRTypeData* tr) const {
        return matchesType(tr, HIRResolvePlaceholdersNop());
    }
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

    //const TraitImpl*    m_parent_spec_impl;

    bool matchesType(const HIRTypeData* tr, tCbResolveType tyRes) const;

    bool matchesType(const HIRTypeData* tr) const {
        return matchesType(tr, HIRResolvePlaceholdersNop());
    }

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

    bool matchesType(const HIRTypeData* tr, tCbResolveType tyRes) const;

    bool matchesType(const HIRTypeData* tr) const {
        return matchesType(tr, HIRResolvePlaceholdersNop());
    }
};

class HIRExternCrate {
public:
    HIRCrate* data = nullptr;
    ::std::string basename; // Just the filename (serialised)
    ::std::string path;    // The path used to load this crate
};

class HIRExternLibrary {
public:
    ::std::string name;
};

class HIRCrate {
public:
    stl::ObjPool* pool;
    HIRTypeInterner& types;
    // Synthetic compiler item. Its signature uses this crate's interned
    // types, so it must not live in process-global storage.
    mutable HIRValueItem intrinsicOffsetof;
    RcString crateName;
    /// The crate's name as the user wrote it. An executable's `crateName` is the
    /// placeholder `bin#`, which `type_name` must not show.
    RcString crateNameDisplay;
    ASTEdition edition;
    // Compile-local crate configuration. This is not serialised because an
    // external crate can never provide this crate's executable entrypoint.
    bool isNoCore = false;
    bool noMain = false;
    // Enabled language features affect type checking of this crate only;
    // consumers of its metadata use their own feature set.
    ::std::set<RcString> features;

    HIRModule rootModule;

    // Placeholder for types created during constant evaluation
    mutable std::vector<std::pair<RcString, HIRVisEnt<HIRTypeItem>*>> newTypes;
    mutable std::vector<std::pair<RcString, HIRVisEnt<HIRValueItem>*>> newValues;

    // Current-crate functions carrying #[define_opaque(...)].  The map is a
    // query index for lazy type checking and is intentionally not serialised.
    std::map<HIRSimplePath, std::vector<HIRPath>> opaqueTypeDefiners;

    bool isOpaqueAliasNamedBy(const HIRTypeDataErasedTypeAliasInner& alias, const HIRSimplePath* names, size_t nameCount) const;

    template <typename T>
    struct ImplGroup {
        typedef ::std::vector<T> listT;
        ::std::map<HIRSimplePath, listT> named;
        listT nonNamed; // TODO: use a map of HIR::ASTType*::Data::Tag
        listT generic;

        const listT* getListForType(const HIRTypeData* ty) const {
            static listT empty;
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

    /// Impl blocks on just a type, split into three groups
    // - Named type (sorted on the path)
    // - Primitive types
    // - Unsorted (generics, and everything before outer type resolution)
    ImplGroup<::std::unique_ptr<HIRTypeImpl>> typeImpls;

    /// CACHE: Cache of all inherent (non-trait) methods (for faster lookup)

    /// Impl blocks
    ::std::map<HIRSimplePath, ImplGroup<::std::unique_ptr<HIRTraitImpl>>> traitImpls;
    ::std::map<HIRSimplePath, ImplGroup<::std::unique_ptr<HIRMarkerImpl>>> markerImpls;

    /// Merged index versions of the above
    ImplGroup<const HIRTypeImpl*> allTypeImpls;
    ::std::map<HIRSimplePath, ImplGroup<const HIRTraitImpl*>> allTraitImpls;
    ::std::map<HIRSimplePath, ImplGroup<const HIRMarkerImpl*>> allMarkerImpls;

    /// List of legacy-exported macros
    std::vector<RcString> exportedMacroNames;

    /// Language items avaliable through this crate (includes ones from loaded externs)
    ::std::unordered_map<::std::string, HIRSimplePath> langItems;

    /// Referenced crates (in load order) - Used to ensure final linking order is sane
    // NOT SERIALISED
    ::std::vector<RcString> extCratesOrdered;
    /// Referenced crates
    ::std::unordered_map<RcString, HIRExternCrate> extCrates;
    /// Referenced system libraries
    ::std::vector<HIRExternLibrary> extLibs;
    /// Extra paths for the linker
    ::std::vector<::std::string> linkPaths;

    HIRCrate(stl::ObjPool* pool, HIRTypeInterner& types);

    /// Method called to populate runtime state after deserialisation
    /// See hir/crate_post_load.cpp
    void postLoadUpdate(const RcString& loadedName);

    const HIRSimplePath& getLangItemPath(const Span& sp, const char* name) const;
    const HIRSimplePath& getLangItemPathOpt(const char* name) const;

    bool featureEnabled(const char* name) const {
        return features.count(RcString::newInterned(name)) != 0;
    }

    const HIRMacroItem& getMacroitemByPath(const Span& sp, const HIRSimplePath& path, bool ignoreCrateName = false, bool ignoreLastNode = false) const;

    const HIRTypeItem& getTypeitemByPath(const Span& sp, const HIRSimplePath& path, bool ignoreCrateName = false, bool ignoreLastNode = false) const;
    /// The type item a path names, or `nullptr` if the path names nothing.
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

    // NOTE: Special implementation to handle `m_inline_statics`
    const HIRStatic& getStaticByPath(const Span& sp, const HIRSimplePath& path) const;

    const HIRConstant& getConstantByPath(const Span& sp, const HIRSimplePath& path) const;

    bool findTraitImpls(const HIRSimplePath& path, const HIRTypeData* type, tCbResolveType tyRes, ::std::function<bool(const HIRTraitImpl&)> callback) const;
    bool findAutoTraitImpls(const HIRSimplePath& path, const HIRTypeData* type, tCbResolveType tyRes, ::std::function<bool(const HIRMarkerImpl&)> callback) const;
    bool findTypeImpls(const HIRTypeData* type, tCbResolveType tyRes, ::std::function<bool(const HIRTypeImpl&)> callback) const;

    const MIRFunction* getOrGenMir(const WireBoard& wb, const HIRItemPath& ip, const HIRExprPtr& ep, const HIRFunction::argsT& args, HIRTypeRef& retTy) const;

    const MIRFunction* getOrGenMir(const WireBoard& wb, const HIRItemPath& ip, const HIRFunction& fcn) const;

    const MIRFunction* getOrGenMir(const WireBoard& wb, const HIRItemPath& ip, const HIRExprPtr& ep, HIRTypeRef& expTy) const;
};

/// Helper for obtaining the matching target for PathTuple/PathNamed
const HIRStruct& patternGetStruct(const Span& sp, const HIRPath& path, const HIRPattern::PathBinding& binding, bool isTuple);
const tTupleFields& patternGetTuple(const Span& sp, const HIRPath& path, const HIRPattern::PathBinding& binding);
const tStructFields& patternGetNamed(const Span& sp, const HIRPath& path, const HIRPattern::PathBinding& binding);
