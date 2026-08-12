#pragma once

#include "hir_path.h"
#include "hir_type.h"
#include "hir_typeck_common.h"

#include <unordered_map>

class StaticTraitResolve;

    class HIRCrate;
    class HIRFunction;
    class HIRStatic;

// TODO: This is very similar to "hir_typeck/common.h" MonomorphState, except it owns its data
struct TransParams: public MonomorphiserPP {
    Span sp;
    const HIRGenericParams* gdefImpl;
    HIRPathParams ppMethod;
    HIRPathParams ppImpl;
    HIRTypeRef selfType;
    bool forceMonomorphisation;

    explicit TransParams(HIRTypeInterner& types);

    TransParams(HIRTypeInterner& types, const Span& sp);

    TransParams(TransParams&& x);

    TransParams& operator=(TransParams&& x);

    TransParams(const TransParams&) = delete;
    TransParams& operator=(const TransParams&) = delete;

    static TransParams newImpl(HIRTypeInterner& types, Span sp, HIRTypeRef ty, HIRPathParams implParams);

    const HIRTypeData* maybeMonomorph(const ::StaticTraitResolve& resolve, HIRTypeRef& tmp, const HIRTypeData* p) const;

    HIRTypeRef monomorph(const ::StaticTraitResolve& resolve, const HIRTypeData* p) const;
    HIRPath monomorph(const ::StaticTraitResolve& resolve, const HIRPath& p) const;
    HIRGenericPath monomorph(const ::StaticTraitResolve& resolve, const HIRGenericPath& p) const;
    HIRPathParams monomorph(const ::StaticTraitResolve& resolve, const HIRPathParams& p) const;

    bool hasTypes() const {
        return forceMonomorphisation || ppMethod.hasParams() || ppImpl.hasParams();
    }

    const HIRTypeData* getSelfType() const override {
        return selfType;
    }

    const HIRPathParams* getImplParams() const override {
        return &ppImpl;
    }

    const HIRPathParams* getMethodParams() const override {
        return &ppMethod;
    }

    const HIRPathParams* getHrbParams() const override {
        return nullptr;
    }
};

struct CachedFunction {
    HIRTypeRef retTy;
    HIRFunction::argsT argTys;
    MIRFunctionPointer code;
};

struct TransListFunction {
    const HIRPath* path; // Pointer into the list (std::map pointers are stable)
    const HIRFunction* ptr;
    TransParams pp;
    // If `pp.has_types` is true, the below is valid
    CachedFunction monomorphised;
    /// Forces the function to not be emited as code (just emit the signature)
    bool forcePrototype;

    TransListFunction(HIRTypeInterner& types, const HIRPath& path);
};

struct TransListStatic {
    const HIRStatic* ptr;
    TransParams pp;

    explicit TransListStatic(HIRTypeInterner& types);
};

struct TransListConst {
    const HIRConstant* ptr;
    TransParams pp;

    explicit TransListConst(HIRTypeInterner& types);
};

class TransList {
    // Translation erases regions, so an exact HIR path is not the identity of
    // an emitted symbol. Keep the ABI identity alongside the exact-path maps.
    ::std::unordered_map<::std::string, HIRPath> functionSymbols;
    ::std::unordered_map<::std::string, HIRPath> staticSymbols;

    struct TypeEmissionState {
        HIRTypeRef canonical;
        bool hasPrototype;
        bool hasDefinition;
    };

    ::std::unordered_map<::std::string, TypeEmissionState> typeSymbols;

public:
    TransList() = default;
    TransList(TransList&&) = default;
    TransList(const TransList&) = delete;
    TransList& operator=(TransList&&) = default;
    TransList& operator=(const TransList&) = delete;

    /// Root-level items (exposed globals)
    ::std::vector<HIRPath> roots;

    ::std::map<HIRPath, ::std::unique_ptr<TransListFunction>> functions;
    ::std::map<HIRPath, ::std::unique_ptr<TransListStatic>> statics;
    /// Constants that are still Defer
    ::std::map<HIRPath, ::std::unique_ptr<TransListConst>> constants;
    ::std::map<HIRPath, TransParams> vtables;
    /// Required type_id values
    ::std::set<HIRTypeRef> typeids;
    // Required drop glue
    ::std::set<HIRTypeRef> dropGlue;
    /// Required struct/enum constructor impls
    ::std::set<HIRGenericPath> constructors;
    // Automatic Clone impls
    ::std::set<HIRTypeRef> autoCloneImpls;
    // Automatic FnPtr impls
    ::std::set<HIRTypeRef> autoFnptrImpls;
    // Trait methods
    ::std::set<HIRPath> traitObjectMethods;

    ::std::vector<::std::unique_ptr<HIRStatic>> autoStatics;
    ::std::vector<::std::unique_ptr<HIRFunction>> autoFunctions;

    // .second is `true` if this is a from a reference to the type
    ::std::vector<::std::pair<HIRTypeRef, bool>> types;

    TransListFunction* addFunction(HIRTypeInterner& types, HIRPath p);
    TransListStatic* addStatic(HIRTypeInterner& types, HIRPath p);
    TransListConst* addConst(HIRTypeInterner& types, HIRPath p);
    TransListFunction* findFunction(const HIRPath& p);
    const TransListFunction* findFunction(const HIRPath& p) const;
    bool hasType(HIRTypeRef type, bool shallow) const;
    bool addType(HIRTypeRef type, bool shallow);
    void clearTypes();

    bool addVtable(HIRPath p, TransParams pp) {
        return vtables.insert(::std::make_pair(mv$(p), mv$(pp))).second;
    }
};
