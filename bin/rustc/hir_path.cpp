#include "hir_path.h"

#include "hir_expr.h"
#include "hir_type.h"

#include <std/sym/i_map.h>
#include <std/mem/obj_pool.h>
#include <std/rng/split_mix_64.h>

#include <algorithm>

using namespace stl;

namespace {
    struct PathNode: public HIRSimplePathData {
        PathNode* next;

        PathNode(u64 h1, u64 h2, ThinVector<RcString> m, PathNode* next);
    };

    struct PathInterner {
        ObjPool::Ref poolRef = ObjPool::fromMemory();
        ObjPool* pool = poolRef.mutPtr();
        IntMap<PathNode*> table{pool};
    };

    u64 contentHash(const RcString& s) {
        return s.contentHash();
    }

    const u64 POS_STEP = 0x9E3779B97F4A7C15;

    u64 key1(u64 ch, size_t i) {
        return splitMix64(ch + (i + 1) * POS_STEP);
    }

    u64 key2(u64 ch, size_t i) {
        return splitMix64((ch + (i + 1) * POS_STEP) ^ 0xD6E8FEB86659FD93);
    }

    PathInterner& interner() {
        static PathInterner in;
        return in;
    }

    const HIRSimplePathData* findPath(u64 h1, u64 h2) {
        if (auto* head = interner().table.find(h1)) {
            for (auto* n = *head; n; n = n->next) {
                if (n->hash2 == h2) {
                    return n;
                }
            }
        }
        return nullptr;
    }

    const HIRSimplePathData* addPath(u64 h1, u64 h2, ThinVector<RcString> members) {
        auto& in = interner();
        auto* head = in.table.find(h1);
        auto* node = in.pool->make<PathNode>(h1, h2, std::move(members), head ? *head : nullptr);
        if (head) {
            *head = node;
        } else {
            in.table.insert(h1, node);
        }
        return node;
    }

    const HIRSimplePathData* internMembers(ThinVector<RcString> members) {
        if (members.empty()) {
            return nullptr;
        }
        u64 h1 = 0, h2 = 0;
        for (size_t i = 0; i < members.size(); i++) {
            auto ch = contentHash(members[i]);
            h1 ^= key1(ch, i);
            h2 ^= key2(ch, i);
        }
        if (const auto* d = findPath(h1, h2)) {
            return d;
        }
        return addPath(h1, h2, std::move(members));
    }

    HIRCompare compareWithPlaceholders(const Span& sp, const HIRPathParams& l, const HIRPathParams& r, tCbResolveType resolvePlaceholder) {
        return l.compareWithPlaceholders(sp, r, resolvePlaceholder);
    }

    HIRCompare compareWithPlaceholders(const Span& sp, const HIRGenericPath& l, const HIRGenericPath& r, tCbResolveType resolvePlaceholder) {
        return l.compareWithPlaceholders(sp, r, resolvePlaceholder);
    }
}

HIRTraitPath::HIRTraitPath()
    : traitPtr(nullptr)
{
}

HIRTraitPath::HIRTraitPath(HIRGenericPath path)
    : path(std::move(path))
    , traitPtr(nullptr)
{
}

HIRTraitPath::HIRTraitPath(HIRGenericPath path, assocListT typeBounds, std::map<RcString, AtyBound> traitBounds, const HIRTrait* traitPtr, HIRBoundConstness constness)
    : path(std::move(path))
    , typeBounds(std::move(typeBounds))
    , traitBounds(std::move(traitBounds))
    , constness(constness)
    , traitPtr(traitPtr)
{
}

HIRTraitPath::~HIRTraitPath() = default;
HIRTraitPath::HIRTraitPath(HIRTraitPath&&) = default;
HIRTraitPath& HIRTraitPath::operator=(HIRTraitPath&&) = default;

std::ostream& operator<<(std::ostream& os, const HIRSimplePath& x) {
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

std::ostream& operator<<(std::ostream& os, const HIRPathParams& x) {
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

std::ostream& operator<<(std::ostream& os, const HIRGenericPath& x) {
    os << x.path << x.params;
    return os;
}

std::ostream& operator<<(std::ostream& os, const HIRTraitPath& x) {
    if (x.constness == HIRBoundConstness::Always) {
        os << "const ";
    } else if (x.constness == HIRBoundConstness::Maybe) {
        os << "[const] ";
    }
    os << x.path.path;
    bool hasArgs = (x.path.params.types.size() > 0 || x.typeBounds.size() > 0 || x.traitBounds.size() > 0);

    if (hasArgs) {
        os << "<";
    }
    for (const auto& ty : x.path.params.types) {
        os << ty << ",";
    }
    for (const auto& v : x.path.params.values) {
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

std::ostream& operator<<(std::ostream& os, const HIRPath& x) {
    switch (x.data.tag()) {
        case HIRPath::Data::TAG_Generic: {
            auto& e = x.data.as_Generic();
            return os << e;
        }
        case HIRPath::Data::TAG_UfcsInherent: {
            auto& e = x.data.as_UfcsInherent();
            return os << "<" << e.type << " /*- " << e.implParams << "*/>::" << e.item << e.params;
        }
        case HIRPath::Data::TAG_UfcsKnown: {
            auto& e = x.data.as_UfcsKnown();
            os << "<" << e.type << " as ";
            os << e.trait << ">::" << e.item << e.params;
            return os;
            break;
        }
        case HIRPath::Data::TAG_UfcsUnknown: {
            auto& e = x.data.as_UfcsUnknown();
            return os << "<" << e.type << " as _>::" << e.item << e.params;
        }
    }
    return os;
}

HIRSimplePath HIRSimplePath::parent() const {
    if (!p) {
        return *this;
    }
    const auto& m = p->members;
    if (m.size() > 1) {
        auto ch = contentHash(m.back());
        auto i = m.size() - 1;
        auto h1 = p->hash1 ^ key1(ch, i);
        auto h2 = p->hash2 ^ key2(ch, i);
        if (const auto* d = findPath(h1, h2)) {
            return HIRSimplePath(d);
        }
        return HIRSimplePath(addPath(h1, h2, ThinVector<RcString>(m.begin(), m.end() - 1)));
    } else {
        return *this;
    }
}

HIRSimplePath HIRSimplePath::operator+(const RcString& s) const {
    if (!p) {
        return HIRSimplePath(internMembers(ThinVector<RcString>({RcString(), s})));
    }
    const auto& m = p->members;
    auto ch = contentHash(s);
    auto h1 = p->hash1 ^ key1(ch, m.size());
    auto h2 = p->hash2 ^ key2(ch, m.size());
    if (const auto* d = findPath(h1, h2)) {
        return HIRSimplePath(d);
    }
    ThinVector<RcString> members;
    members.reserve(m.size() + 1);
    for (const auto& v : m) {
        members.push_back(v);
    }
    members.push_back(s);
    return HIRSimplePath(addPath(h1, h2, std::move(members)));
}

void HIRSimplePath::operator+=(const RcString& s) {
    *this = *this + s;
}

RcString HIRSimplePath::popComponent() {
    if (!p) {
        return RcString();
    }
    const auto& m = p->members;
    if (m.size() <= 1) {
        return RcString();
    }
    auto rv = m.back();
    if (m.size() == 2 && m[0] == RcString()) {
        p = nullptr;
        return rv;
    }
    auto ch = contentHash(rv);
    auto i = m.size() - 1;
    auto h1 = p->hash1 ^ key1(ch, i);
    auto h2 = p->hash2 ^ key2(ch, i);
    if (const auto* d = findPath(h1, h2)) {
        p = d;
    } else {
        p = addPath(h1, h2, ThinVector<RcString>(m.begin(), m.end() - 1));
    }
    return rv;
}

void HIRSimplePath::updateCrateName(RcString v) {
    if (!p) {
        if (v != RcString()) {
            p = internMembers(ThinVector<RcString>({std::move(v)}));
        }
        return;
    }
    const auto& m = p->members;
    if (m.empty()) {
        p = internMembers(ThinVector<RcString>({std::move(v)}));
    } else if (v.c_str()[0] == '\0' && m.size() == 1) {
        p = nullptr;
    } else {
        auto chOld = contentHash(m[0]);
        auto chNew = contentHash(v);
        auto h1 = p->hash1 ^ key1(chOld, 0) ^ key1(chNew, 0);
        auto h2 = p->hash2 ^ key2(chOld, 0) ^ key2(chNew, 0);
        if (const auto* d = findPath(h1, h2)) {
            p = d;
            return;
        }
        ThinVector<RcString> members(m.begin(), m.end());
        members[0] = std::move(v);
        p = addPath(h1, h2, std::move(members));
    }
}

void HIRSimplePath::updateLastComponent(RcString v) {
    const auto& m = p->members;
    assert(m.size() >= 2);
    auto i = m.size() - 1;
    auto chOld = contentHash(m.back());
    auto chNew = contentHash(v);
    auto h1 = p->hash1 ^ key1(chOld, i) ^ key1(chNew, i);
    auto h2 = p->hash2 ^ key2(chOld, i) ^ key2(chNew, i);
    if (const auto* d = findPath(h1, h2)) {
        p = d;
        return;
    }
    ThinVector<RcString> members(m.begin(), m.end());
    members.back() = std::move(v);
    p = addPath(h1, h2, std::move(members));
}

bool HIRSimplePath::startsWith(const HIRSimplePath& x, bool skipLast /*=false*/) const {
    if (!x.p) {
        return crateName() == RcString();
    }
    if (!p) {
        return skipLast && x.p->members.size() == 1;
    }
    const auto& m = p->members;
    const auto& xm = x.p->members;
    if (m.size() < xm.size() - (skipLast ? 1 : 0)) {
        return false;
    }
    for (size_t i = 0; i < xm.size() - (skipLast ? 1 : 0); i++) {
        if (xm[i] != m[i]) {
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
    : path(mv$(sp))
{
}

HIRGenericPath::HIRGenericPath(HIRSimplePath sp, HIRPathParams params)
    : path(mv$(sp))
    , params(mv$(params))
{
}

HIRGenericPath HIRGenericPath::clone() const {
    return HIRGenericPath(path.clone(), params.clone());
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
    return path == x.path && params.equalsIgnoringRegions(x.params);
}

Ordering HIRGenericPath::ord(const HIRGenericPath& x) const {
    ORD(path, x.path);
    ORD(params, x.params);

    return OrdEqual;
}

HIRTraitPath HIRTraitPath::clone() const {
    HIRTraitPath rv{path.clone(), {}, {}, traitPtr, constness};

    for (const auto& assoc : typeBounds) {
        rv.typeBounds.insert(std::make_pair(assoc.first, assoc.second.clone()));
    }
    for (const auto& assoc : traitBounds) {
        rv.traitBounds.insert(std::make_pair(assoc.first, assoc.second.clone()));
    }

    return rv;
}

bool HIRTraitPath::equalsIgnoringRegions(const HIRTraitPath& x) const {
    if (!path.equalsIgnoringRegions(x.path) || typeBounds.size() != x.typeBounds.size() || traitBounds.size() != x.traitBounds.size()) {
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
    ORD(path, x.path);
    ORD(traitBounds, x.traitBounds);
    ORD(typeBounds, x.typeBounds);
    return OrdEqual;
}

HIRPath::HIRPath(HIRGenericPath gp)
    : data(HIRPath::Data::make_Generic(mv$(gp)))
{
}

HIRPath::HIRPath(HIRSimplePath sp)
    : data(HIRPath::Data::make_Generic(HIRGenericPath(mv$(sp))))
{
}

HIRPath::HIRPath(HIRTypeRef ty, RcString item, HIRPathParams itemParams)
    : data(Data::make_UfcsInherent({mv$(ty), mv$(item), mv$(itemParams)}))
{
}

HIRPath::HIRPath(HIRTypeRef ty, HIRGenericPath trait, RcString item, HIRPathParams itemParams)
    : data(Data::make_UfcsKnown({mv$(ty), mv$(trait), mv$(item), mv$(itemParams)}))
{
}

HIRPath HIRPath::clone() const {
    switch (data.tag()) {
        case HIRPathData::TAG_Generic: {
            auto& e = data.as_Generic();
            return HIRPath(Data::make_Generic(e.clone()));
        }
        case HIRPathData::TAG_UfcsInherent: {
            auto& e = data.as_UfcsInherent();
            return HIRPath(Data::make_UfcsInherent({e.type, e.item, e.params.clone(), e.implParams.clone()}));
        }
        case HIRPathData::TAG_UfcsKnown: {
            auto& e = data.as_UfcsKnown();
            return HIRPath(
                Data::make_UfcsKnown({
                    e.type,
                    e.trait.clone(),
                    e.item,
                    e.params.clone(),
                })
            );
        }
        case HIRPathData::TAG_UfcsUnknown: {
            auto& e = data.as_UfcsUnknown();
            return HIRPath(Data::make_UfcsUnknown({e.type, e.item, e.params.clone()}));
        }
    }
    UNREACHABLE();
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

            struct H2 {
                static bool getLiteral(const HIRConstGeneric& v, U128& out) {
                    if (const auto* ev = v.opt_Evaluated()) {
                        auto sl = EncodedLiteralSlice(**ev);
                        if (sl.size == 0 || sl.size > 16) {
                            return false;
                        }
                        out = sl.readUint(sl.size);
                        return true;
                    }
                    if (const auto* uev = v.opt_Unevaluated()) {
                        if (!(*uev)->expr || !*(*uev)->expr) {
                            return false;
                        }
                        const auto& node = **(*uev)->expr;
                        if (const auto* lit = cast<const HIRExprNodeLiteral>(&node)) {
                            if (const auto* i = lit->data.opt_Integer()) {
                                out = i->value;
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
    if (this->path != x.path) {
        return HIRCompare::Unequal;
    }

    return this->params.compareWithPlaceholders(sp, x.params, resolvePlaceholder);
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
    auto rv = path.compareWithPlaceholders(sp, x.path, resolvePlaceholder);
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
    if (this->data.tag() != x.data.tag()) {
        return HIRCompare::Unequal;
    }
    switch (this->data.tag()) {
        case HIRPathData::TAG_Generic: {
            auto& ple = this->data.as_Generic();
            auto& pre = x.data.as_Generic();
            return ::compareWithPlaceholders(sp, ple, pre, resolvePlaceholder);
        }
        case HIRPathData::TAG_UfcsUnknown: {
            auto& ple = this->data.as_UfcsUnknown();
            auto& pre = x.data.as_UfcsUnknown();
            if (ple.item != pre.item) {
                return HIRCompare::Unequal;
            }
            HIRCompare rv = HIRCompare::Equal;
            CMP(rv, ple.type->compareWithPlaceholders(sp, pre.type, resolvePlaceholder));
            CMP(rv, ::compareWithPlaceholders(sp, ple.params, pre.params, resolvePlaceholder));
            return rv;
        }
        case HIRPathData::TAG_UfcsInherent: {
            auto& ple = this->data.as_UfcsInherent();
            auto& pre = x.data.as_UfcsInherent();
            if (ple.item != pre.item) {
                return HIRCompare::Unequal;
            }
            HIRCompare rv = HIRCompare::Equal;
            CMP(rv, ple.type->compareWithPlaceholders(sp, pre.type, resolvePlaceholder));
            CMP(rv, ::compareWithPlaceholders(sp, ple.params, pre.params, resolvePlaceholder));
            return rv;
        }
        case HIRPathData::TAG_UfcsKnown: {
            auto& ple = this->data.as_UfcsKnown();
            auto& pre = x.data.as_UfcsKnown();
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
    UNREACHABLE();
}

Ordering HIRPath::ord(const HIRPath& x) const {
    ORD((unsigned)data.tag(), (unsigned)x.data.tag());
    switch (this->data.tag()) {
        case HIRPath::Data::TAG_Generic: {
            auto& tpe = this->data.as_Generic();
            auto& xpe = x.data.as_Generic();
            return ::ord(tpe, xpe);
        }
        case HIRPath::Data::TAG_UfcsInherent: {
            auto& tpe = this->data.as_UfcsInherent();
            auto& xpe = x.data.as_UfcsInherent();
            ORD(tpe.type, xpe.type);
            ORD(tpe.item, xpe.item);
            return ::ord(tpe.params, xpe.params);
            break;
        }
        case HIRPath::Data::TAG_UfcsKnown: {
            auto& tpe = this->data.as_UfcsKnown();
            auto& xpe = x.data.as_UfcsKnown();
            ORD(tpe.type, xpe.type);
            ORD(tpe.trait, xpe.trait);
            ORD(tpe.item, xpe.item);
            return ::ord(tpe.params, xpe.params);
            break;
        }
        case HIRPath::Data::TAG_UfcsUnknown: {
            auto& tpe = this->data.as_UfcsUnknown();
            auto& xpe = x.data.as_UfcsUnknown();
            ORD(tpe.type, xpe.type);
            ORD(tpe.item, xpe.item);
            return ::ord(tpe.params, xpe.params);
            break;
        }
    }
    UNREACHABLE();
}

bool HIRPath::equalsIgnoringRegions(const HIRPath& x) const {
    if (data.tag() != x.data.tag()) {
        return false;
    }
    switch (data.tag()) {
        case HIRPathData::TAG_Generic: {
            auto& lhs = data.as_Generic();
            auto& rhs = x.data.as_Generic();
            return lhs.equalsIgnoringRegions(rhs);
        }
        case HIRPathData::TAG_UfcsInherent: {
            auto& lhs = data.as_UfcsInherent();
            auto& rhs = x.data.as_UfcsInherent();
            return lhs.item == rhs.item && (lhs.type == rhs.type || lhs.type->equalsIgnoringRegions(rhs.type)) && lhs.params.equalsIgnoringRegions(rhs.params) && lhs.implParams.equalsIgnoringRegions(rhs.implParams);
        }
        case HIRPathData::TAG_UfcsKnown: {
            auto& lhs = data.as_UfcsKnown();
            auto& rhs = x.data.as_UfcsKnown();
            return lhs.item == rhs.item && (lhs.type == rhs.type || lhs.type->equalsIgnoringRegions(rhs.type)) && lhs.trait.equalsIgnoringRegions(rhs.trait) && lhs.params.equalsIgnoringRegions(rhs.params);
        }
        case HIRPathData::TAG_UfcsUnknown: {
            auto& lhs = data.as_UfcsUnknown();
            auto& rhs = x.data.as_UfcsUnknown();
            return lhs.item == rhs.item && (lhs.type == rhs.type || lhs.type->equalsIgnoringRegions(rhs.type)) && lhs.params.equalsIgnoringRegions(rhs.params);
        }
    }
    UNREACHABLE();
}

bool HIRPath::operator==(const HIRPath& x) const {
    return this->ord(x) == ::OrdEqual;
}

const EncodedLiteral* freezeEncodedLiteral(ObjPool& pool, EncodedLiteral e) {
    return pool.make<EncodedLiteral>(mv$(e));
}

HIRSimplePath::HIRSimplePath(ThinVector<RcString> members)
    : p(internMembers(std::move(members)))
{
}

HIRSimplePath::HIRSimplePath()
    : p(nullptr)
{
}

HIRSimplePath::HIRSimplePath(RcString crate)
    : HIRSimplePath(crate, std::span<const RcString>())
{
}

HIRSimplePath::HIRSimplePath(RcString crate, std::vector<RcString> components)
    : HIRSimplePath(crate, std::span<const RcString>(components))
{
}

HIRSimplePath::HIRSimplePath(RcString crate, std::span<RcString> components)
    : HIRSimplePath(crate, std::span<const RcString>(components.begin(), components.end()))
{
}

HIRSimplePath::HIRSimplePath(RcString crate, std::span<const RcString> components) {
    if (crate.c_str()[0] == '\0' && components.empty()) {
        p = nullptr;
        return;
    }
    auto ch = contentHash(crate);
    u64 h1 = key1(ch, 0);
    u64 h2 = key2(ch, 0);
    for (size_t i = 0; i < components.size(); i++) {
        ch = contentHash(components[i]);
        h1 ^= key1(ch, i + 1);
        h2 ^= key2(ch, i + 1);
    }
    if (const auto* d = findPath(h1, h2)) {
        p = d;
        return;
    }
    ThinVector<RcString> members;
    members.reserve(1 + components.size());
    members.push_back(std::move(crate));
    for (const auto& n : components) {
        members.push_back(n);
    }
    p = addPath(h1, h2, std::move(members));
}

HIRSimplePath::HIRSimplePath(RcString crate, std::initializer_list<RcString> components)
    : HIRSimplePath(std::move(crate), std::span<const RcString>(components.begin(), components.end()))
{
}

RcString HIRSimplePath::crateName() const {
    return p ? p->members.front() : RcString();
}

std::vector<RcString> HIRSimplePath::componentsVec() const {
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
    return AtyBound{sourceTrait.clone(), atyParams.clone(), std::move(newTraits)};
}

HIRPath::HIRPath(Data data)
    : data(mv$(data))
{
}

HIRConstGenericUnevaluated::HIRConstGenericUnevaluated() {
}

std::ostream& operator<<(std::ostream& os, const HIRCompare& x) {
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
    }
    return x;
}

std::ostream& operator<<(std::ostream& os, const HIRTraitPath::AtyEqual& x) {
    os << x.type;
    return os;
}

PathNode::PathNode(u64 h1, u64 h2, ThinVector<RcString> m, PathNode* next)
    : HIRSimplePathData{h1, h2, std::move(m)}
    , next(next)
{
}
