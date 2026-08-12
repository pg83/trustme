#pragma once

#include "target_version.h"
#include "int128.h"

#include <cassert>
#include <unordered_map>
#include <vector>
#include <memory>
#include <optional>
#include <set>

#include "tagged_union.h"

#include "ast_edition.h"
#include "macro_rules_macro_rules_ptr.h"

#include "hir_type.h"
#include "hir_path.h"
#include "hir_pattern.h"
#include "hir_expr_ptr.h"
#include "hir_generic_params.h"
#include "hir_encoded_literal.h"
#include "hir_inherent_cache.h"
#include "hir_asm.h"

class Monomorphiser;

namespace stl {
    class ObjPool;
}

namespace HIR {

    class Crate;
    class Module;

    class Function;
    class Static;

    class ValueItem;
    class TypeItem;
    class MacroItem;

    class ItemPath;

    class Publicity {
        static ::std::shared_ptr<::HIR::SimplePath> nonePath;
        ::std::shared_ptr<::HIR::SimplePath> vis_path;

        Publicity(::std::shared_ptr<::HIR::SimplePath> p);

    public:
        static Publicity newGlobal() {
            return Publicity({});
        }

        static Publicity newNone() {
            return Publicity(nonePath);
        }

        static Publicity newPriv(::HIR::SimplePath p);

        bool isGlobal() const {
            return !vis_path;
        }

        bool isVisible(const ::HIR::SimplePath& p) const;

        friend ::std::ostream& operator<<(::std::ostream& os, const Publicity& x);
    };

    enum class ConstEvalState {
        None,
        Active,
        Complete,
    };

    template <typename Ent>
    struct VisEnt {
        Publicity publicity;
        Ent ent;
    };

    // --------------------------------------------------------------------
    // Value structures
    // --------------------------------------------------------------------
    struct Linkage {
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

    class Static {
    public:
        // NOTE: The generics can't influence the value of this `const`
        GenericParams mParams;

        Linkage linkage;
        bool isMut;
        TypeRef mType;

        ExprPtr mValue;

        EncodedLiteral valueRes;
        bool valueGenerated = false;
        bool saveLiteral = false;
        bool noEmitValue = false;

        mutable ::std::map<::HIR::Path, EncodedLiteral> monomorphCache;

        Static(Linkage linkage, bool is_mut, TypeRef type, ExprPtr value);
    };

    class Constant {
    public:
        // NOTE: The generics can't influence the value of this `const`
        GenericParams mParams;

        TypeRef mType;
        ExprPtr mValue;

        EncodedLiteral valueRes;
        enum class ValueState {
            Unknown,
            Generic,
            Known
        } valueState = ValueState::Unknown;

        // A cache of monomorphised versions when the `const` depends on generics for its value
        // TODO: Wait, how?
        mutable ::std::map<::HIR::Path, EncodedLiteral> monomorphCache;

        Constant();

        Constant(GenericParams params, TypeRef type, ExprPtr value);
    };

    class Function {
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

        typedef ::std::vector<::std::pair<::HIR::Pattern, ::HIR::TypeRef>> argsT;

        bool saveCode = false; // Filled by enumerate, defaults to false
        Linkage linkage;

        Receiver receiver = Receiver::Free;
        ::std::optional<HIR::TypeRef> receiverType; // Present only for a custom receiver
        RcString mAbi = RcString::newInterned(ABI_RUST);
        bool unsafe = false;
        bool isConst = false;

        GenericParams mParams;

        argsT mArgs;
        bool variadic = false;
        TypeRef returnType;

        ExprPtr mCode;

        struct Markings {
            std::vector<unsigned> rustcLegacyConstGenerics;
            bool trackCaller = false;
            bool isNaked = false;

            enum Inline {
                Auto,   // no annotation
                Never,  // #[inline(never)]
                Normal, // #[inline]
                Always  // #[inline(always)]
            } inlineType = Inline::Auto;
        } markings;

        Function();

        Function(Receiver receiver, GenericParams params, argsT args, TypeRef ret_ty, ExprPtr code);

        ::HIR::TypeRef makePtrTy(const Span& sp, const Monomorphiser& ms) const;
    };

    // --------------------------------------------------------------------
    // Type structures
    // --------------------------------------------------------------------
    struct TypeAlias {
        GenericParams mParams;
        ::HIR::TypeRef mType;
    };

    struct TraitAlias {
        GenericParams mParams;
        ::std::vector<::HIR::TraitPath> traits;
    };

    typedef ::std::vector<VisEnt<::HIR::TypeRef>> tTupleFields;

    struct StructField {
        RcString name;
        Publicity vis;
        HIR::TypeRef ty;
        /// @brief Default value for this field
        ::std::unique_ptr<HIR::GenericPath> default_value;
    };

    typedef ::std::vector<StructField> tStructFields;

    extern HIR::TypeRef fnPtrTupleConstructor(const Span& sp, const Monomorphiser& ms, HIR::TypeRef ret_ty, const tTupleFields& types);

    /// Cache of the state of various language traits on an enum/struct
    struct TraitMarkings {
        /// Indicates that there is at least one Deref impl
        bool hasADeref = false;

        /// Indicates that there is a Drop impl
        /// - If there is an impl, there must be an applicable impl to every instance.
        bool hasDropImpl = false;

        /// `true` if there is a Copy impl
        bool is_copy = false;

        struct AutoMarking {
            // If present, this impl is conditionally true based on the listed type parameters
            ::std::vector<::HIR::TypeRef> conditions;
            // Implementation state
            bool isImpled;
        };

        // General auto trait impls
        mutable ::std::map<::HIR::SimplePath, AutoMarking> autoImpls;
    };

    // Trait implementations relevant only to structs
    struct StructMarkings {
        /// `#[fundamental]`: orphan checking may look through this type's
        /// generic arguments when searching for a local key parameter.
        bool is_fundamental = false;

        /// This type has a <T: ?Sized> parameter that is used directly
        bool canUnsize = false;
        /// Index of the parameter that is ?Sized
        unsigned int unsized_param = ~0u;

        // TODO: This would have to be changed for custom DSTs
        enum class DstType {
            None,        // Sized
            Possible,    // A ?Sized parameter
            Slice,       // [T]
            TraitObject, // (Trait)
        } dst_type = DstType::None;
        unsigned int unsized_field = ~0u;

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

        // #[rustc_layout_scalar_valid_range_end]
        bool boundedMax = false;
        U128 boundedMaxValue;
    };

    class ExternType {
    public:
        // TODO: do extern types need any associated data?
        TraitMarkings markings;
    };

    class Enum {
    public:
        struct DataVariant {
            RcString name;
            bool is_struct; // Indicates that the variant does not show up in the value namespace
            ::HIR::TypeRef type;

            /// Optional explicit descriminant value, only valid when repr isn't Repr::Auto
            ::HIR::ExprPtr discriminantExpr;
            // Constant-evaluated descriminant value
            U128 discriminant_value = U128(0);
        };
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

        struct ValueVariant {
            RcString name;
            ::HIR::ExprPtr expr;
            // TODO: Signed.
            U128 val = U128(0);
        };

        TAGGED_UNION(Class, Data, (Data, ::std::vector<DataVariant>), (Value, struct { ::std::vector<ValueVariant> variants; }));

        GenericParams mParams;
        bool isCRepr;
        Repr tagRepr;
        Class mData;

        // Flag indicating that constant evaluation has completed
        bool discriminantsEvaluated;

        TraitMarkings markings;

        size_t numVariants() const {
            return (mData.is_Data() ? mData.as_Data().size() : mData.as_Value().variants.size());
        }

        size_t findVariant(const RcString&) const;

        /// Returns true if this enum is a C-like enum (has values only)
        bool isValue() const;
        /// Returns the value for the given variant (onlu for value enums)
        U128 getValue(size_t variant) const;

        /// Get a type for the given repr value
        static ::HIR::CoreType getReprType(Repr r);
    };

    class Struct {
    public:
        enum class Repr {
            Rust,
            C,
            Simd,
            Transparent,
        };
        TAGGED_UNION(Data, Unit, (Unit, struct {}), (Tuple, tTupleFields), (Named, tStructFields));

        struct FieldDefault {
            size_t index;
            HIR::ExprPtr expr;
            EncodedLiteral value_res;
            Constant::ValueState state = Constant::ValueState::Unknown;

            FieldDefault(size_t index, HIR::ExprPtr v);
        };

        Struct(GenericParams params, Repr repr, Data data);

        Struct(GenericParams params, Repr repr, Data data, unsigned align, TraitMarkings tm, StructMarkings sm);

        GenericParams mParams;
        Repr repr;
        Data mData;
        unsigned forcedAlignment = 0;
        unsigned maxFieldAlignment = 0; // for packed

        TraitMarkings markings;
        StructMarkings structMarkings;

        ConstEvalState constEvalState = ConstEvalState::None;
    };

    extern ::std::ostream& operator<<(::std::ostream& os, const Struct::Repr& x);

    class Union {
    public:
        enum class Repr {
            Rust,
            C,
            Transparent,
        };

        GenericParams mParams;
        Repr repr;
        tStructFields mVariants;

        TraitMarkings markings;
    };

    struct AssociatedType {
        ::HIR::GenericParams generics;
        bool is_sized;
        LifetimeRef lifetimeBound;
        ::std::vector<::HIR::TraitPath> traitBounds;
        bool hasDefault;
        ::HIR::TypeRef defaultValue;

        AssociatedType(
            ::HIR::GenericParams generics,
            bool is_sized,
            LifetimeRef lifetime_bound,
            ::std::vector<::HIR::TraitPath> trait_bounds,
            ::HIR::TypeRef defaultType
        );
    };

    TAGGED_UNION(TraitValueItem, Constant, (Constant, Constant), (Static, Static), (Function, Function));

    class Trait {
    public:
        GenericParams mParams;
        LifetimeRef lifetime;
        // NOTE: Not serialised!
        ::std::vector<::HIR::TraitPath> parentTraits;

        bool isMarker; // aka auto trait/OIBIT
        bool isConst;
        /// Auto traits and traits carrying `#[rustc_coinductive]` admit
        /// productive recursive goals in the trait solver.
        bool isCoinductive;
        /// `#[fundamental]`: absence of an impl for this trait can be used by
        /// coherence once downstream impls have been excluded.
        bool isFundamental;

        ::std::unordered_map<RcString, AssociatedType> types;
        ::std::unordered_map<RcString, TraitValueItem> values;

        // Indexes into the vtable for each present method and value
        // - TODO: Find an easier way of having this be `(GenericPath,RcString) -> unsigned`
        ::std::unordered_multimap<RcString, ::std::pair<unsigned int, ::HIR::GenericPath>> valueIndexes;
        // Indexes in the vtable parameter list for each associated type
        ::std::unordered_map<RcString, unsigned int> typeIndexes;
        /// Index of the first vtable entry for parent traits
        unsigned vtableParentTraitsStart;

        // Flattend set of parent traits (monomorphised and associated types fixed)
        ::std::vector<::HIR::TraitPath> allParentTraits;
        // VTable path
        ::HIR::SimplePath vtablePath;

        Trait(GenericParams gps, LifetimeRef lifetime, ::std::vector<::HIR::TraitPath> parents);

        ::HIR::TypeRef getVtableType(const Span& sp, const ::HIR::Crate& crate, const ::HIR::TypeData::Data_TraitObject& te) const;
        unsigned getVtableValueIndex(const HIR::GenericPath& trait_path, const RcString& name) const;
        unsigned getVtableParentIndex(TypeInterner& types, const Span& sp, const HIR::PathParams& thisParams, const HIR::GenericPath& trait_path) const;
        ::std::pair<const ::HIR::AssociatedType*, const ::HIR::PathParams*> getAtyDef(const RcString& name) const;
    };

    class ProcMacro {
    public:
        enum class Ty {
            Function,
            Derive,
            Attribute,
        } ty;
        // Name of the macro
        RcString name;
        // Path to the handler
        ::HIR::SimplePath path;
        // A list of attributes to hand to the handler
        ::std::vector<::std::string> attributes;
    };

    class Module {
    public:
        // List of in-scope traits in this module
        ::std::vector<::HIR::SimplePath> traits;

        // Contains all values and functions (including type constructors)
        ::std::unordered_map<RcString, ::std::unique_ptr<VisEnt<ValueItem>>> valueItems;
        // Contains types, traits, and modules
        ::std::unordered_map<RcString, ::std::unique_ptr<VisEnt<TypeItem>>> modItems;
        // Macros!
        ::std::unordered_map<RcString, ::std::unique_ptr<VisEnt<MacroItem>>> macroItems;

        ::std::vector<::std::pair<RcString, std::unique_ptr<Static>>> inlineStatics;

        Module();

        Module(const Module&) = delete;
        Module(Module&& x) = default;
        Module& operator=(const Module&) = delete;
        Module& operator=(Module&&) = default;
    };

    // --------------------------------------------------------------------

    TAGGED_UNION(
        TypeItem,
        Import,
        (Import,
         struct {
             ::HIR::SimplePath path;
             bool isVariant;
             unsigned int idx;
         }),
        (Module, Module),
        (TypeAlias, TypeAlias), // NOTE: These don't introduce new values
        (TraitAlias, TraitAlias),
        (ExternType, ExternType),
        (Enum, Enum),
        (Struct, Struct),
        (Union, Union),
        (Trait, Trait)
    );
    TAGGED_UNION(
        ValueItem,
        Import,
        (Import,
         struct {
             ::HIR::SimplePath path;
             bool isVariant;
             unsigned int idx;
         }),
        (Constant, Constant),
        (Static, Static),
        (StructConstant, struct { ::HIR::SimplePath ty; }),
        (Function, Function),
        (StructConstructor, struct { ::HIR::SimplePath ty; })
    );
    TAGGED_UNION(MacroItem, Import, (Import, struct { ::HIR::SimplePath path; }), (MacroRules, MacroRulesPtr), (ProcMacro, ProcMacro));

    // --------------------------------------------------------------------

    class TypeImpl {
    public:
        template <typename T>
        struct VisImplEnt {
            Publicity publicity;
            bool isSpecialisable;
            T data;
        };

        ::HIR::GenericParams mParams;
        ::HIR::TypeRef mType;

        ::std::map<RcString, VisImplEnt<::HIR::Function>> methods;
        ::std::map<RcString, VisImplEnt<::HIR::Constant>> constants;
        ::std::map<RcString, VisImplEnt<::HIR::TypeAlias>> types;

        ::HIR::SimplePath srcModule;

        bool matchesType(const ::HIR::TypeData* tr, tCbResolveType ty_res) const;

        bool matchesType(const ::HIR::TypeData* tr) const {
            return matchesType(tr, ResolvePlaceholdersNop());
        }
    };

    class TraitImpl {
    public:
        template <typename T>
        struct ImplEnt {
            bool isSpecialisable;
            T data;
        };

        ::HIR::GenericParams mParams;
        ::HIR::PathParams traitArgs;
        ::HIR::TypeRef mType;

        ::std::map<RcString, ImplEnt<::HIR::Function>> methods;
        ::std::map<RcString, ImplEnt<::HIR::Constant>> constants;
        ::std::map<RcString, ImplEnt<::HIR::Static>> statics;

        ::std::map<RcString, ImplEnt<::HIR::TypeRef>> types;

        ::HIR::SimplePath srcModule;
        bool isConst = false;

        //
        //const TraitImpl*    m_parent_spec_impl;

        bool matchesType(const ::HIR::TypeData* tr, tCbResolveType ty_res) const;

        bool matchesType(const ::HIR::TypeData* tr) const {
            return matchesType(tr, ResolvePlaceholdersNop());
        }

        bool moreSpecificThan(TypeInterner& types, const TraitImpl& x) const;
        bool overlapsWith(const Crate& crate, const TraitImpl& other) const;
    };

    class MarkerImpl {
    public:
        ::HIR::GenericParams mParams;
        ::HIR::PathParams traitArgs;
        bool isPositive;
        ::HIR::TypeRef mType;

        ::HIR::SimplePath srcModule;

        bool matchesType(const ::HIR::TypeData* tr, tCbResolveType ty_res) const;

        bool matchesType(const ::HIR::TypeData* tr) const {
            return matchesType(tr, ResolvePlaceholdersNop());
        }
    };

    class GlobalAssembly {
    public:
        ::std::vector<AsmCommon::Line> lines;
        ::std::vector<HIR::Path> symbols;
        AsmCommon::Options options;
    };

    class ExternCrate {
    public:
        ::HIR::Crate* mData = nullptr;
        ::std::string basename; // Just the filename (serialised)
        ::std::string mPath;     // The path used to load this crate
    };

    class ExternLibrary {
    public:
        ::std::string name;
    };

    class Crate {
    public:
        stl::ObjPool* pool;
        TypeInterner& types;
        // Synthetic compiler item. Its signature uses this crate's interned
        // types, so it must not live in process-global storage.
        mutable ValueItem intrinsicOffsetof;
        RcString crateName;
        AST::Edition edition;
        // Compile-local crate configuration. This is not serialised because an
        // external crate can never provide this crate's executable entrypoint.
        bool isNoCore = false;
        bool noMain = false;
        // Enabled language features affect type checking of this crate only;
        // consumers of its metadata use their own feature set.
        ::std::set<RcString> features;

        Module rootModule;

        // Placeholder for types created during constant evaluation
        mutable std::vector<std::pair<RcString, std::unique_ptr<VisEnt<TypeItem>>>> newTypes;
        mutable std::vector<std::pair<RcString, std::unique_ptr<VisEnt<ValueItem>>>> newValues;

        template <typename T>
        struct ImplGroup {
            typedef ::std::vector<T> listT;
            ::std::map<::HIR::SimplePath, listT> named;
            listT nonNamed; // TODO: use a map of HIR::TypeRef::Data::Tag
            listT generic;

            const listT* getListForType(const ::HIR::TypeData* ty) const {
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

            listT& getListForTypeMut(const ::HIR::TypeData* ty) {
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
        ImplGroup<::std::unique_ptr<::HIR::TypeImpl>> typeImpls;

        /// CACHE: Cache of all inherent (non-trait) methods (for faster lookup)
        InherentCache inherentMethodCache;

        /// Impl blocks
        ::std::map<::HIR::SimplePath, ImplGroup<::std::unique_ptr<::HIR::TraitImpl>>> traitImpls;
        ::std::map<::HIR::SimplePath, ImplGroup<::std::unique_ptr<::HIR::MarkerImpl>>> markerImpls;

        /// Global assembly items
        ::std::vector<::HIR::GlobalAssembly> globalAsm;

        /// Merged index versions of the above
        ImplGroup<const ::HIR::TypeImpl*> allTypeImpls;
        ::std::map<::HIR::SimplePath, ImplGroup<const ::HIR::TraitImpl*>> allTraitImpls;
        ::std::map<::HIR::SimplePath, ImplGroup<const ::HIR::MarkerImpl*>> allMarkerImpls;

        /// List of legacy-exported macros
        std::vector<RcString> exportedMacroNames;

        /// Language items avaliable through this crate (includes ones from loaded externs)
        ::std::unordered_map<::std::string, ::HIR::SimplePath> mLangItems;

        /// Referenced crates (in load order) - Used to ensure final linking order is sane
        // NOT SERIALISED
        ::std::vector<RcString> extCratesOrdered;
        /// Referenced crates
        ::std::unordered_map<RcString, ExternCrate> extCrates;
        /// Referenced system libraries
        ::std::vector<ExternLibrary> extLibs;
        /// Extra paths for the linker
        ::std::vector<::std::string> linkPaths;

        Crate(stl::ObjPool* pool, TypeInterner& types);

        /// Method called to populate runtime state after deserialisation
        /// See hir/crate_post_load.cpp
        void postLoadUpdate(const RcString& loadedName);

        const ::HIR::SimplePath& getLangItemPath(const Span& sp, const char* name) const;
        const ::HIR::SimplePath& getLangItemPathOpt(const char* name) const;

        bool featureEnabled(const char* name) const {
            return features.count(RcString::newInterned(name)) != 0;
        }

        const ::HIR::MacroItem& getMacroitemByPath(const Span& sp, const ::HIR::SimplePath& path, bool ignoreCrateName = false, bool ignoreLastNode = false) const;

        const ::HIR::TypeItem& getTypeitemByPath(const Span& sp, const ::HIR::SimplePath& path, bool ignoreCrateName = false, bool ignoreLastNode = false) const;
        const ::HIR::Trait& getTraitByPath(const Span& sp, const ::HIR::SimplePath& path) const;
        ::std::optional<size_t> findMostSpecificTrait(const Span& sp, const ::std::vector<::HIR::SimplePath>& candidates) const;
        const ::HIR::Struct& getStructByPath(const Span& sp, const ::HIR::SimplePath& path) const;
        const ::HIR::Union& getUnionByPath(const Span& sp, const ::HIR::SimplePath& path) const;
        const ::HIR::Enum& getEnumByPath(const Span& sp, const ::HIR::SimplePath& path, bool ignoreCrateName = false, bool ignoreLastNode = false) const;
        const ::HIR::Module& getModByPath(const Span& sp, const ::HIR::SimplePath& path, bool ignoreLastNode = false, bool ignoreCrateName = false) const;

        const ::HIR::ValueItem& getValitemByPath(const Span& sp, const ::HIR::SimplePath& path, bool ignoreCrateName = false) const;
        const ::HIR::Function& getFunctionByPath(const Span& sp, const ::HIR::SimplePath& path) const;

        // NOTE: Special implementation to handle `m_inline_statics`
        const ::HIR::Static& getStaticByPath(const Span& sp, const ::HIR::SimplePath& path) const;

        const ::HIR::Constant& getConstantByPath(const Span& sp, const ::HIR::SimplePath& path) const;

        bool findTraitImpls(const ::HIR::SimplePath& path, const ::HIR::TypeData* type, tCbResolveType ty_res, ::std::function<bool(const ::HIR::TraitImpl&)> callback) const;
        bool findAutoTraitImpls(const ::HIR::SimplePath& path, const ::HIR::TypeData* type, tCbResolveType ty_res, ::std::function<bool(const ::HIR::MarkerImpl&)> callback) const;
        bool findTypeImpls(const ::HIR::TypeData* type, tCbResolveType ty_res, ::std::function<bool(const ::HIR::TypeImpl&)> callback) const;

        const ::MIR::Function* getOrGenMir(const ::HIR::ItemPath& ip, const ::HIR::ExprPtr& ep, const ::HIR::Function::argsT& args, ::HIR::TypeRef& ret_ty) const;

        const ::MIR::Function* getOrGenMir(const ::HIR::ItemPath& ip, const ::HIR::Function& fcn) const;

        const ::MIR::Function* getOrGenMir(const ::HIR::ItemPath& ip, const ::HIR::ExprPtr& ep, ::HIR::TypeRef& expTy) const;
    };

    /// Helper for obtaining the matching target for PathTuple/PathNamed
    const ::HIR::Struct& patternGetStruct(const Span& sp, const ::HIR::Path& path, const ::HIR::Pattern::PathBinding& binding, bool isTuple);
    const ::HIR::tTupleFields& patternGetTuple(const Span& sp, const ::HIR::Path& path, const ::HIR::Pattern::PathBinding& binding);
    const ::HIR::tStructFields& patternGetNamed(const Span& sp, const ::HIR::Path& path, const ::HIR::Pattern::PathBinding& binding);

} // namespace HIR
