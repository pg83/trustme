#pragma once

#include "hir_type.h"
#include "hir_path.h"
#include "hir_typeck_common.h"
#include <unordered_map>

class StaticTraitResolve;

namespace HIR {
    class Crate;
    class Function;
    class Static;
}

// TODO: This is very similar to "hir_typeck/common.h" MonomorphState, except it owns its data
struct TransParams: public MonomorphiserPP {
    Span sp;
    const ::HIR::GenericParams* gdef_impl;
    ::HIR::PathParams pp_method;
    ::HIR::PathParams pp_impl;
    ::HIR::TypeRef self_type;
    bool force_monomorphisation;

    explicit TransParams(HIR::TypeInterner& types);

    TransParams(HIR::TypeInterner& types, const Span& sp);

    TransParams(TransParams&& x);

    TransParams& operator=(TransParams&& x);

    TransParams(const TransParams&) = delete;
    TransParams& operator=(const TransParams&) = delete;

    static TransParams new_impl(HIR::TypeInterner& types, Span sp, HIR::TypeRef ty, HIR::PathParams impl_params);

    const ::HIR::TypeData* maybe_monomorph(const ::StaticTraitResolve& resolve, ::HIR::TypeRef& tmp, const ::HIR::TypeData* p) const;

    ::HIR::TypeRef monomorph(const ::StaticTraitResolve& resolve, const ::HIR::TypeData* p) const;
    ::HIR::Path monomorph(const ::StaticTraitResolve& resolve, const ::HIR::Path& p) const;
    ::HIR::GenericPath monomorph(const ::StaticTraitResolve& resolve, const ::HIR::GenericPath& p) const;
    ::HIR::PathParams monomorph(const ::StaticTraitResolve& resolve, const ::HIR::PathParams& p) const;

    bool has_types() const {
        return force_monomorphisation || pp_method.has_params() || pp_impl.has_params();
    }

    const ::HIR::TypeData* get_self_type() const override {
        return self_type;
    }

    const ::HIR::PathParams* get_impl_params() const override {
        return &pp_impl;
    }

    const ::HIR::PathParams* get_method_params() const override {
        return &pp_method;
    }

    const ::HIR::PathParams* get_hrb_params() const override {
        return nullptr;
    }
};

struct CachedFunction {
    ::HIR::TypeRef ret_ty;
    ::HIR::Function::args_t arg_tys;
    ::MIR::FunctionPointer code;
};

struct TransListFunction {
    const ::HIR::Path* path; // Pointer into the list (std::map pointers are stable)
    const ::HIR::Function* ptr;
    TransParams pp;
    // If `pp.has_types` is true, the below is valid
    CachedFunction monomorphised;
    /// Forces the function to not be emited as code (just emit the signature)
    bool force_prototype;

    TransListFunction(HIR::TypeInterner& types, const ::HIR::Path& path);
};

struct TransListStatic {
    const ::HIR::Static* ptr;
    TransParams pp;

    explicit TransListStatic(HIR::TypeInterner& types);
};

struct TransListConst {
    const ::HIR::Constant* ptr;
    TransParams pp;

    explicit TransListConst(HIR::TypeInterner& types);
};

class TransList {
    // Translation erases regions, so an exact HIR path is not the identity of
    // an emitted symbol. Keep the ABI identity alongside the exact-path maps.
    ::std::unordered_map<::std::string, ::HIR::Path> m_function_symbols;
    ::std::unordered_map<::std::string, ::HIR::Path> m_static_symbols;
    struct TypeEmissionState {
        ::HIR::TypeRef canonical;
        bool has_prototype;
        bool has_definition;
    };
    ::std::unordered_map<::std::string, TypeEmissionState> m_type_symbols;

public:
    TransList() = default;
    TransList(TransList&&) = default;
    TransList(const TransList&) = delete;
    TransList& operator=(TransList&&) = default;
    TransList& operator=(const TransList&) = delete;

    /// Root-level items (exposed globals)
    ::std::vector<HIR::Path> m_roots;

    ::std::map<::HIR::Path, ::std::unique_ptr<TransListFunction>> m_functions;
    ::std::map<::HIR::Path, ::std::unique_ptr<TransListStatic>> m_statics;
    /// Constants that are still Defer
    ::std::map<::HIR::Path, ::std::unique_ptr<TransListConst>> m_constants;
    ::std::map<::HIR::Path, TransParams> m_vtables;
    /// Required type_id values
    ::std::set<::HIR::TypeRef> m_typeids;
    // Required drop glue
    ::std::set<::HIR::TypeRef> m_drop_glue;
    /// Required struct/enum constructor impls
    ::std::set<::HIR::GenericPath> m_constructors;
    // Automatic Clone impls
    ::std::set<::HIR::TypeRef> auto_clone_impls;
    // Automatic FnPtr impls
    ::std::set<::HIR::TypeRef> auto_fnptr_impls;
    // Trait methods
    ::std::set<::HIR::Path> trait_object_methods;

    ::std::vector<::std::unique_ptr<::HIR::Static>> m_auto_statics;
    ::std::vector<::std::unique_ptr<::HIR::Function>> m_auto_functions;

    // .second is `true` if this is a from a reference to the type
    ::std::vector<::std::pair<::HIR::TypeRef, bool>> m_types;

    TransListFunction* add_function(HIR::TypeInterner& types, ::HIR::Path p);
    TransListStatic* add_static(HIR::TypeInterner& types, ::HIR::Path p);
    TransListConst* add_const(HIR::TypeInterner& types, ::HIR::Path p);
    TransListFunction* find_function(const ::HIR::Path& p);
    const TransListFunction* find_function(const ::HIR::Path& p) const;
    bool has_type(::HIR::TypeRef type, bool shallow) const;
    bool add_type(::HIR::TypeRef type, bool shallow);
    void clear_types();

    bool add_vtable(::HIR::Path p, TransParams pp) {
        return m_vtables.insert(::std::make_pair(mv$(p), mv$(pp))).second;
    }
};
