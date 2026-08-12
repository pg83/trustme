#pragma once

#include "hir_hir.h"
#include "hir_typeck_common.h"
#include "hir_typeck_impl_ref.h"
#include "range_vec_map.h"
#include "hir_typeck_resolve_common.h"

enum class MetadataType {
    Unknown,     // Unknown still
    None,        // Sized pointer
    Zero,        // No metadata, but still unsized
    Slice,       // usize metadata
    TraitObject, // VTable pointer metadata
};

std::ostream& operator<<(std::ostream& os, const MetadataType& x);

class StaticTraitResolve: public TraitResolveCommon {
    class NextSolverBridge;

    MetadataType selfMetadata = MetadataType::Unknown;
    mutable ::std::map<::HIR::TypeRef, bool> copyCache;
    mutable ::std::map<::HIR::TypeRef, bool> cloneCache;
    mutable ::std::map<::HIR::TypeRef, bool> dropCache;
    mutable ::std::map<std::string, HIR::TypeRef> atyCache;

    /// Cache of the result of find_impl__check_crate_raw
    mutable ::std::map<std::string, std::pair<HIR::PathParams, HIR::Compare>> cachedImplChecks;
    mutable ::std::vector<::std::tuple<
        const ::HIR::SimplePath*,
        const ::HIR::PathParams*,
        const ::HIR::TypeData*
    >> findImplStack;
    // Owned by the crate ObjPool and reused across all fully-static goals.
    mutable NextSolverBridge* nextSolver = nullptr;

public:
    explicit StaticTraitResolve(const ::HIR::Crate& crate);

private:
    void prepIndexes();

public:
    /// \brief State manipulation
    /// \{
    NullOnDrop<const ::HIR::GenericParams> setImplGenerics(HIR::StructMarkings::DstType structDstType, const ::HIR::GenericParams& gps);

    NullOnDrop<const ::HIR::GenericParams> setImplGenerics(MetadataType selfMetaType, const ::HIR::GenericParams& gps);

    NullOnDrop<const ::HIR::GenericParams> setImplGenerics(const ::HIR::TypeData* selfTy, const ::HIR::GenericParams& gps);

    void updateImplSelfMetadata(const ::HIR::TypeData* selfTy);

    NullOnDrop<const ::HIR::GenericParams> setItemGenerics(const ::HIR::GenericParams& gps);

    void setImplGenericsRaw(MetadataType selfMetaType, const ::HIR::GenericParams& gps);

    void clearImplGenerics();

    void setItemGenericsRaw(const ::HIR::GenericParams& gps);

    void clearItemGenerics();

    void setBothGenericsRaw(const ::HIR::GenericParams* gpsImpl, const ::HIR::GenericParams* gpsFcn);

    void clearBothGenerics();

    // Used by ResolveUFCS to regenerate
    void prepIndexes(const Span& sp) {
        TraitResolveCommon::prepIndexes(sp);
    }

    /// \}

    /// \brief Lookups
    /// \{
    typedef ::std::function<bool(ImplRef, bool isFuzzed)> tCbFindImpl;

    bool findImpl(const Span& sp, const ::HIR::SimplePath& traitPath, const ::HIR::PathParams& traitParams, const ::HIR::TypeData* type, tCbFindImpl foundCb) const {
        return this->findImpl(sp, traitPath, &traitParams, type, foundCb);
    }

    bool findImpl(const Span& sp, const ::HIR::SimplePath& traitPath, const ::HIR::PathParams* traitParams, const ::HIR::TypeData* type, tCbFindImpl foundCb, bool dontHandoffToSpecialised = false) const;

private:
    bool findImplBounds(const Span& sp, const ::HIR::SimplePath& traitPath, const ::HIR::PathParams* traitParams, const ::HIR::TypeData* type, tCbFindImpl foundCb) const;
    bool findImplCheckCrate(const Span& sp, const ::HIR::SimplePath& traitPath, const ::HIR::PathParams* traitParams, const ::HIR::TypeData* type, tCbFindImpl foundCb, const ::HIR::TraitImpl& impl) const;
    bool findImplCheckCrateRaw(const Span& sp, const ::HIR::SimplePath& desTraitPath, const ::HIR::PathParams* desTraitParams, const ::HIR::TypeData* desType, const ::HIR::GenericParams& implParamsDef, const ::HIR::PathParams& implTraitParams, const ::HIR::TypeData* implType, ::std::function<bool(HIR::PathParams, ::HIR::Compare)>) const;
    ::HIR::Compare checkAutoTraitImplDestructure(const Span& sp, const ::HIR::SimplePath& trait, const ::HIR::PathParams* paramsPtr, const ::HIR::TypeData* type) const;

public:
    const ::HIR::TypeData* fixTraitDefaultReturn(const Span& sp, const HIR::ItemPath& p, const ::HIR::TypeData* tpl, ::HIR::TypeRef& tmp) const;

    void expandAssociatedTypes(const Span& sp, ::HIR::TypeRef& input) const;
    void expandAssociatedTypesPath(const Span& sp, ::HIR::Path& input) const;
    void evaluateArraySize(const Span& sp, ::HIR::ArraySize& size) const;
    void evaluateConstGeneric(const Span& sp, ::HIR::ConstGeneric& value) const;
    void evaluatePathParams(const Span& sp, ::HIR::PathParams& params) const;
    bool expandAssociatedTypesSingle(const Span& sp, ::HIR::TypeRef& input) const;
    bool typesEqualResolvingOpaque(const Span& sp, const ::HIR::TypeData* left, const ::HIR::TypeData* right) const;

    // Helper: Run monomorphise+EAT if the type contains generics
    const ::HIR::TypeData* monomorphExpandOpt(const Span& sp, ::HIR::TypeRef& tmp, const ::HIR::TypeData* input, const Monomorphiser& m) const;

    ::HIR::TypeRef monomorphExpand(const Span& sp, const ::HIR::TypeData* input, const Monomorphiser& m) const;

    void expandAssociatedTypesTp(const Span& sp, ::HIR::TraitPath& input) const;

private:
    void expandAssociatedTypesParams(const Span& sp, ::HIR::PathParams& input) const;
    void expandAssociatedTypesInner(const Span& sp, ::HIR::TypeRef& input) const;
    bool expandAssociatedTypesUfcsInherent(const Span& sp, ::HIR::TypeRef& input) const;
    bool expandAssociatedTypesUfcsKnown(const Span& sp, ::HIR::TypeRef& input, bool recurse = true) const;

protected:
    virtual bool replaceEqualities(::HIR::TypeRef& input) const;

public:
    /// \}

    /// Locate a named trait in the provied trait (either itself or as a parent trait)
    bool findNamedTraitInTrait(const Span& sp, const ::HIR::SimplePath& des, const ::HIR::PathParams& params, const ::HIR::Trait& traitPtr, const ::HIR::SimplePath& traitPath, const ::HIR::PathParams& pp, const ::HIR::TypeData* selfType, ::std::function<bool(const ::HIR::PathParams&, ::HIR::TraitPath::assocListT)> callback) const;
    ///
    bool traitContainsType(const Span& sp, const ::HIR::GenericPath& traitPath, const ::HIR::Trait& traitPtr, const char* name, ::HIR::GenericPath& outPath) const;
    bool iterateAtyBounds(const Span& sp, const ::HIR::Path::Data::Data_UfcsKnown& pe, ::std::function<bool(const ::HIR::TraitPath&)> cb) const;

    // --------------
    // Common bounds
    // -------------
    bool typeIsCopy(const Span& sp, const ::HIR::TypeData* ty) const;
    bool typeIsClone(const Span& sp, const ::HIR::TypeData* ty) const; // 1.29
    bool typeIsSized(const Span& sp, const ::HIR::TypeData* ty) const;
    bool typeIsImpossible(const Span& sp, const ::HIR::TypeData* ty) const;
    bool canUnsize(const Span& sp, const ::HIR::TypeData* dst, const ::HIR::TypeData* src) const;
    /// Check if the passed type contains an UnsafeCell (i.e. is interior mutable)
    /// Returns:
    /// - `Fuzzy` if generic (can't know for sure yet)
    /// - `Equal` if it does contain an UnsafeCell
    //  - `Unequal` if it doesn't (shared=immutable)
    HIR::Compare typeIsInteriorMutable(const Span& sp, const ::HIR::TypeData* ty) const;

    MetadataType metadataType(const Span& sp, const ::HIR::TypeData* ty, bool errOnUnknown = false) const;

    /// Returns `true` if the passed type either implements Drop, or contains a type that implements Drop
    bool typeNeedsDropGlue(const Span& sp, const ::HIR::TypeData* ty) const;

    const ::HIR::TypeData* isTypeOwnedBox(const ::HIR::TypeData* ty) const;
    const ::HIR::TypeData* isTypePhantomData(const ::HIR::TypeData* ty) const;

    HIR::TypeRef getFieldType(const Span& sp, const ::HIR::TypeData* ty, const RcString& name) const;

    TAGGED_UNION(
        ValuePtr,
        NotFound,
        (NotFound, struct {}),
        (NotYetKnown, struct {}),
        (Constant, const ::HIR::Constant*),
        (Static, const ::HIR::Static*),
        (Function, const ::HIR::Function*),
        (EnumConstructor,
         struct {
             const ::HIR::Enum* e;
             size_t v;
         }),
        (EnumValue,
         struct {
             const ::HIR::Enum* e;
             size_t v;
         }),
        (StructConstructor,
         struct {
             const ::HIR::SimplePath* p;
             const ::HIR::Struct* s;
         }),
        (StructConstant, struct {
            const ::HIR::SimplePath* p;
            const ::HIR::Struct* s;
        })
    );

    /// `signature_only` - Returns a pointer to an item with the correct signature, not the actual implementation (faster)
    ValuePtr getValue(const Span& sp, const ::HIR::Path& p, MonomorphState& outParams, bool signatureOnly = false, const HIR::GenericParams** outImplParamsDef = nullptr) const;
};
