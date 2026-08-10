/*
 * MRustC - Rust Compiler
 * - By John Hodge (Mutabah/thePowersGang)
 *
 * trans/trans_list.cpp
 * - A list of items that require translation
 */
#include "trans_trans_list.h"
#include "hir_typeck_static.h" // StaticTraitResolve
#include "trans_mangling.h"

TransList_Function* TransList::add_function(HIR::TypeInterner& types, ::HIR::Path p) {
    auto symbol = FMT(Trans_Mangle(p));
    auto existing = m_function_symbols.find(symbol);
    if (existing != m_function_symbols.end()) {
        ASSERT_BUG(Span(), existing->second.equals_ignoring_regions(p),
            "Distinct function paths have the same mangled name: " << existing->second << " and " << p);
        return nullptr;
    }

    auto rv = m_functions.insert(::std::make_pair(mv$(p), nullptr));
    if (rv.second) {
        m_function_symbols.emplace(mv$(symbol), rv.first->first.clone());
        DEBUG("Function " << rv.first->first);
        assert(!rv.first->second);
        rv.first->second.reset(new TransList_Function(types, rv.first->first));
        return &*rv.first->second;
    } else {
        return nullptr;
    }
}

const TransList_Function* TransList::find_function(const ::HIR::Path& p) const {
    auto exact = m_functions.find(p);
    if (exact != m_functions.end()) {
        return exact->second.get();
    }

    const auto symbol = FMT(Trans_Mangle(p));
    auto canonical = m_function_symbols.find(symbol);
    if (canonical == m_function_symbols.end()) {
        return nullptr;
    }
    ASSERT_BUG(Span(), canonical->second.equals_ignoring_regions(p),
        "Distinct function paths have the same mangled name: " << canonical->second << " and " << p);
    exact = m_functions.find(canonical->second);
    ASSERT_BUG(Span(), exact != m_functions.end(), "Function symbol index is stale for " << p);
    return exact->second.get();
}

TransList_Function* TransList::find_function(const ::HIR::Path& p) {
    return const_cast<TransList_Function*>(static_cast<const TransList&>(*this).find_function(p));
}

bool TransList::has_type(::HIR::TypeRef type, bool shallow) const {
    const auto symbol = FMT(Trans_Mangle(type));
    const auto existing = m_type_symbols.find(symbol);
    if (existing == m_type_symbols.end()) {
        return false;
    }
    ASSERT_BUG(Span(), existing->second.canonical == type || existing->second.canonical->equals_ignoring_regions(type),
        "Distinct types have the same mangled name: " << existing->second.canonical << " and " << type);
    return existing->second.has_definition || (shallow && existing->second.has_prototype);
}

bool TransList::add_type(::HIR::TypeRef type, bool shallow) {
    auto symbol = FMT(Trans_Mangle(type));
    auto existing = m_type_symbols.find(symbol);
    if (existing == m_type_symbols.end()) {
        m_type_symbols.emplace(mv$(symbol), TypeEmissionState{type, shallow, !shallow});
    } else {
        auto& state = existing->second;
        ASSERT_BUG(Span(), state.canonical == type || state.canonical->equals_ignoring_regions(type),
            "Distinct types have the same mangled name: " << state.canonical << " and " << type);
        auto& already_emitted = shallow ? state.has_prototype : state.has_definition;
        if (already_emitted || (shallow && state.has_definition)) {
            return false;
        }
        already_emitted = true;
    }
    m_types.push_back(::std::make_pair(type, shallow));
    return true;
}

void TransList::clear_types() {
    m_types.clear();
    m_type_symbols.clear();
}

TransList_Static* TransList::add_static(HIR::TypeInterner& types, ::HIR::Path p) {
    auto symbol = FMT(Trans_Mangle(p));
    auto existing = m_static_symbols.find(symbol);
    if (existing != m_static_symbols.end()) {
        ASSERT_BUG(Span(), existing->second.equals_ignoring_regions(p),
            "Distinct static paths have the same mangled name: " << existing->second << " and " << p);
        return nullptr;
    }

    auto rv = m_statics.insert(::std::make_pair(mv$(p), nullptr));
    if (rv.second) {
        m_static_symbols.emplace(mv$(symbol), rv.first->first.clone());
        DEBUG("Static " << rv.first->first);
        assert(!rv.first->second);
        rv.first->second.reset(new TransList_Static(types));
        return &*rv.first->second;
    } else {
        return nullptr;
    }
}

TransList_Const* TransList::add_const(HIR::TypeInterner& types, ::HIR::Path p) {
    auto rv = m_constants.insert(::std::make_pair(mv$(p), nullptr));
    if (rv.second) {
        DEBUG("Const " << rv.first->first);
        assert(!rv.first->second);
        rv.first->second.reset(new TransList_Const(types));
        return &*rv.first->second;
    } else {
        return nullptr;
    }
}

::HIR::Path Trans_Params::monomorph(const ::StaticTraitResolve& resolve, const ::HIR::Path& p) const {
    TRACE_FUNCTION_F(p);
    auto rv = this->monomorph_path(sp, p, false);

    TU_MATCH_HDRA( (rv.m_data), {)
    TU_ARMA(Generic, e2) {
            for (auto& arg : e2.m_params.m_types) {
                resolve.expand_associated_types(sp, arg);
            }
        }
        TU_ARMA(UfcsInherent, e2) {
            resolve.expand_associated_types(sp, e2.type);
            for (auto& arg : e2.params.m_types) {
                resolve.expand_associated_types(sp, arg);
            }
            // TODO: impl params too?
            for (auto& arg : e2.impl_params.m_types) {
                resolve.expand_associated_types(sp, arg);
            }
        }
        TU_ARMA(UfcsKnown, e2) {
            resolve.expand_associated_types(sp, e2.type);
            for (auto& arg : e2.trait.m_params.m_types) {
                resolve.expand_associated_types(sp, arg);
            }
            for (auto& arg : e2.params.m_types) {
                resolve.expand_associated_types(sp, arg);
            }
        }
        TU_ARMA(UfcsUnknown, e2) {
            BUG(sp, "Encountered UfcsUnknown");
        }
    }
    return rv;
}

::HIR::GenericPath Trans_Params::monomorph(const ::StaticTraitResolve& resolve, const ::HIR::GenericPath& p) const {
    return ::HIR::GenericPath(p.m_path, this->monomorph(resolve, p.m_params));
}

::HIR::PathParams Trans_Params::monomorph(const ::StaticTraitResolve& resolve, const ::HIR::PathParams& p) const {
    auto rv = this->monomorph_path_params(sp, p, false);
    for (auto& arg : rv.m_types) {
        resolve.expand_associated_types(sp, arg);
    }
    return rv;
}

::HIR::TypeRef Trans_Params::monomorph(const ::StaticTraitResolve& resolve, const ::HIR::TypeRef& ty) const {
    return resolve.monomorph_expand(sp, ty, *this);
}
