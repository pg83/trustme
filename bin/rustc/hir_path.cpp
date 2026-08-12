#include "hir_path.h"

#include "hir_expr.h"
#include "hir_type.h"

#include <algorithm>

HIRTraitPath::HIRTraitPath()
    : traitPtr(nullptr)
{
}

HIRTraitPath::HIRTraitPath(HIRGenericPath path)
    : mPath(::std::move(path))
    , traitPtr(nullptr)
{
}

HIRTraitPath::HIRTraitPath(HIRGenericPath path, assocListT typeBounds, ::std::map<RcString, AtyBound> traitBounds, const HIRTrait* traitPtr, HIRBoundConstness constness)
    : mPath(::std::move(path))
    , typeBounds(::std::move(typeBounds))
    , traitBounds(::std::move(traitBounds))
    , constness(constness)
    , traitPtr(traitPtr)
{
}

HIRTraitPath::~HIRTraitPath() = default;
HIRTraitPath::HIRTraitPath(HIRTraitPath&&) = default;
HIRTraitPath& HIRTraitPath::operator=(HIRTraitPath&&) = default;

::std::ostream& operator<<(::std::ostream& os, const HIRSimplePath& x) {
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

::std::ostream& operator<<(::std::ostream& os, const HIRPathParams& x) {
    bool hasArgs = (x.types.size() > 0 || x.values.size() > 0);

    if (hasArgs) {
        os << "<";
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

::std::ostream& operator<<(::std::ostream& os, const HIRGenericPath& x) {
    os << x.mPath << x.mParams;
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const HIRTraitPath& x) {
    if (x.constness == HIRBoundConstness::Always) {
        os << "const ";
    } else if (x.constness == HIRBoundConstness::Maybe) {
        os << "[const] ";
    }
    os << x.mPath.mPath;
    bool hasArgs = (x.mPath.mParams.types.size() > 0 || x.typeBounds.size() > 0 || x.traitBounds.size() > 0);

    if (hasArgs) {
        os << "<";
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

::std::ostream& operator<<(::std::ostream& os, const HIRPath& x) {
    TU_MATCH(HIRPath::Data, (x.mData), (e), (Generic, return os << e;), (UfcsInherent, return os << "<" << e.type << " /*- " << e.implParams << "*/>::" << e.item << e.params;), (UfcsKnown, os << "<" << e.type << " as "; os << e.trait << ">::" << e.item << e.params; return os;), (UfcsUnknown, return os << "<" << e.type << " as _>::" << e.item << e.params;))
    return os;
}

HIRSimplePath HIRSimplePath::clone() const {
    return HIRSimplePath(members);
}

HIRSimplePath HIRSimplePath::parent() const {
    if (members.size() > 1) {
        return HIRSimplePath(ThinVector<RcString>(members.begin(), members.end() - 1));
    } else {
        return this->clone();
    }
}

HIRSimplePath HIRSimplePath::operator+(const RcString& s) const {
    if (members.empty()) {
        return ThinVector<RcString>({RcString(), s});
    } else {
        HIRSimplePath rv;
        rv.members.reserve(members.size());
        for (const auto& v : members) {
            rv.members.push_back(v);
        }
        rv.members.push_back(s);
        return rv;
    }
}

void HIRSimplePath::operator+=(const RcString& s) {
    if (members.empty()) {
        members = ThinVector<RcString>({RcString(), s});
    } else {
        members.push_back(s);
    }
}

RcString HIRSimplePath::popComponent() {
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

void HIRSimplePath::updateCrateName(RcString v) {
    if (members.empty()) {
        members.push_back(v);
    } else if (v.c_str()[0] == '\0' && members.size() == 1) {
        members = ThinVector<RcString>();
    } else {
        members[0] = std::move(v);
    }
}

void HIRSimplePath::updateLastComponent(RcString v) {
    assert(members.size() >= 2);
    members.back() = std::move(v);
}

bool HIRSimplePath::startsWith(const HIRSimplePath& p, bool skipLast /*=false*/) const {
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

HIRPathParams::HIRPathParams() {
}

HIRPathParams::HIRPathParams(HIRTypeRef ty0) {
    types = ThinVector<HIRTypeRef>(1);
    types[0] = std::move(ty0);
}

HIRPathParams HIRPathParams::clone() const {
    HIRPathParams rv;
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

HIRGenericPath::HIRGenericPath() {
}

HIRGenericPath::HIRGenericPath(HIRSimplePath sp)
    : mPath(mv$(sp))
{
}

HIRGenericPath::HIRGenericPath(HIRSimplePath sp, HIRPathParams params)
    : mPath(mv$(sp))
    , mParams(mv$(params))
{
}

HIRGenericPath HIRGenericPath::clone() const {
    return HIRGenericPath(mPath.clone(), mParams.clone());
}

bool HIRPathParams::equalsIgnoringRegions(const HIRPathParams& x) const {
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

bool HIRGenericPath::equalsIgnoringRegions(const HIRGenericPath& x) const {
    return mPath == x.mPath && mParams.equalsIgnoringRegions(x.mParams);
}

Ordering HIRGenericPath::ord(const HIRGenericPath& x) const {
    ORD(mPath, x.mPath);
    ORD(mParams, x.mParams);

    return OrdEqual;
}

HIRTraitPath HIRTraitPath::clone() const {
    HIRTraitPath rv{mPath.clone(), {}, {}, traitPtr, constness};

    for (const auto& assoc : typeBounds) {
        rv.typeBounds.insert(::std::make_pair(assoc.first, assoc.second.clone()));
    }
    for (const auto& assoc : traitBounds) {
        rv.traitBounds.insert(::std::make_pair(assoc.first, assoc.second.clone()));
    }

    return rv;
}

bool HIRTraitPath::equalsIgnoringRegions(const HIRTraitPath& x) const {
    if (!mPath.equalsIgnoringRegions(x.mPath) || typeBounds.size() != x.typeBounds.size() || traitBounds.size() != x.traitBounds.size()) {
        return false;
    }

    auto lhsType = typeBounds.begin();
    auto rhsType = x.typeBounds.begin();
    for (; lhsType != typeBounds.end(); ++lhsType, ++rhsType) {
        const auto& lhs = lhsType->second;
        const auto& rhs = rhsType->second;
        if (lhsType->first != rhsType->first || !lhs.sourceTrait.equalsIgnoringRegions(rhs.sourceTrait) || !lhs.atyParams.equalsIgnoringRegions(rhs.atyParams) || (lhs.type != rhs.type && !lhs.type->equalsIgnoringRegions(rhs.type))) {
            return false;
        }
    }

    auto lhsBound = traitBounds.begin();
    auto rhsBound = x.traitBounds.begin();
    for (; lhsBound != traitBounds.end(); ++lhsBound, ++rhsBound) {
        const auto& lhs = lhsBound->second;
        const auto& rhs = rhsBound->second;
        if (lhsBound->first != rhsBound->first || !lhs.sourceTrait.equalsIgnoringRegions(rhs.sourceTrait) || !lhs.atyParams.equalsIgnoringRegions(rhs.atyParams) || lhs.traits.size() != rhs.traits.size()) {
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

Ordering HIRTraitPath::ord(const HIRTraitPath& x) const {
    // NOTE: An empty set is treated as the same as none
    ORD(mPath, x.mPath);
    ORD(traitBounds, x.traitBounds);
    ORD(typeBounds, x.typeBounds);
    return OrdEqual;
}

HIRPath::HIRPath(HIRGenericPath gp)
    : mData(HIRPath::Data::make_Generic(mv$(gp)))
{
}

HIRPath::HIRPath(HIRSimplePath sp)
    : mData(HIRPath::Data::make_Generic(HIRGenericPath(mv$(sp))))
{
}

HIRPath::HIRPath(HIRTypeRef ty, RcString item, HIRPathParams itemParams)
    : mData(Data::make_UfcsInherent({mv$(ty), mv$(item), mv$(itemParams)}))
{
}

HIRPath::HIRPath(HIRTypeRef ty, HIRGenericPath trait, RcString item, HIRPathParams itemParams)
    : mData(Data::make_UfcsKnown({mv$(ty), mv$(trait), mv$(item), mv$(itemParams)}))
{
}

HIRPath HIRPath::clone() const {
    TU_MATCH_HDRA((mData), {)
    TU_ARMA(Generic, e) {
            return HIRPath(Data::make_Generic(e.clone()));
        }
        TU_ARMA(UfcsInherent, e) {
            return HIRPath(Data::make_UfcsInherent({e.type, e.item, e.params.clone(), e.implParams.clone()}));
        }
        TU_ARMA(UfcsKnown, e) {
            return HIRPath(
                Data::make_UfcsKnown({
                    e.type,
                    e.trait.clone(),
                    e.item,
                    e.params.clone(),
                })
            );
        }
        TU_ARMA(UfcsUnknown, e) {
            return HIRPath(Data::make_UfcsUnknown({e.type, e.item, e.params.clone()}));
        }
    }
    throw "";
}

HIRCompare HIRPathParams::compareWithPlaceholders(const Span& sp, const HIRPathParams& x, tCbResolveType resolvePlaceholder) const {
    auto rv = HIRCompare::Equal;
    if (this->types.size() > 0 || x.types.size() > 0) {
        if (this->types.size() != x.types.size()) {
            return HIRCompare::Unequal;
        }
        for (unsigned int i = 0; i < x.types.size(); i++) {
            auto rv2 = this->types[i]->compareWithPlaceholders(sp, x.types[i], resolvePlaceholder);
            if (rv2 == HIRCompare::Unequal) {
                return HIRCompare::Unequal;
            }
            if (rv2 == HIRCompare::Fuzzy) {
                rv = HIRCompare::Fuzzy;
            }
        }
    }
    if (this->values.size() > 0 || x.values.size() > 0) {
        if (this->values.size() != x.values.size()) {
            return HIRCompare::Unequal;
        }
        for (unsigned int i = 0; i < x.values.size(); i++) {
            const auto& valT = resolvePlaceholder.getVal(sp, this->values[i]);
            const auto& valX = resolvePlaceholder.getVal(sp, x.values[i]);

            {
                // TODO: Look up the the ivars?
                if (valT.is_Infer() || valX.is_Infer()) {
                    rv = HIRCompare::Fuzzy;
                } else if (valT != valX) {
                    if (valT.is_Unevaluated() || valX.is_Unevaluated()) {
                        rv = HIRCompare::Fuzzy;
                    } else {
                        return HIRCompare::Unequal;
                    }
                }
            }
        }
    }
    return rv;
}

HIRCompare HIRPathParams::matchTestGenericsFuzz(const Span& sp, const HIRPathParams& x, tCbResolveType resolvePlaceholder, HIRMatchGenerics& match) const {
    auto rv = HIRCompare::Equal;
    TRACE_FUNCTION_F("(PathParams) " << *this << " with " << x);

    if (this->types.size() != x.types.size()) {
        return HIRCompare::Unequal;
    }
    for (unsigned int i = 0; i < x.types.size(); i++) {
        rv &= this->types[i]->matchTestGenericsFuzz(sp, x.types[i], resolvePlaceholder, match);
        if (rv == HIRCompare::Unequal) {
            return HIRCompare::Unequal;
        }
    }

    if (this->values.size() != x.values.size()) {
        return HIRCompare::Unequal;
    }
    for (unsigned int i = 0; i < x.values.size(); i++) {
        const auto& valT = resolvePlaceholder.getVal(sp, this->values[i]);
        const auto& valX = resolvePlaceholder.getVal(sp, x.values[i]);
        if (const auto* ge = valT.opt_Generic()) {
            rv &= match.matchVal(*ge, valX);
            if (rv == HIRCompare::Unequal) {
                return HIRCompare::Unequal;
            }
        } else {
            // TODO: Look up the the ivars?
            if (valT.is_Infer() || valX.is_Infer()) {
                return HIRCompare::Fuzzy;
            }

            // An unevaluated value that is a plain integer literal can still be compared
            // exactly; treating it as fuzzy made impl selection on const generics pick the
            // first candidate (harfrust's `SelectAtomic<8/16/32>`).
            struct H2 {
                static bool getLiteral(const HIRConstGeneric& v, U128& out) {
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
                        if (const auto* lit = cast<const HIRExprNodeLiteral>(&node)) {
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
                    return HIRCompare::Unequal;
                }
                // Equal literals: continue (leaves `rv` as-is)
            } else if (valT != valX) {
                if (valT.is_Unevaluated() || valX.is_Unevaluated()) {
                    return HIRCompare::Fuzzy;
                }
                return HIRCompare::Unequal;
            }
        }
    }

    return rv;
}

HIRCompare HIRGenericPath::compareWithPlaceholders(const Span& sp, const HIRGenericPath& x, tCbResolveType resolvePlaceholder) const {
    if (this->mPath != x.mPath) {
        return HIRCompare::Unequal;
    }

    return this->mParams.compareWithPlaceholders(sp, x.mParams, resolvePlaceholder);
}

namespace {
    HIRCompare compareWithPlaceholders(const Span& sp, const HIRPathParams& l, const HIRPathParams& r, tCbResolveType resolvePlaceholder) {
        return l.compareWithPlaceholders(sp, r, resolvePlaceholder);
    }

    HIRCompare compareWithPlaceholders(const Span& sp, const HIRGenericPath& l, const HIRGenericPath& r, tCbResolveType resolvePlaceholder) {
        return l.compareWithPlaceholders(sp, r, resolvePlaceholder);
    }
}

#define CMP(rv, cmp)                        \
    do {                                    \
        switch (cmp) {                      \
            case HIRCompare::Unequal:       \
                return HIRCompare::Unequal; \
            case HIRCompare::Fuzzy:         \
                rv = HIRCompare::Fuzzy;     \
                break;                      \
            case HIRCompare::Equal:         \
                break;                      \
        }                                   \
    } while (0)

HIRCompare HIRTraitPath::compareWithPlaceholders(const Span& sp, const HIRTraitPath& x, tCbResolveType resolvePlaceholder) const {
    auto rv = mPath.compareWithPlaceholders(sp, x.mPath, resolvePlaceholder);
    if (rv == HIRCompare::Unequal) {
        return rv;
    }

    auto itL = typeBounds.begin();
    auto itR = x.typeBounds.begin();
    while (itL != typeBounds.end() && itR != x.typeBounds.end()) {
        if (itL->first != itR->first) {
            return HIRCompare::Unequal;
        }
        CMP(rv, itL->second.type->compareWithPlaceholders(sp, itR->second.type, resolvePlaceholder));
        ++itL;
        ++itR;
    }

    if (itL != typeBounds.end() || itR != x.typeBounds.end()) {
        return HIRCompare::Unequal;
    }

    return rv;
}

HIRCompare HIRPath::compareWithPlaceholders(const Span& sp, const HIRPath& x, tCbResolveType resolvePlaceholder) const {
    if (this->mData.tag() != x.mData.tag()) {
        return HIRCompare::Unequal;
    }
    TU_MATCH_HDRA( (this->mData, x.mData), {)
    TU_ARMA(Generic, ple, pre) {
            return ::compareWithPlaceholders(sp, ple, pre, resolvePlaceholder);
        }
        TU_ARMA(UfcsUnknown, ple, pre) {
            if (ple.item != pre.item) {
                return HIRCompare::Unequal;
            }

            TODO(sp, "Path::compare_with_placeholders - UfcsUnknown");
        }
        TU_ARMA(UfcsInherent, ple, pre) {
            if (ple.item != pre.item) {
                return HIRCompare::Unequal;
            }
            HIRCompare rv = HIRCompare::Equal;
            CMP(rv, ple.type->compareWithPlaceholders(sp, pre.type, resolvePlaceholder));
            CMP(rv, ::compareWithPlaceholders(sp, ple.params, pre.params, resolvePlaceholder));
            return rv;
        }
        TU_ARMA(UfcsKnown, ple, pre) {
            if (ple.item != pre.item) {
                return HIRCompare::Unequal;
            }

            HIRCompare rv = HIRCompare::Equal;
            CMP(rv, ple.type->compareWithPlaceholders(sp, pre.type, resolvePlaceholder));
            CMP(rv, ::compareWithPlaceholders(sp, ple.trait, pre.trait, resolvePlaceholder));
            CMP(rv, ::compareWithPlaceholders(sp, ple.params, pre.params, resolvePlaceholder));
            return rv;
        }
    }
    throw "";
}

Ordering HIRPath::ord(const HIRPath& x) const {
    ORD((unsigned)mData.tag(), (unsigned)x.mData.tag());
    TU_MATCH(HIRPath::Data, (this->mData, x.mData), (tpe, xpe), (Generic, return ::ord(tpe, xpe);), (UfcsInherent, ORD(tpe.type, xpe.type); ORD(tpe.item, xpe.item); return ::ord(tpe.params, xpe.params);), (UfcsKnown, ORD(tpe.type, xpe.type); ORD(tpe.trait, xpe.trait); ORD(tpe.item, xpe.item); return ::ord(tpe.params, xpe.params);), (UfcsUnknown, ORD(tpe.type, xpe.type); ORD(tpe.item, xpe.item); return ::ord(tpe.params, xpe.params);))
    throw "";
}

bool HIRPath::equalsIgnoringRegions(const HIRPath& x) const {
    if (mData.tag() != x.mData.tag()) {
        return false;
    }
    TU_MATCH_HDRA((mData, x.mData), {)
    TU_ARMA(Generic, lhs, rhs) {
            return lhs.equalsIgnoringRegions(rhs);
        }
        TU_ARMA(UfcsInherent, lhs, rhs) {
            return lhs.item == rhs.item && (lhs.type == rhs.type || lhs.type->equalsIgnoringRegions(rhs.type)) && lhs.params.equalsIgnoringRegions(rhs.params) && lhs.implParams.equalsIgnoringRegions(rhs.implParams);
        }
        TU_ARMA(UfcsKnown, lhs, rhs) {
            return lhs.item == rhs.item && (lhs.type == rhs.type || lhs.type->equalsIgnoringRegions(rhs.type)) && lhs.trait.equalsIgnoringRegions(rhs.trait) && lhs.params.equalsIgnoringRegions(rhs.params);
        }
        TU_ARMA(UfcsUnknown, lhs, rhs) {
            return lhs.item == rhs.item && (lhs.type == rhs.type || lhs.type->equalsIgnoringRegions(rhs.type)) && lhs.params.equalsIgnoringRegions(rhs.params);
        }
    }
    throw "";
}

bool HIRPath::operator==(const HIRPath& x) const {
    return this->ord(x) == ::OrdEqual;
}

HIREncodedLiteralPtr::HIREncodedLiteralPtr()
    : p(nullptr)
{
}

HIREncodedLiteralPtr::HIREncodedLiteralPtr(HIREncodedLiteralPtr&& x)
    : p(x.p)
{
    x.p = nullptr;
}

HIREncodedLiteralPtr& HIREncodedLiteralPtr::operator=(HIREncodedLiteralPtr&& x) {
    this->~HIREncodedLiteralPtr();
    this->p = x.p;
    x.p = nullptr;
    return *this;
}

EncodedLiteral& HIREncodedLiteralPtr::operator*() {
    assert(p);
    return *p;
}

const EncodedLiteral& HIREncodedLiteralPtr::operator*() const {
    assert(p);
    return *p;
}

EncodedLiteral* HIREncodedLiteralPtr::operator->() {
    assert(p);
    return p;
}

const EncodedLiteral* HIREncodedLiteralPtr::operator->() const {
    assert(p);
    return p;
}

HIRSimplePath::HIRSimplePath(ThinVector<RcString> members)
    : members(std::move(members))
{
}

HIRSimplePath::HIRSimplePath() {
}

HIRSimplePath::HIRSimplePath(RcString crate)
    : HIRSimplePath(crate, ::std::span<RcString>())
{
}

HIRSimplePath::HIRSimplePath(RcString crate, ::std::vector<RcString> components)
    : HIRSimplePath(crate, ::std::span<RcString>(components))
{
}

HIRSimplePath::HIRSimplePath(RcString crate, ::std::span<RcString> components) {
    // NOTE: Ensure that it's impossible for the crate name to be empty with only one value in `m_members`, simplifies comparison logic
    if (crate.c_str()[0] != '\0' || !components.empty()) {
        members.reserve(1 + components.size());
        members.push_back(std::move(crate));
        for (auto& n : components) {
            members.push_back(std::move(n));
        }
    }
}

HIRSimplePath::HIRSimplePath(RcString crate, ::std::span<const RcString> components) {
    if (crate.c_str()[0] != '\0' || !components.empty()) {
        members.reserve(1 + components.size());
        members.push_back(std::move(crate));
        for (const auto& n : components) {
            members.push_back(n);
        }
    }
}

HIRSimplePath::HIRSimplePath(RcString crate, ::std::initializer_list<RcString> components)
    : HIRSimplePath(std::move(crate), ::std::span<const RcString>(components.begin(), components.end()))
{
}

const RcString& HIRSimplePath::crateName() const {
    static RcString empty;
    return members.empty() ? empty : members.front();
}

::std::vector<RcString> HIRSimplePath::componentsVec() const {
    const auto values = components();
    return {values.begin(), values.end()};
}

Ordering HIRPathParams::ord(const HIRPathParams& x) const {
    if (auto cmp = ::ord(types, x.types)) {
        return cmp;
    }
    if (auto cmp = ::ord(values, x.values)) {
        return cmp;
    }
    return OrdEqual;
}

Ordering HIRTraitPath::AtyEqual::ord(const AtyEqual& x) const {
    ORD(sourceTrait, x.sourceTrait);
    ORD(atyParams, x.atyParams);
    ORD(type, x.type);
    return OrdEqual;
}

Ordering HIRTraitPath::AtyBound::ord(const AtyBound& x) const {
    ORD(sourceTrait, x.sourceTrait);
    ORD(atyParams, x.atyParams);
    ORD(traits, x.traits);
    return OrdEqual;
}

HIRTraitPath::AtyBound HIRTraitPath::AtyBound::clone() const {
    std::vector<HIRTraitPath> newTraits;
    newTraits.reserve(traits.size());
    for (const auto& t : traits) {
        newTraits.push_back(t.clone());
    }
    return AtyBound{sourceTrait.clone(), atyParams.clone(), ::std::move(newTraits)};
}

HIRPath::HIRPath(Data data)
    : mData(mv$(data))
{
}

HIRConstGenericUnevaluated::HIRConstGenericUnevaluated() {
}

::std::ostream& operator<<(::std::ostream& os, const HIRCompare& x) {
    switch (x) {
        case HIRCompare::Equal:
            os << "Equal";
            break;
        case HIRCompare::Fuzzy:
            os << "Fuzzy";
            break;
        case HIRCompare::Unequal:
            os << "Unequal";
            break;
    }
    return os;
}

HIRCompare& operator&=(HIRCompare& x, const HIRCompare& y) {
    if (x == HIRCompare::Unequal) {
    } else if (y == HIRCompare::Unequal) {
        x = HIRCompare::Unequal;
    } else if (y == HIRCompare::Fuzzy) {
        x = HIRCompare::Fuzzy;
    } else {
        // keep as-is
    }
    return x;
}

::std::ostream& operator<<(::std::ostream& os, const HIRTraitPath::AtyEqual& x) {
    os << x.type;
    return os;
}
