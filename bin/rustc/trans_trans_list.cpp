#include "trans_trans_list.h"
#include "hir_typeck_static.h" // StaticTraitResolve
#include "trans_mangling.h"

TransListFunction* TransList::add_function(HIR::TypeInterner& types, ::HIR::Path p) {
    auto symbol = FMT(TransMangle(p));
    auto existing = functionSymbols.find(symbol);
    if (existing != functionSymbols.end()) {
        ASSERT_BUG(Span(), existing->second.equals_ignoring_regions(p),
            "Distinct function paths have the same mangled name: " << existing->second << " and " << p);
        return nullptr;
    }

    auto rv = functions.insert(::std::make_pair(mv$(p), nullptr));
    if (rv.second) {
        functionSymbols.emplace(mv$(symbol), rv.first->first.clone());
        DEBUG("Function " << rv.first->first);
        assert(!rv.first->second);
        rv.first->second.reset(new TransListFunction(types, rv.first->first));
        return &*rv.first->second;
    } else {
        return nullptr;
    }
}

const TransListFunction* TransList::find_function(const ::HIR::Path& p) const {
    auto exact = functions.find(p);
    if (exact != functions.end()) {
        return exact->second.get();
    }

    const auto symbol = FMT(TransMangle(p));
    auto canonical = functionSymbols.find(symbol);
    if (canonical == functionSymbols.end()) {
        return nullptr;
    }
    ASSERT_BUG(Span(), canonical->second.equals_ignoring_regions(p),
        "Distinct function paths have the same mangled name: " << canonical->second << " and " << p);
    exact = functions.find(canonical->second);
    ASSERT_BUG(Span(), exact != functions.end(), "Function symbol index is stale for " << p);
    return exact->second.get();
}

TransListFunction* TransList::find_function(const ::HIR::Path& p) {
    return const_cast<TransListFunction*>(static_cast<const TransList&>(*this).find_function(p));
}

bool TransList::has_type(::HIR::TypeRef type, bool shallow) const {
    const auto symbol = FMT(TransMangle(type));
    const auto existing = typeSymbols.find(symbol);
    if (existing == typeSymbols.end()) {
        return false;
    }
    ASSERT_BUG(Span(), existing->second.canonical == type || existing->second.canonical->equals_ignoring_regions(type),
        "Distinct types have the same mangled name: " << existing->second.canonical << " and " << type);
    return existing->second.has_definition || (shallow && existing->second.has_prototype);
}

bool TransList::add_type(::HIR::TypeRef type, bool shallow) {
    auto symbol = FMT(TransMangle(type));
    auto existing = typeSymbols.find(symbol);
    if (existing == typeSymbols.end()) {
        typeSymbols.emplace(mv$(symbol), TypeEmissionState{type, shallow, !shallow});
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
    types.push_back(::std::make_pair(type, shallow));
    return true;
}

void TransList::clear_types() {
    types.clear();
    typeSymbols.clear();
}

TransListStatic* TransList::add_static(HIR::TypeInterner& types, ::HIR::Path p) {
    auto symbol = FMT(TransMangle(p));
    auto existing = staticSymbols.find(symbol);
    if (existing != staticSymbols.end()) {
        ASSERT_BUG(Span(), existing->second.equals_ignoring_regions(p),
            "Distinct static paths have the same mangled name: " << existing->second << " and " << p);
        return nullptr;
    }

    auto rv = statics.insert(::std::make_pair(mv$(p), nullptr));
    if (rv.second) {
        staticSymbols.emplace(mv$(symbol), rv.first->first.clone());
        DEBUG("Static " << rv.first->first);
        assert(!rv.first->second);
        rv.first->second.reset(new TransListStatic(types));
        return &*rv.first->second;
    } else {
        return nullptr;
    }
}

TransListConst* TransList::add_const(HIR::TypeInterner& types, ::HIR::Path p) {
    auto rv = constants.insert(::std::make_pair(mv$(p), nullptr));
    if (rv.second) {
        DEBUG("Const " << rv.first->first);
        assert(!rv.first->second);
        rv.first->second.reset(new TransListConst(types));
        return &*rv.first->second;
    } else {
        return nullptr;
    }
}

::HIR::Path TransParams::monomorph(const ::StaticTraitResolve& resolve, const ::HIR::Path& p) const {
    TRACE_FUNCTION_F(p);
    auto rv = this->monomorph_path(sp, p, false);

    TU_MATCH_HDRA( (rv.mData), {)
    TU_ARMA(Generic, e2) {
            for (auto& arg : e2.mParams.types) {
                resolve.expand_associated_types(sp, arg);
            }
        }
        TU_ARMA(UfcsInherent, e2) {
            resolve.expand_associated_types(sp, e2.type);
            for (auto& arg : e2.params.types) {
                resolve.expand_associated_types(sp, arg);
            }
            // TODO: impl params too?
            for (auto& arg : e2.impl_params.types) {
                resolve.expand_associated_types(sp, arg);
            }
        }
        TU_ARMA(UfcsKnown, e2) {
            resolve.expand_associated_types(sp, e2.type);
            for (auto& arg : e2.trait.mParams.types) {
                resolve.expand_associated_types(sp, arg);
            }
            for (auto& arg : e2.params.types) {
                resolve.expand_associated_types(sp, arg);
            }
        }
        TU_ARMA(UfcsUnknown, e2) {
            BUG(sp, "Encountered UfcsUnknown");
        }
    }
    return rv;
}

::HIR::GenericPath TransParams::monomorph(const ::StaticTraitResolve& resolve, const ::HIR::GenericPath& p) const {
    return ::HIR::GenericPath(p.mPath, this->monomorph(resolve, p.mParams));
}

::HIR::PathParams TransParams::monomorph(const ::StaticTraitResolve& resolve, const ::HIR::PathParams& p) const {
    auto rv = this->monomorph_path_params(sp, p, false);
    for (auto& arg : rv.types) {
        resolve.expand_associated_types(sp, arg);
    }
    return rv;
}

::HIR::TypeRef TransParams::monomorph(const ::StaticTraitResolve& resolve, const ::HIR::TypeData* ty) const {
    return resolve.monomorph_expand(sp, ty, *this);
}

TransParams::TransParams(HIR::TypeInterner& types)
    : MonomorphiserPP(types)
    , gdef_impl(nullptr)
    , force_monomorphisation(false) {
}
TransParams::TransParams(HIR::TypeInterner& types, const Span& sp)
    : MonomorphiserPP(types)
    , sp(sp)
    , gdef_impl(nullptr)
    , force_monomorphisation(false) {
}
TransParams::TransParams(TransParams&& x)
    : TransParams(x.type_interner()) {
    *this = ::std::move(x);
}
TransParams& TransParams::operator=(TransParams&& x) {
    sp = ::std::move(x.sp);
    gdef_impl = x.gdef_impl;
    pp_method = ::std::move(x.pp_method);
    pp_impl = ::std::move(x.pp_impl);
    self_type = x.self_type;
    force_monomorphisation = x.force_monomorphisation;
    return *this;
}
TransParams TransParams::new_impl(HIR::TypeInterner& types, Span sp, HIR::TypeRef ty, HIR::PathParams impl_params) {
    TransParams tp(types, sp);
    tp.self_type = std::move(ty);
    tp.pp_impl = std::move(impl_params);
    return tp;
}
const ::HIR::TypeData* TransParams::maybe_monomorph(const ::StaticTraitResolve& resolve, ::HIR::TypeRef& tmp, const ::HIR::TypeData* p) const {
    if (monomorphise_type_needed(p)) {
        return tmp = this->monomorph(resolve, p);
    } else {
        return p;
    }
}
TransListFunction::TransListFunction(HIR::TypeInterner& types, const ::HIR::Path& path)
    : path(&path)
    , ptr(nullptr)
    , pp(types)
    , force_prototype(false) {
}
TransListStatic::TransListStatic(HIR::TypeInterner& types): ptr(nullptr), pp(types) {}
TransListConst::TransListConst(HIR::TypeInterner& types): ptr(nullptr), pp(types) {}
