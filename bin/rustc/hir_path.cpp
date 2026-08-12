#include "hir_path.h"
#include "hir_type.h"
#include "hir_expr.h"
#include <algorithm>

namespace {
    bool gCompareHrls = false;
}

namespace HIR {
    TraitPath::TraitPath()
        : traitPtr(nullptr)
    {
    }

    TraitPath::TraitPath(::std::unique_ptr<GenericParams> hrtbs, GenericPath path)
        : hrtbs(::std::move(hrtbs))
        , mPath(::std::move(path))
        , traitPtr(nullptr)
    {
    }

    TraitPath::TraitPath(::std::unique_ptr<GenericParams> hrtbs, GenericPath path, assocListT typeBounds, ::std::map<RcString, AtyBound> traitBounds, const ::HIR::Trait* traitPtr, BoundConstness constness)
        : hrtbs(::std::move(hrtbs))
        , mPath(::std::move(path))
        , typeBounds(::std::move(typeBounds))
        , traitBounds(::std::move(traitBounds))
        , constness(constness)
        , traitPtr(traitPtr)
    {
    }

    TraitPath::~TraitPath() = default;
    TraitPath::TraitPath(TraitPath&&) = default;
    TraitPath& TraitPath::operator=(TraitPath&&) = default;

    ::std::ostream& operator<<(::std::ostream& os, const ::HIR::SimplePath& x) {
        if (x.crateName() != "") {
            os << "::\"" << x.crateName() << "\"";
        } else if (x.components().size() == 0) {
            os << "::";
        } else {
        }
        for (const auto& n : x.components()) {
            os << "::" << n;
        }
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const PathParams& x) {
        bool hasArgs = (x.mLifetimes.size() > 0 || x.types.size() > 0 || x.values.size() > 0);

        if (hasArgs) {
            os << "<";
        }
        for (const auto& lft : x.mLifetimes) {
            os << lft << ",";
        }
        for (const auto& ty : x.types) {
            os << ty << ",";
        }
        for (const auto& v : x.values) {
            os << "{" << v << "},";
        }
        if (hasArgs) {
            os << ">";
        }
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const GenericPath& x) {
        os << x.mPath << x.mParams;
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const TraitPath& x) {
        if (x.constness == BoundConstness::Always) {
            os << "const ";
        } else if (x.constness == BoundConstness::Maybe) {
            os << "[const] ";
        }
        if (x.hrtbs) {
            os << "for" << x.hrtbs->fmtArgs() << " ";
        }
        os << x.mPath.mPath;
        bool hasArgs = (x.mPath.mParams.mLifetimes.size() > 0 || x.mPath.mParams.types.size() > 0 || x.typeBounds.size() > 0 || x.traitBounds.size() > 0);

        if (hasArgs) {
            os << "<";
        }
        for (const auto& lft : x.mPath.mParams.mLifetimes) {
            os << lft << ",";
        }
        for (const auto& ty : x.mPath.mParams.types) {
            os << ty << ",";
        }
        for (const auto& v : x.mPath.mParams.values) {
            os << v << ",";
        }
        for (const auto& assoc : x.typeBounds) {
            os << assoc.first << "{" << assoc.second.sourceTrait << "}=" << assoc.second << ",";
        }
        for (const auto& assoc : x.traitBounds) {
            for (const auto& trait : assoc.second.traits) {
                os << assoc.first << "{" << assoc.second.sourceTrait << "}: " << trait << ",";
            }
        }
        if (hasArgs) {
            os << ">";
        }
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const Path& x) {
        TU_MATCH(::HIR::Path::Data, (x.mData), (e), (Generic, return os << e;), (UfcsInherent, return os << "<" << e.type << " /*- " << e.implParams << "*/>::" << e.item << e.params;), (UfcsKnown, os << "<" << e.type << " as "; if (e.hrtbs) { os << "for" << e.hrtbs->fmtArgs() << " "; } os << e.trait << ">::" << e.item << e.params; return os;), (UfcsUnknown, return os << "<" << e.type << " as _>::" << e.item << e.params;))
        return os;
    }
}

::HIR::SimplePath HIR::SimplePath::clone() const {
    return SimplePath(members);
}

::HIR::SimplePath HIR::SimplePath::parent() const {
    if (members.size() > 1) {
        return SimplePath(ThinVector<RcString>(members.begin(), members.end() - 1));
    } else {
        return this->clone();
    }
}

::HIR::SimplePath HIR::SimplePath::operator+(const RcString& s) const {
    if (members.empty()) {
        return ThinVector<RcString>({RcString(), s});
    } else {
        SimplePath rv;
        rv.members.reserve(members.size());
        for (const auto& v : members) {
            rv.members.push_back(v);
        }
        rv.members.push_back(s);
        return rv;
    }
}

void HIR::SimplePath::operator+=(const RcString& s) {
    if (members.empty()) {
        members = ThinVector<RcString>({RcString(), s});
    } else {
        members.push_back(s);
    }
}

RcString HIR::SimplePath::popComponent() {
    if (members.size() <= 1) {
        return RcString();
    } else {
        auto rv = members.back();
        members.pop_back();
        if (members.size() == 1 && members[0] == RcString()) {
            members = ThinVector<RcString>();
        }
        return rv;
    }
}

void HIR::SimplePath::updateCrateName(RcString v) {
    if (members.empty()) {
        members.push_back(v);
    } else if (v.c_str()[0] == '\0' && members.size() == 1) {
        members = ThinVector<RcString>();
    } else {
        members[0] = std::move(v);
    }
}

void HIR::SimplePath::updateLastComponent(RcString v) {
    assert(members.size() >= 2);
    members.back() = std::move(v);
}

bool HIR::SimplePath::startsWith(const HIR::SimplePath& p, bool skipLast /*=false*/) const {
    if (p.members.empty()) {
        return crateName() == RcString();
    }
    // This path can't start with `p` if it's shorter than `p`
    if (members.size() < p.members.size() - (skipLast ? 1 : 0)) {
        return false;
    }
    for (size_t i = 0; i < p.members.size() - (skipLast ? 1 : 0); i++) {
        if (p.members[i] != this->members[i]) {
            return false;
        }
    }
    return true;
}

::HIR::PathParams::PathParams() {
}

::HIR::PathParams::PathParams(::HIR::TypeRef ty0) {
    types = ThinVector<HIR::TypeRef>(1);
    types[0] = std::move(ty0);
}

HIR::PathParams::PathParams(::HIR::LifetimeRef lft) {
    mLifetimes = ThinVector<HIR::LifetimeRef>(1);
    mLifetimes[0] = std::move(lft);
}

::HIR::PathParams HIR::PathParams::clone() const {
    PathParams rv;
    rv.mLifetimes = this->mLifetimes;
    rv.types.reserve(types.size());
    for (const auto& t : types) {
        rv.types.push_back(t);
    }
    rv.values.reserve(values.size());
    for (const auto& t : values) {
        rv.values.push_back(t.clone());
    }
    return rv;
}

::HIR::GenericPath::GenericPath() {
}

::HIR::GenericPath::GenericPath(::HIR::SimplePath sp)
    : mPath(mv$(sp))
{
}

::HIR::GenericPath::GenericPath(::HIR::SimplePath sp, ::HIR::PathParams params)
    : mPath(mv$(sp))
    , mParams(mv$(params))
{
}

::HIR::GenericPath::GenericPath(::HIR::GenericParams hrls, ::HIR::SimplePath sp, ::HIR::PathParams params)
    : mPath(mv$(sp))
    , mParams(mv$(params))
{
}

::HIR::GenericPath HIR::GenericPath::clone() const {
    return GenericPath(mPath.clone(), mParams.clone());
}

bool HIR::PathParams::equalsIgnoringRegions(const HIR::PathParams& x) const {
    if (types.size() != x.types.size() || values.size() != x.values.size()) {
        return false;
    }
    for (size_t i = 0; i < types.size(); i++) {
        if (types[i] != x.types[i] && !types[i]->equalsIgnoringRegions(x.types[i])) {
            return false;
        }
    }
    for (size_t i = 0; i < values.size(); i++) {
        if (values[i] != x.values[i]) {
            return false;
        }
    }
    return true;
}

bool HIR::GenericPath::equalsIgnoringRegions(const HIR::GenericPath& x) const {
    return mPath == x.mPath && mParams.equalsIgnoringRegions(x.mParams);
}

Ordering HIR::GenericPath::ord(const HIR::GenericPath& x) const {
    ORD(mPath, x.mPath);
    //DEBUG("\n  " << *this << "\n  " << x);
    ORD(mParams, x.mParams);

    return OrdEqual;
}

::HIR::TraitPath HIR::TraitPath::clone() const {
    ::HIR::TraitPath rv{hrtbs ? box$(hrtbs->clone()) : nullptr, mPath.clone(), {}, {}, traitPtr, constness};
    rv.lifetimeElision = lifetimeElision;

    for (const auto& assoc : typeBounds) {
        rv.typeBounds.insert(::std::make_pair(assoc.first, assoc.second.clone()));
    }
    for (const auto& assoc : traitBounds) {
        rv.traitBounds.insert(::std::make_pair(assoc.first, assoc.second.clone()));
    }

    return rv;
}

bool HIR::TraitPath::equalsIgnoringRegions(const TraitPath& x) const {
    if (!mPath.equalsIgnoringRegions(x.mPath)
        || typeBounds.size() != x.typeBounds.size()
        || traitBounds.size() != x.traitBounds.size()) {
        return false;
    }

    auto lhsType = typeBounds.begin();
    auto rhsType = x.typeBounds.begin();
    for (; lhsType != typeBounds.end(); ++lhsType, ++rhsType) {
        const auto& lhs = lhsType->second;
        const auto& rhs = rhsType->second;
        if (lhsType->first != rhsType->first
            || !lhs.sourceTrait.equalsIgnoringRegions(rhs.sourceTrait)
            || !lhs.atyParams.equalsIgnoringRegions(rhs.atyParams)
            || (lhs.type != rhs.type && !lhs.type->equalsIgnoringRegions(rhs.type))) {
            return false;
        }
    }

    auto lhsBound = traitBounds.begin();
    auto rhsBound = x.traitBounds.begin();
    for (; lhsBound != traitBounds.end(); ++lhsBound, ++rhsBound) {
        const auto& lhs = lhsBound->second;
        const auto& rhs = rhsBound->second;
        if (lhsBound->first != rhsBound->first
            || !lhs.sourceTrait.equalsIgnoringRegions(rhs.sourceTrait)
            || !lhs.atyParams.equalsIgnoringRegions(rhs.atyParams)
            || lhs.traits.size() != rhs.traits.size()) {
            return false;
        }
        for (size_t i = 0; i < lhs.traits.size(); i++) {
            if (!lhs.traits[i].equalsIgnoringRegions(rhs.traits[i])) {
                return false;
            }
        }
    }
    return true;
}

Ordering HIR::TraitPath::ord(const TraitPath& x) const {
    // NOTE: An empty set is treated as the same as none
    if (gCompareHrls) {
        ORD(hrtbs.get() && !hrtbs->isEmpty(), x.hrtbs.get() && !x.hrtbs->isEmpty());
        if (hrtbs && x.hrtbs) {
            ORD(hrtbs->mLifetimes.size(), x.hrtbs->mLifetimes.size());
            ORD(hrtbs->bounds, x.hrtbs->bounds);
        }
    }

    ORD(mPath, x.mPath);
    ORD(traitBounds, x.traitBounds);
    ORD(typeBounds, x.typeBounds);
    return OrdEqual;
}

::HIR::Path::Path(::HIR::GenericPath gp)
    : mData(::HIR::Path::Data::make_Generic(mv$(gp)))
{
}

::HIR::Path::Path(::HIR::SimplePath sp)
    : mData(::HIR::Path::Data::make_Generic(::HIR::GenericPath(mv$(sp))))
{
}

::HIR::Path::Path(TypeRef ty, RcString item, PathParams itemParams)
    : mData(Data::make_UfcsInherent({mv$(ty), mv$(item), mv$(itemParams)}))
{
}

::HIR::Path::Path(TypeRef ty, GenericPath trait, RcString item, PathParams itemParams)
    : mData(Data::make_UfcsKnown({mv$(ty), mv$(trait), mv$(item), mv$(itemParams)}))
{
}

::HIR::Path::Path(TypeRef ty, GenericParams hrtbs, GenericPath trait, RcString item, PathParams itemParams)
    : mData(Data::make_UfcsKnown({mv$(ty), mv$(trait), mv$(item), mv$(itemParams), box$(hrtbs)}))
{
}

::HIR::Path HIR::Path::clone() const {
    TU_MATCH_HDRA((mData), {)
    TU_ARMA(Generic, e) {
            return Path(Data::make_Generic(e.clone()));
        }
        TU_ARMA(UfcsInherent, e) {
            return Path(Data::make_UfcsInherent({e.type, e.item, e.params.clone(), e.implParams.clone()}));
        }
        TU_ARMA(UfcsKnown, e) {
            return Path(
                Data::make_UfcsKnown({
                    e.type,
                    e.trait.clone(),
                    e.item,
                    e.params.clone(),
                    e.hrtbs ? box$(e.hrtbs->clone()) : nullptr,
                })
            );
        }
        TU_ARMA(UfcsUnknown, e) {
            return Path(Data::make_UfcsUnknown({e.type, e.item, e.params.clone()}));
        }
    }
    throw "";
}

::HIR::Compare HIR::PathParams::compareWithPlaceholders(const Span& sp, const ::HIR::PathParams& x, ::HIR::tCbResolveType resolvePlaceholder) const {
    using ::HIR::Compare;

    auto rv = Compare::Equal;
    if (this->types.size() > 0 || x.types.size() > 0) {
        if (this->types.size() != x.types.size()) {
            return Compare::Unequal;
        }
        for (unsigned int i = 0; i < x.types.size(); i++) {
            auto rv2 = this->types[i]->compareWithPlaceholders(sp, x.types[i], resolvePlaceholder);
            if (rv2 == Compare::Unequal) {
                return Compare::Unequal;
            }
            if (rv2 == Compare::Fuzzy) {
                rv = Compare::Fuzzy;
            }
        }
    }
#if 1
    if (this->values.size() > 0 || x.values.size() > 0) {
        if (this->values.size() != x.values.size()) {
            return Compare::Unequal;
        }
        for (unsigned int i = 0; i < x.values.size(); i++) {
            const auto& valT = resolvePlaceholder.getVal(sp, this->values[i]);
            const auto& valX = resolvePlaceholder.getVal(sp, x.values[i]);
            /*if( const auto* ge = val_t.opt_Generic() ) {
                rv &= match.match_val(*ge, val_x);
                if(rv == Compare::Unequal)
                    return Compare::Unequal;
            }
            else*/
            {
                // TODO: Look up the the ivars?
                if (valT.is_Infer() || valX.is_Infer()) {
                    //return Compare::Fuzzy;
                    rv = Compare::Fuzzy;
                } else if (valT != valX) {
                    if (valT.is_Unevaluated() || valX.is_Unevaluated()) {
                        //return Compare::Fuzzy;
                        rv = Compare::Fuzzy;
                    } else {
                        return Compare::Unequal;
                    }
                }
            }
        }
    }
#endif
    return rv;
}

::HIR::Compare HIR::PathParams::matchTestGenericsFuzz(const Span& sp, const PathParams& x, tCbResolveType resolvePlaceholder, ::HIR::MatchGenerics& match) const {
    using ::HIR::Compare;
    auto rv = Compare::Equal;
    TRACE_FUNCTION_F("(PathParams) " << *this << " with " << x);

    if (this->types.size() != x.types.size()) {
        return Compare::Unequal;
    }
    for (unsigned int i = 0; i < x.types.size(); i++) {
        rv &= this->types[i]->matchTestGenericsFuzz(sp, x.types[i], resolvePlaceholder, match);
        if (rv == Compare::Unequal) {
            return Compare::Unequal;
        }
    }

    if (this->values.size() != x.values.size()) {
        return Compare::Unequal;
    }
    for (unsigned int i = 0; i < x.values.size(); i++) {
        const auto& valT = resolvePlaceholder.getVal(sp, this->values[i]);
        const auto& valX = resolvePlaceholder.getVal(sp, x.values[i]);
        if (const auto* ge = valT.opt_Generic()) {
            rv &= match.matchVal(*ge, valX);
            if (rv == Compare::Unequal) {
                return Compare::Unequal;
            }
        } else {
            // TODO: Look up the the ivars?
            if (valT.is_Infer() || valX.is_Infer()) {
                return Compare::Fuzzy;
            }

            // An unevaluated value that is a plain integer literal can still be compared
            // exactly; treating it as fuzzy made impl selection on const generics pick the
            // first candidate (harfrust's `SelectAtomic<8/16/32>`).
            struct H2 {
                static bool getLiteral(const ::HIR::ConstGeneric& v, U128& out) {
                    if (const auto* ev = v.opt_Evaluated()) {
                        auto sl = EncodedLiteralSlice(**ev);
                        if (sl.mSize == 0 || sl.mSize > 16) {
                            return false;
                        }
                        out = sl.readUint(sl.mSize);
                        return true;
                    }
                    if (const auto* uev = v.opt_Unevaluated()) {
                        if (!(*uev)->expr || !*(*uev)->expr) {
                            return false;
                        }
                        const auto& node = **(*uev)->expr;
                        if (const auto* lit = cast<const ::HIR::ExprNodeLiteral>(&node)) {
                            if (const auto* i = lit->mData.opt_Integer()) {
                                out = i->mValue;
                                return true;
                            }
                        }
                        return false;
                    }
                    return false;
                }
            };

            U128 litT, litX;
            if (H2::getLiteral(valT, litT) && H2::getLiteral(valX, litX)) {
                if (litT != litX) {
                    return Compare::Unequal;
                }
                // Equal literals: continue (leaves `rv` as-is)
            } else if (valT != valX) {
                if (valT.is_Unevaluated() || valX.is_Unevaluated()) {
                    return Compare::Fuzzy;
                }
                return Compare::Unequal;
            }
        }
    }

#if 1
    if (this->mLifetimes.size() != x.mLifetimes.size()) {
        //return Compare::Unequal;
    }
    for (unsigned int i = 0; i < std::min(this->mLifetimes.size(), x.mLifetimes.size()); i++) {
        if (this->mLifetimes[i].isParam()) {
            /*rv &=*/match.matchLft(this->mLifetimes[i].asParam(), x.mLifetimes[i]);
            //if(rv == Compare::Unequal)
            //    return Compare::Unequal;
        } else {
            //if( this->m_lifetimes[i] != x.m_lifetimes[i] ) {
            //    return Compare::Unequal;
            //}
        }
    }
#endif

    return rv;
}

::HIR::Compare HIR::GenericPath::compareWithPlaceholders(const Span& sp, const ::HIR::GenericPath& x, ::HIR::tCbResolveType resolvePlaceholder) const {
    if (this->mPath != x.mPath) {
        return ::HIR::Compare::Unequal;
    }

    return this->mParams.compareWithPlaceholders(sp, x.mParams, resolvePlaceholder);
}

namespace {
    ::HIR::Compare compareWithPlaceholders(const Span& sp, const ::HIR::PathParams& l, const ::HIR::PathParams& r, ::HIR::tCbResolveType resolvePlaceholder) {
        return l.compareWithPlaceholders(sp, r, resolvePlaceholder);
    }

    ::HIR::Compare compareWithPlaceholders(const Span& sp, const ::HIR::GenericPath& l, const ::HIR::GenericPath& r, ::HIR::tCbResolveType resolvePlaceholder) {
        return l.compareWithPlaceholders(sp, r, resolvePlaceholder);
    }
}

#define CMP(rv, cmp)                            \
    do {                                        \
        switch (cmp) {                          \
            case ::HIR::Compare::Unequal:       \
                return ::HIR::Compare::Unequal; \
            case ::HIR::Compare::Fuzzy:         \
                rv = ::HIR::Compare::Fuzzy;     \
                break;                          \
            case ::HIR::Compare::Equal:         \
                break;                          \
        }                                       \
    } while (0)

::HIR::Compare HIR::TraitPath::compareWithPlaceholders(const Span& sp, const TraitPath& x, tCbResolveType resolvePlaceholder) const {
    auto rv = mPath.compareWithPlaceholders(sp, x.mPath, resolvePlaceholder);
    if (rv == Compare::Unequal) {
        return rv;
    }

    // TODO: HRLs

#if 1
    if (gCompareHrls) {
        if ((this->hrtbs && !this->hrtbs->isEmpty()) != (x.hrtbs && !x.hrtbs->isEmpty())) {
            return Compare::Unequal;
        }
        if (this->hrtbs && x.hrtbs) {
            if (this->hrtbs->mLifetimes.size() != x.hrtbs->mLifetimes.size()) {
                return Compare::Unequal;
            }
        }
    }
#endif

    auto itL = typeBounds.begin();
    auto itR = x.typeBounds.begin();
    while (itL != typeBounds.end() && itR != x.typeBounds.end()) {
        if (itL->first != itR->first) {
            return Compare::Unequal;
        }
        CMP(rv, itL->second.type->compareWithPlaceholders(sp, itR->second.type, resolvePlaceholder));
        ++itL;
        ++itR;
    }

    if (itL != typeBounds.end() || itR != x.typeBounds.end()) {
        return Compare::Unequal;
    }

    return rv;
}

::HIR::Compare HIR::Path::compareWithPlaceholders(const Span& sp, const Path& x, tCbResolveType resolvePlaceholder) const {
    if (this->mData.tag() != x.mData.tag()) {
        return Compare::Unequal;
    }
    TU_MATCH_HDRA( (this->mData, x.mData), {)
    TU_ARMA(Generic, ple, pre) {
            return ::compareWithPlaceholders(sp, ple, pre, resolvePlaceholder);
        }
        TU_ARMA(UfcsUnknown, ple, pre) {
            if (ple.item != pre.item) {
                return Compare::Unequal;
            }

            TODO(sp, "Path::compare_with_placeholders - UfcsUnknown");
        }
        TU_ARMA(UfcsInherent, ple, pre) {
            if (ple.item != pre.item) {
                return Compare::Unequal;
            }
            ::HIR::Compare rv = ::HIR::Compare::Equal;
            CMP(rv, ple.type->compareWithPlaceholders(sp, pre.type, resolvePlaceholder));
            CMP(rv, ::compareWithPlaceholders(sp, ple.params, pre.params, resolvePlaceholder));
            return rv;
        }
        TU_ARMA(UfcsKnown, ple, pre) {
            if (ple.item != pre.item) {
                return Compare::Unequal;
            }

            ::HIR::Compare rv = ::HIR::Compare::Equal;
            CMP(rv, ple.type->compareWithPlaceholders(sp, pre.type, resolvePlaceholder));
            CMP(rv, ::compareWithPlaceholders(sp, ple.trait, pre.trait, resolvePlaceholder));
            CMP(rv, ::compareWithPlaceholders(sp, ple.params, pre.params, resolvePlaceholder));
            return rv;
        }
    }
    throw "";
}

Ordering HIR::Path::ord(const ::HIR::Path& x) const {
    ORD((unsigned)mData.tag(), (unsigned)x.mData.tag());
    TU_MATCH(::HIR::Path::Data, (this->mData, x.mData), (tpe, xpe), (Generic, return ::ord(tpe, xpe);), (UfcsInherent, ORD(tpe.type, xpe.type); ORD(tpe.item, xpe.item); return ::ord(tpe.params, xpe.params);), (UfcsKnown, ORD(tpe.type, xpe.type); ORD(tpe.trait, xpe.trait); ORD(tpe.item, xpe.item); return ::ord(tpe.params, xpe.params);), (UfcsUnknown, ORD(tpe.type, xpe.type); ORD(tpe.item, xpe.item); return ::ord(tpe.params, xpe.params);))
    throw "";
}

bool HIR::Path::equalsIgnoringRegions(const Path& x) const {
    if (mData.tag() != x.mData.tag()) {
        return false;
    }
    TU_MATCH_HDRA((mData, x.mData), {)
    TU_ARMA(Generic, lhs, rhs) {
        return lhs.equalsIgnoringRegions(rhs);
    }
    TU_ARMA(UfcsInherent, lhs, rhs) {
        return lhs.item == rhs.item
            && (lhs.type == rhs.type || lhs.type->equalsIgnoringRegions(rhs.type))
            && lhs.params.equalsIgnoringRegions(rhs.params)
            && lhs.implParams.equalsIgnoringRegions(rhs.implParams);
    }
    TU_ARMA(UfcsKnown, lhs, rhs) {
        return lhs.item == rhs.item
            && (lhs.type == rhs.type || lhs.type->equalsIgnoringRegions(rhs.type))
            && lhs.trait.equalsIgnoringRegions(rhs.trait)
            && lhs.params.equalsIgnoringRegions(rhs.params);
    }
    TU_ARMA(UfcsUnknown, lhs, rhs) {
        return lhs.item == rhs.item
            && (lhs.type == rhs.type || lhs.type->equalsIgnoringRegions(rhs.type))
            && lhs.params.equalsIgnoringRegions(rhs.params);
    }
    }
    throw "";
}

bool ::HIR::Path::operator==(const Path & x) const {
    return this->ord(x) == ::OrdEqual;
}

namespace HIR {

EncodedLiteralPtr::EncodedLiteralPtr()
    : p(nullptr) {
}
EncodedLiteralPtr::EncodedLiteralPtr(EncodedLiteralPtr&& x)
    : p(x.p) {
    x.p = nullptr;
}
EncodedLiteralPtr& EncodedLiteralPtr::operator=(EncodedLiteralPtr&& x) {
    this->~EncodedLiteralPtr();
    this->p = x.p;
    x.p = nullptr;
    return *this;
}
EncodedLiteral& EncodedLiteralPtr::operator*() {
    assert(p);
    return *p;
}
const EncodedLiteral& EncodedLiteralPtr::operator*() const {
    assert(p);
    return *p;
}
EncodedLiteral* EncodedLiteralPtr::operator->() {
    assert(p);
    return p;
}
const EncodedLiteral* EncodedLiteralPtr::operator->() const {
    assert(p);
    return p;
}
SimplePath::SimplePath(ThinVector<RcString> members)
    : members(std::move(members)) {
}
SimplePath::SimplePath() {
}
SimplePath::SimplePath(RcString crate)
    : SimplePath(crate, ::std::span<RcString>()) {
}
SimplePath::SimplePath(RcString crate, ::std::vector<RcString> components)
    : SimplePath(crate, ::std::span<RcString>(components)) {
}
SimplePath::SimplePath(RcString crate, ::std::span<RcString> components) {
    // NOTE: Ensure that it's impossible for the crate name to be empty with only one value in `m_members`, simplifies comparison logic
    if (crate.c_str()[0] != '\0' || !components.empty()) {
        members.reserve(1 + components.size());
        members.push_back(std::move(crate));
        for (auto& n : components) {
            members.push_back(std::move(n));
        }
    }
}
SimplePath::SimplePath(RcString crate, ::std::span<const RcString> components) {
    if (crate.c_str()[0] != '\0' || !components.empty()) {
        members.reserve(1 + components.size());
        members.push_back(std::move(crate));
        for (const auto& n : components) {
            members.push_back(n);
        }
    }
}
SimplePath::SimplePath(RcString crate, ::std::initializer_list<RcString> components)
    : SimplePath(std::move(crate), ::std::span<const RcString>(components.begin(), components.end())) {
}
const RcString& SimplePath::crateName() const {
    static RcString empty;
    return members.empty() ? empty : members.front();
}
::std::vector<RcString> SimplePath::componentsVec() const {
    const auto values = components();
    return {values.begin(), values.end()};
}
Ordering PathParams::ord(const PathParams& x) const {
    //if(auto cmp = ::ord(m_lifetimes, x.m_lifetimes)) return cmp;
    if (auto cmp = ::ord(types, x.types)) {
        return cmp;
    }
    if (auto cmp = ::ord(values, x.values)) {
        return cmp;
    }
    return OrdEqual;
}
Ordering TraitPath::AtyEqual::ord(const AtyEqual& x) const {
    ORD(sourceTrait, x.sourceTrait);
    ORD(atyParams, x.atyParams);
    ORD(type, x.type);
    return OrdEqual;
}
Ordering TraitPath::AtyBound::ord(const AtyBound& x) const {
    ORD(sourceTrait, x.sourceTrait);
    ORD(atyParams, x.atyParams);
    ORD(traits, x.traits);
    return OrdEqual;
}
TraitPath::AtyBound TraitPath::AtyBound::clone() const {
    std::vector<::HIR::TraitPath> newTraits;
    newTraits.reserve(traits.size());
    for (const auto& t : traits) {
        newTraits.push_back(t.clone());
    }
    return AtyBound{sourceTrait.clone(), atyParams.clone(), ::std::move(newTraits)};
}
Path::Path(Data data)
    : mData(mv$(data)) {
}
ConstGenericUnevaluated::ConstGenericUnevaluated() {
}
}

namespace HIR {

::std::ostream& operator<<(::std::ostream& os, const Compare& x) {
    switch (x) {
        case Compare::Equal:
            os << "Equal";
            break;
        case Compare::Fuzzy:
            os << "Fuzzy";
            break;
        case Compare::Unequal:
            os << "Unequal";
            break;
    }
    return os;
}
Compare& operator&=(Compare& x, const Compare& y) {
    if (x == Compare::Unequal) {
    } else if (y == Compare::Unequal) {
        x = Compare::Unequal;
    } else if (y == Compare::Fuzzy) {
        x = Compare::Fuzzy;
    } else {
        // keep as-is
    }
    return x;
}
::std::ostream& operator<<(::std::ostream& os, const TraitPath::AtyEqual& x) {
    os << x.type;
    return os;
}
}
