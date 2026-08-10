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
struct Trans_Params: public MonomorphiserPP {
    Span sp;
    const ::HIR::GenericParams* gdef_impl;
    ::HIR::PathParams pp_method;
    ::HIR::PathParams pp_impl;
    ::HIR::TypeRef self_type;
    bool force_monomorphisation;

    explicit Trans_Params(HIR::TypeInterner& types)
        : MonomorphiserPP(types)
        , gdef_impl(nullptr)
        , force_monomorphisation(false)
    {
    }

    Trans_Params(HIR::TypeInterner& types, const Span& sp)
        : MonomorphiserPP(types)
        , sp(sp)
        , gdef_impl(nullptr)
        , force_monomorphisation(false)
    {
    }

    Trans_Params(Trans_Params&& x)
        : Trans_Params(x.type_interner())
    {
        *this = ::std::move(x);
    }

    Trans_Params& operator=(Trans_Params&& x) {
        sp = ::std::move(x.sp);
        gdef_impl = x.gdef_impl;
        pp_method = ::std::move(x.pp_method);
        pp_impl = ::std::move(x.pp_impl);
        self_type = x.self_type;
        force_monomorphisation = x.force_monomorphisation;
        return *this;
    }

    Trans_Params(const Trans_Params&) = delete;
    Trans_Params& operator=(const Trans_Params&) = delete;

    static Trans_Params new_impl(HIR::TypeInterner& types, Span sp, HIR::TypeRef ty, HIR::PathParams impl_params) {
        Trans_Params tp(types, sp);
        tp.self_type = std::move(ty);
        tp.pp_impl = std::move(impl_params);
        return tp;
    }

    const ::HIR::TypeData* maybe_monomorph(const ::StaticTraitResolve& resolve, ::HIR::TypeRef& tmp, const ::HIR::TypeData* p) const {
        if (monomorphise_type_needed(p)) {
            return tmp = this->monomorph(resolve, p);
        } else {
            return p;
        }
    }

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

struct TransList_Function {
    const ::HIR::Path* path; // Pointer into the list (std::map pointers are stable)
    const ::HIR::Function* ptr;
    Trans_Params pp;
    // If `pp.has_types` is true, the below is valid
    CachedFunction monomorphised;
    /// Forces the function to not be emited as code (just emit the signature)
    bool force_prototype;

    TransList_Function(HIR::TypeInterner& types, const ::HIR::Path& path)
        : path(&path)
        , ptr(nullptr)
        , pp(types)
        , force_prototype(false)
    {
    }
};

struct TransList_Static {
    const ::HIR::Static* ptr;
    Trans_Params pp;

    explicit TransList_Static(HIR::TypeInterner& types): ptr(nullptr), pp(types) {}
};

struct TransList_Const {
    const ::HIR::Constant* ptr;
    Trans_Params pp;

    explicit TransList_Const(HIR::TypeInterner& types): ptr(nullptr), pp(types) {}
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

    ::std::map<::HIR::Path, ::std::unique_ptr<TransList_Function>> m_functions;
    ::std::map<::HIR::Path, ::std::unique_ptr<TransList_Static>> m_statics;
    /// Constants that are still Defer
    ::std::map<::HIR::Path, ::std::unique_ptr<TransList_Const>> m_constants;
    ::std::map<::HIR::Path, Trans_Params> m_vtables;
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

    TransList_Function* add_function(HIR::TypeInterner& types, ::HIR::Path p);
    TransList_Static* add_static(HIR::TypeInterner& types, ::HIR::Path p);
    TransList_Const* add_const(HIR::TypeInterner& types, ::HIR::Path p);
    TransList_Function* find_function(const ::HIR::Path& p);
    const TransList_Function* find_function(const ::HIR::Path& p) const;
    bool has_type(::HIR::TypeRef type, bool shallow) const;
    bool add_type(::HIR::TypeRef type, bool shallow);
    void clear_types();

    bool add_vtable(::HIR::Path p, Trans_Params pp) {
        return m_vtables.insert(::std::make_pair(mv$(p), mv$(pp))).second;
    }
};
