#include "trans_trans_list.h"
#include "hir_typeck_static.h" // StaticTraitResolve
#include "trans_mangling.h"

TransListFunction* TransList::addFunction(HIR::TypeInterner& types, ::HIR::Path p) {
    auto symbol = FMT(TransMangle(p));
    auto existing = functionSymbols.find(symbol);
    if (existing != functionSymbols.end()) {
        ASSERT_BUG(Span(), existing->second.equalsIgnoringRegions(p),
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

const TransListFunction* TransList::findFunction(const ::HIR::Path& p) const {
    auto exact = functions.find(p);
    if (exact != functions.end()) {
        return exact->second.get();
    }

    const auto symbol = FMT(TransMangle(p));
    auto canonical = functionSymbols.find(symbol);
    if (canonical == functionSymbols.end()) {
        return nullptr;
    }
    ASSERT_BUG(Span(), canonical->second.equalsIgnoringRegions(p),
        "Distinct function paths have the same mangled name: " << canonical->second << " and " << p);
    exact = functions.find(canonical->second);
    ASSERT_BUG(Span(), exact != functions.end(), "Function symbol index is stale for " << p);
    return exact->second.get();
}

TransListFunction* TransList::findFunction(const ::HIR::Path& p) {
    return const_cast<TransListFunction*>(static_cast<const TransList&>(*this).findFunction(p));
}

bool TransList::hasType(::HIR::TypeRef type, bool shallow) const {
    const auto symbol = FMT(TransMangle(type));
    const auto existing = typeSymbols.find(symbol);
    if (existing == typeSymbols.end()) {
        return false;
    }
    ASSERT_BUG(Span(), existing->second.canonical == type || existing->second.canonical->equalsIgnoringRegions(type),
        "Distinct types have the same mangled name: " << existing->second.canonical << " and " << type);
    return existing->second.hasDefinition || (shallow && existing->second.hasPrototype);
}

bool TransList::addType(::HIR::TypeRef type, bool shallow) {
    auto symbol = FMT(TransMangle(type));
    auto existing = typeSymbols.find(symbol);
    if (existing == typeSymbols.end()) {
        typeSymbols.emplace(mv$(symbol), TypeEmissionState{type, shallow, !shallow});
    } else {
        auto& state = existing->second;
        ASSERT_BUG(Span(), state.canonical == type || state.canonical->equalsIgnoringRegions(type),
            "Distinct types have the same mangled name: " << state.canonical << " and " << type);
        auto& alreadyEmitted = shallow ? state.hasPrototype : state.hasDefinition;
        if (alreadyEmitted || (shallow && state.hasDefinition)) {
            return false;
        }
        alreadyEmitted = true;
    }
    types.push_back(::std::make_pair(type, shallow));
    return true;
}

void TransList::clearTypes() {
    types.clear();
    typeSymbols.clear();
}

TransListStatic* TransList::addStatic(HIR::TypeInterner& types, ::HIR::Path p) {
    auto symbol = FMT(TransMangle(p));
    auto existing = staticSymbols.find(symbol);
    if (existing != staticSymbols.end()) {
        ASSERT_BUG(Span(), existing->second.equalsIgnoringRegions(p),
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
    auto rv = this->monomorphPath(sp, p, false);

    TU_MATCH_HDRA( (rv.mData), {)
    TU_ARMA(Generic, e2) {
            for (auto& arg : e2.mParams.types) {
                resolve.expandAssociatedTypes(sp, arg);
            }
        }
        TU_ARMA(UfcsInherent, e2) {
            resolve.expandAssociatedTypes(sp, e2.type);
            for (auto& arg : e2.params.types) {
                resolve.expandAssociatedTypes(sp, arg);
            }
            // TODO: impl params too?
            for (auto& arg : e2.impl_params.types) {
                resolve.expandAssociatedTypes(sp, arg);
            }
        }
        TU_ARMA(UfcsKnown, e2) {
            resolve.expandAssociatedTypes(sp, e2.type);
            for (auto& arg : e2.trait.mParams.types) {
                resolve.expandAssociatedTypes(sp, arg);
            }
            for (auto& arg : e2.params.types) {
                resolve.expandAssociatedTypes(sp, arg);
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
    auto rv = this->monomorphPathParams(sp, p, false);
    for (auto& arg : rv.types) {
        resolve.expandAssociatedTypes(sp, arg);
    }
    return rv;
}

::HIR::TypeRef TransParams::monomorph(const ::StaticTraitResolve& resolve, const ::HIR::TypeData* ty) const {
    return resolve.monomorphExpand(sp, ty, *this);
}

TransParams::TransParams(HIR::TypeInterner& types)
    : MonomorphiserPP(types)
    , gdefImpl(nullptr)
    , forceMonomorphisation(false) {
}
TransParams::TransParams(HIR::TypeInterner& types, const Span& sp)
    : MonomorphiserPP(types)
    , sp(sp)
    , gdefImpl(nullptr)
    , forceMonomorphisation(false) {
}
TransParams::TransParams(TransParams&& x)
    : TransParams(x.type_interner()) {
    *this = ::std::move(x);
}
TransParams& TransParams::operator=(TransParams&& x) {
    sp = ::std::move(x.sp);
    gdefImpl = x.gdefImpl;
    pp_method = ::std::move(x.pp_method);
    pp_impl = ::std::move(x.pp_impl);
    self_type = x.self_type;
    forceMonomorphisation = x.forceMonomorphisation;
    return *this;
}
TransParams TransParams::newImpl(HIR::TypeInterner& types, Span sp, HIR::TypeRef ty, HIR::PathParams impl_params) {
    TransParams tp(types, sp);
    tp.self_type = std::move(ty);
    tp.pp_impl = std::move(impl_params);
    return tp;
}
const ::HIR::TypeData* TransParams::maybeMonomorph(const ::StaticTraitResolve& resolve, ::HIR::TypeRef& tmp, const ::HIR::TypeData* p) const {
    if (monomorphiseTypeNeeded(p)) {
        return tmp = this->monomorph(resolve, p);
    } else {
        return p;
    }
}
TransListFunction::TransListFunction(HIR::TypeInterner& types, const ::HIR::Path& path)
    : path(&path)
    , ptr(nullptr)
    , pp(types)
    , forcePrototype(false) {
}
TransListStatic::TransListStatic(HIR::TypeInterner& types): ptr(nullptr), pp(types) {}
TransListConst::TransListConst(HIR::TypeInterner& types): ptr(nullptr), pp(types) {}
