#include "trans_trans_list.h"

#include "trans_mangling.h"
#include "hir_typeck_static.h" // StaticTraitResolve

TransListFunction* TransList::addFunction(HIRTypeInterner& types, HIRPath p) {
    auto symbol = FMT(TransMangleValue(p));
    auto existing = functionSymbols.find(symbol);
    if (existing != functionSymbols.end()) {
        ASSERT_BUG(Span(), existing->second.equalsIgnoringRegions(p), "Distinct function paths have the same mangled name: " << existing->second << " and " << p);
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

const TransListFunction* TransList::findFunction(const HIRPath& p) const {
    auto exact = functions.find(p);
    if (exact != functions.end()) {
        return exact->second.get();
    }

    const auto symbol = FMT(TransMangleValue(p));
    auto canonical = functionSymbols.find(symbol);
    if (canonical == functionSymbols.end()) {
        return nullptr;
    }
    ASSERT_BUG(Span(), canonical->second.equalsIgnoringRegions(p), "Distinct function paths have the same mangled name: " << canonical->second << " and " << p);
    exact = functions.find(canonical->second);
    ASSERT_BUG(Span(), exact != functions.end(), "Function symbol index is stale for " << p);
    return exact->second.get();
}

TransListFunction* TransList::findFunction(const HIRPath& p) {
    return const_cast<TransListFunction*>(static_cast<const TransList&>(*this).findFunction(p));
}

bool TransList::hasType(HIRTypeRef type, bool shallow) const {
    const auto symbol = FMT(TransMangle(type));
    const auto existing = typeSymbols.find(symbol);
    if (existing == typeSymbols.end()) {
        return false;
    }
    ASSERT_BUG(Span(), existing->second.canonical == type || existing->second.canonical->equalsIgnoringRegions(type), "Distinct types have the same mangled name: " << existing->second.canonical << " and " << type);
    return existing->second.hasDefinition || (shallow && existing->second.hasPrototype);
}

bool TransList::addType(HIRTypeRef type, bool shallow) {
    auto symbol = FMT(TransMangle(type));
    auto existing = typeSymbols.find(symbol);
    if (existing == typeSymbols.end()) {
        typeSymbols.emplace(mv$(symbol), TypeEmissionState{type, shallow, !shallow});
    } else {
        auto& state = existing->second;
        ASSERT_BUG(Span(), state.canonical == type || state.canonical->equalsIgnoringRegions(type), "Distinct types have the same mangled name: " << state.canonical << " and " << type);
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

TransListStatic* TransList::addStatic(HIRTypeInterner& types, HIRPath p) {
    auto symbol = FMT(TransMangleValue(p));
    auto existing = staticSymbols.find(symbol);
    if (existing != staticSymbols.end()) {
        ASSERT_BUG(Span(), existing->second.equalsIgnoringRegions(p), "Distinct static paths have the same mangled name: " << existing->second << " and " << p);
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

TransListConst* TransList::addConst(HIRTypeInterner& types, HIRPath p) {
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

HIRPath TransParams::monomorph(const ::StaticTraitResolve& resolve, const HIRPath& p) const {
    TRACE_FUNCTION_F(p);
    auto rv = this->monomorphPath(sp, p, false);

    switch (rv.data.tag()) {
        case HIRPathData::TAG_Generic: {
            auto& e2 = rv.data.as_Generic();
            for (auto& arg : e2.params.types) {
                resolve.expandAssociatedTypes(sp, arg);
            }
            break;
        }
        case HIRPathData::TAG_UfcsInherent: {
            auto& e2 = rv.data.as_UfcsInherent();
            resolve.expandAssociatedTypes(sp, e2.type);
            for (auto& arg : e2.params.types) {
                resolve.expandAssociatedTypes(sp, arg);
            }
            // TODO: impl params too?
            for (auto& arg : e2.implParams.types) {
                resolve.expandAssociatedTypes(sp, arg);
            }
            break;
        }
        case HIRPathData::TAG_UfcsKnown: {
            auto& e2 = rv.data.as_UfcsKnown();
            resolve.expandAssociatedTypes(sp, e2.type);
            for (auto& arg : e2.trait.params.types) {
                resolve.expandAssociatedTypes(sp, arg);
            }
            for (auto& arg : e2.params.types) {
                resolve.expandAssociatedTypes(sp, arg);
            }
            break;
        }
        case HIRPathData::TAG_UfcsUnknown: {
            BUG(sp, "Encountered UfcsUnknown");
            break;
        }
    }
    return rv;
}

HIRGenericPath TransParams::monomorph(const ::StaticTraitResolve& resolve, const HIRGenericPath& p) const {
    return HIRGenericPath(p.path, this->monomorph(resolve, p.params));
}

HIRPathParams TransParams::monomorph(const ::StaticTraitResolve& resolve, const HIRPathParams& p) const {
    auto rv = this->monomorphPathParams(sp, p, false);
    for (auto& arg : rv.types) {
        resolve.expandAssociatedTypes(sp, arg);
    }
    return rv;
}

HIRTypeRef TransParams::monomorph(const ::StaticTraitResolve& resolve, const HIRTypeData* ty) const {
    return resolve.monomorphExpand(sp, ty, *this);
}

TransParams::TransParams(HIRTypeInterner& types)
    : MonomorphiserPP(types)
    , gdefImpl(nullptr)
    , forceMonomorphisation(false)
{
}

TransParams::TransParams(HIRTypeInterner& types, const Span& sp)
    : MonomorphiserPP(types)
    , sp(sp)
    , gdefImpl(nullptr)
    , forceMonomorphisation(false)
{
}

TransParams::TransParams(TransParams&& x)
    : TransParams(x.typeInterner())
{
    *this = ::std::move(x);
}

TransParams& TransParams::operator=(TransParams&& x) {
    sp = ::std::move(x.sp);
    gdefImpl = x.gdefImpl;
    ppMethod = ::std::move(x.ppMethod);
    ppImpl = ::std::move(x.ppImpl);
    selfType = x.selfType;
    forceMonomorphisation = x.forceMonomorphisation;
    return *this;
}

TransParams TransParams::newImpl(HIRTypeInterner& types, Span sp, HIRTypeRef ty, HIRPathParams implParams) {
    TransParams tp(types, sp);
    tp.selfType = std::move(ty);
    tp.ppImpl = std::move(implParams);
    return tp;
}

const HIRTypeData* TransParams::maybeMonomorph(const ::StaticTraitResolve& resolve, HIRTypeRef& tmp, const HIRTypeData* p) const {
    if (monomorphiseTypeNeeded(p)) {
        return tmp = this->monomorph(resolve, p);
    } else {
        return p;
    }
}

TransListFunction::TransListFunction(HIRTypeInterner& types, const HIRPath& path)
    : path(&path)
    , ptr(nullptr)
    , pp(types)
    , forcePrototype(false)
{
}

TransListStatic::TransListStatic(HIRTypeInterner& types)
    : ptr(nullptr)
    , pp(types)
{
}

TransListConst::TransListConst(HIRTypeInterner& types)
    : ptr(nullptr)
    , pp(types)
{
}

const HIRTypeData* TransParams::getSelfType() const {
    return selfType;
}

const HIRPathParams* TransParams::getImplParams() const {
    return &ppImpl;
}

const HIRPathParams* TransParams::getMethodParams() const {
    return &ppMethod;
}

const HIRPathParams* TransParams::getHrbParams() const {
    return nullptr;
}
