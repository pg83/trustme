#include "hir_path.h"

#include "output.h"
#include "mir_mir.h"
#include "hir_expr.h"
#include "hir_type.h"
#include "hir_main_bindings.h"

#include <std/sym/i_map.h>
#include <std/lib/vector.h>
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

    const HIRConstGeneric* getUnevaluatedParam(const HIRConstGenericUnevaluated& value, unsigned int binding) {
        const HIRPathParams* params = nullptr;
        switch (binding >> 8) {
            case GENERICImpl:
                params = &value.paramsImpl;
                break;
            case GENERICItem:
                params = &value.paramsItem;
                break;
            default:
                return nullptr;
        }
        const unsigned int index = binding & 0xFF;
        return index < params->values.size() ? &params->values[index] : nullptr;
    }

    bool constExprLiteralsEqual(const HIRExprNodeLiteral& left, const HIRExprNodeLiteral& right) {
        if (left.data.tag() != right.data.tag()) {
            return false;
        }
        switch (left.data.tag()) {
            case HIRExprLiteral::TAG_Integer: {
                auto& l = left.data.as_Integer();
                auto& r = right.data.as_Integer();
                return l.type == r.type && l.value == r.value;
            }
            case HIRExprLiteral::TAG_Float: {
                auto& l = left.data.as_Float();
                auto& r = right.data.as_Float();
                return l.type == r.type && l.value == r.value;
            }
            case HIRExprLiteral::TAG_Boolean: {
                auto& l = left.data.as_Boolean();
                auto& r = right.data.as_Boolean();
                return l == r;
            }
            case HIRExprLiteral::TAG_String: {
                auto& l = left.data.as_String();
                auto& r = right.data.as_String();
                return l == r;
            }
            case HIRExprLiteral::TAG_CString: {
                auto& l = left.data.as_CString();
                auto& r = right.data.as_CString();
                return l.v == r.v;
            }
            case HIRExprLiteral::TAG_ByteString: {
                auto& l = left.data.as_ByteString();
                auto& r = right.data.as_ByteString();
                return ::ord(l, r) == OrdEqual;
            }
        }
        UNREACHABLE();
    }

    bool constExprNodesEqual(const HIRConstGenericUnevaluated& leftValue, const HIRExprNode& left, const HIRConstGenericUnevaluated& rightValue, const HIRExprNode& right) {
        if (const auto* l = cast<const HIRExprNodeConstParam>(&left)) {
            const auto* r = cast<const HIRExprNodeConstParam>(&right);
            if (!r) {
                return false;
            }
            const auto* lParam = getUnevaluatedParam(leftValue, l->binding);
            const auto* rParam = getUnevaluatedParam(rightValue, r->binding);
            return lParam && rParam ? *lParam == *rParam : l->binding == r->binding;
        }
        if (const auto* l = cast<const HIRExprNodeLiteral>(&left)) {
            const auto* r = cast<const HIRExprNodeLiteral>(&right);
            return r && constExprLiteralsEqual(*l, *r);
        }
        if (const auto* l = cast<const HIRExprNodeBinOp>(&left)) {
            const auto* r = cast<const HIRExprNodeBinOp>(&right);
            return r && l->op == r->op && constExprNodesEqual(leftValue, *l->left, rightValue, *r->left) && constExprNodesEqual(leftValue, *l->right, rightValue, *r->right);
        }
        if (const auto* l = cast<const HIRExprNodeUniOp>(&left)) {
            const auto* r = cast<const HIRExprNodeUniOp>(&right);
            return r && l->op == r->op && constExprNodesEqual(leftValue, *l->value, rightValue, *r->value);
        }
        if (const auto* l = cast<const HIRExprNodeCast>(&left)) {
            const auto* r = cast<const HIRExprNodeCast>(&right);
            return r && l->dstType == r->dstType && constExprNodesEqual(leftValue, *l->value, rightValue, *r->value);
        }
        if (const auto* l = cast<const HIRExprNodeConstBlock>(&left)) {
            const auto* r = cast<const HIRExprNodeConstBlock>(&right);
            return r && constExprNodesEqual(leftValue, *l->inner, rightValue, *r->inner);
        }
        if (const auto* l = cast<const HIRExprNodeCallPath>(&left)) {
            const auto* r = cast<const HIRExprNodeCallPath>(&right);
            if (!r || l->path != r->path || l->args.size() != r->args.size()) {
                return false;
            }
            for (unsigned int i = 0; i < l->args.size(); i++) {
                if (!constExprNodesEqual(leftValue, *l->args[i], rightValue, *r->args[i])) {
                    return false;
                }
            }
            return true;
        }
        if (const auto* l = cast<const HIRExprNodeBlock>(&left)) {
            const auto* r = cast<const HIRExprNodeBlock>(&right);
            if (!r || l->nodes.size() != r->nodes.size() || static_cast<bool>(l->valueNode) != static_cast<bool>(r->valueNode)) {
                return false;
            }
            for (unsigned int i = 0; i < l->nodes.size(); i++) {
                if (!constExprNodesEqual(leftValue, *l->nodes[i], rightValue, *r->nodes[i])) {
                    return false;
                }
            }
            return !l->valueNode || constExprNodesEqual(leftValue, *l->valueNode, rightValue, *r->valueNode);
        }
        return false;
    }

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
    BUG_ASSERT(m.size() >= 2);
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

HIRPathParams::HIRPathParams(const HIRTypeData* ty0) {
    types = ThinVector<const HIRTypeData*>(1);
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

HIRPath::HIRPath(const HIRTypeData* ty, RcString item, HIRPathParams itemParams)
    : data(Data::make_UfcsInherent({mv$(ty), mv$(item), mv$(itemParams)}))
{
}

HIRPath::HIRPath(const HIRTypeData* ty, HIRGenericPath trait, RcString item, HIRPathParams itemParams)
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

    TRACE_FUNCTION_F(StringView("(PathParams) ") << *this << StringView(" with ") << x);
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

HIRSimplePath::HIRSimplePath(RcString crate, Vector<RcString> components)
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

Vector<RcString> HIRSimplePath::componentsVec() const {
    const auto values = components();
    Vector<RcString> result;
    result.append(values.data(), values.size());
    return result;
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

HIRConstGenericUnevaluated::HIRConstGenericUnevaluated(HIRExprPtr ep)
    : expr(std::make_shared<HIRExprPtr>(std::move(ep)))
{
}

HIRConstGenericUnevaluated HIRConstGenericUnevaluated::clone() const {
    HIRConstGenericUnevaluated rv;
    rv.selfType = selfType;
    rv.paramsImpl = paramsImpl.clone();
    rv.paramsItem = paramsItem.clone();
    rv.expr = expr;
    return rv;
}

HIRConstGenericUnevaluated HIRConstGenericUnevaluated::monomorph(const Span& sp, const Monomorphiser& ms, bool allowInfer /*=true*/) const {
    HIRConstGenericUnevaluated rv;
    rv.selfType = selfType ? ms.monomorphType(sp, selfType, allowInfer) : nullptr;
    rv.paramsImpl = ms.monomorphPathParams(sp, paramsImpl, allowInfer);
    rv.paramsItem = ms.monomorphPathParams(sp, paramsItem, allowInfer);
    rv.expr = this->expr;
    return rv;
}

bool HIRConstGenericUnevaluated::equivalent(const HIRConstGenericUnevaluated& x) const {
    return selfType == x.selfType && constExprNodesEqual(*this, **this->expr, x, **x.expr);
}

Ordering HIRConstGenericUnevaluated::ord(const HIRConstGenericUnevaluated& x) const {
    if (this->expr.get() != x.expr.get()) {
        if (!this->expr->mir != !x.expr->mir) {
            return (this->expr->mir ? OrdGreater : OrdLess);
        }

        // HACK: If the inner is a const param on both, sort based on that.

        const auto* tn = cast<const HIRExprNodeConstParam>(&**this->expr);
        const auto* xn = cast<const HIRExprNodeConstParam>(&**x.expr);
        if (tn && xn) {
            return ::ord(tn->binding, xn->binding);
        }

        auto vT = FMT(*this);
        auto vX = FMT(x);
        return ::ord(vT, vX);
    }
    if (auto cmp = ::ord(this->selfType, x.selfType)) {
        return cmp;
    }
    if (auto cmp = this->paramsImpl.ord(x.paramsImpl)) {
        return cmp;
    }
    if (auto cmp = this->paramsItem.ord(x.paramsItem)) {
        return cmp;
    }
    return OrdEqual;
}

void HIRConstGenericUnevaluated::fmt(ZeroCopyOutput& os) const {
    os << StringView("{");
    if (this->selfType) {
        os << StringView("S=") << this->selfType;
    }
    os << StringView("0=") << this->paramsImpl;
    os << StringView("1=") << this->paramsItem;
    os << StringView("}");
    if (expr->mir) {
        for (const auto& b : expr->mir->blocks) {
            os << StringView("bb") << (&b - expr->mir->blocks.data()) << StringView(":{ ");
            for (const auto& s : b.statements) {
                os << s << StringView("; ");
            }
            os << b.terminator;
            os << StringView(" }");
        }
    } else {
        struct NoNewline final: ZeroCopyOutput {
            ZeroCopyOutput& inner;
            char buffer[4096];

            NoNewline(ZeroCopyOutput& inner)
                : inner(inner)
            {
            }

            void* imbueImpl(size_t* len) override {
                if (*len > sizeof(buffer)) {
                    *len = sizeof(buffer);
                }
                return buffer;
            }

            void commitImpl(size_t len) override {
                for (size_t i = 0; i < len; ++i) {
                    if (buffer[i] == '\n') {
                        buffer[i] = ' ';
                    }
                }
                inner.write(buffer, len);
            }
        } innerOs(os);

        HIRDumpExpr(innerOs, *expr);
    }
}

HIRConstGeneric HIRConstGeneric::clone() const {
    switch ((*this).tag()) {
        case HIRConstGeneric::TAG_Infer: {
            auto& e = (*this).as_Infer();
            return e;
        }
        case HIRConstGeneric::TAG_Unevaluated: {
            auto& e = (*this).as_Unevaluated();
            return std::make_unique<HIRConstGenericUnevaluated>(e->clone());
        }
        case HIRConstGeneric::TAG_Generic: {
            auto& e = (*this).as_Generic();
            return e;
        }
        case HIRConstGeneric::TAG_Evaluated: {
            auto& e = (*this).as_Evaluated();
            return e;
        }
    }
    UNREACHABLE();
}

HIRConstGenericUnevaluated::HIRConstGenericUnevaluated() {
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

PathNode::PathNode(u64 h1, u64 h2, ThinVector<RcString> m, PathNode* next)
    : HIRSimplePathData{h1, h2, std::move(m)}
    , next(next)
{
}

template <>
void stl::output<ZeroCopyOutput, HIRBoundConstness>(ZeroCopyOutput& out, HIRBoundConstness value) {
    switch (value) {
        case HIRBoundConstness::Never:
            out << StringView("Never");
            return;
        case HIRBoundConstness::Always:
            out << StringView("Always");
            return;
        case HIRBoundConstness::Maybe:
            out << StringView("Maybe");
            return;
    }
}

template <>
void stl::output<ZeroCopyOutput, HIRInferData>(ZeroCopyOutput& out, HIRInferData value) {
    out << StringView("HIRInferData(index = ") << value.index << StringView(")");
}

template <>
void stl::output<ZeroCopyOutput, std::vector<HIRGenericPath>>(ZeroCopyOutput& out, const std::vector<HIRGenericPath>& values) {
    outCont(out, values);
}

template <>
void stl::output<ZeroCopyOutput, HIRSimplePath>(ZeroCopyOutput& os, HIRSimplePath x) {
    if (x.crateName() != "") {
        os << StringView("::\"") << x.crateName() << StringView("\"");
    } else if (x.components().size() == 0) {
        os << StringView("::");
    } else {
    }
    for (const auto& n : x.components()) {
        os << StringView("::") << n;
    }
    return;
}

template <>
void stl::output<ZeroCopyOutput, HIRPathParams>(ZeroCopyOutput& os, const HIRPathParams& x) {
    bool hasArgs = (x.types.size() > 0 || x.values.size() > 0);

    if (hasArgs) {
        os << StringView("<");
    }
    for (const auto& ty : x.types) {
        os << ty << StringView(",");
    }
    for (const auto& v : x.values) {
        os << StringView("{") << v << StringView("},");
    }
    if (hasArgs) {
        os << StringView(">");
    }
    return;
}

template <>
void stl::output<ZeroCopyOutput, HIRGenericPath>(ZeroCopyOutput& os, const HIRGenericPath& x) {
    os << x.path << x.params;
    return;
}

template <>
void stl::output<ZeroCopyOutput, HIRTraitPath>(ZeroCopyOutput& os, const HIRTraitPath& x) {
    if (x.constness == HIRBoundConstness::Always) {
        os << StringView("const ");
    } else if (x.constness == HIRBoundConstness::Maybe) {
        os << StringView("[const] ");
    }
    os << x.path.path;
    bool hasArgs = (x.path.params.types.size() > 0 || x.typeBounds.size() > 0 || x.traitBounds.size() > 0);

    if (hasArgs) {
        os << StringView("<");
    }
    for (const auto& ty : x.path.params.types) {
        os << ty << StringView(",");
    }
    for (const auto& v : x.path.params.values) {
        os << v << StringView(",");
    }
    for (const auto& assoc : x.typeBounds) {
        os << assoc.first << StringView("{") << assoc.second.sourceTrait << StringView("}=") << assoc.second << StringView(",");
    }
    for (const auto& assoc : x.traitBounds) {
        for (const auto& trait : assoc.second.traits) {
            os << assoc.first << StringView("{") << assoc.second.sourceTrait << StringView("}: ") << trait << StringView(",");
        }
    }
    if (hasArgs) {
        os << StringView(">");
    }
    return;
}

template <>
void stl::output<ZeroCopyOutput, HIRPath>(ZeroCopyOutput& os, const HIRPath& x) {
    switch (x.data.tag()) {
        case HIRPath::Data::TAG_Generic: {
            auto& e = x.data.as_Generic();
            os << e;
            return;
        }
        case HIRPath::Data::TAG_UfcsInherent: {
            auto& e = x.data.as_UfcsInherent();
            os << StringView("<") << e.type << StringView(" /*- ") << e.implParams << StringView("*/>::") << e.item << e.params;
            return;
        }
        case HIRPath::Data::TAG_UfcsKnown: {
            auto& e = x.data.as_UfcsKnown();
            os << StringView("<") << e.type << StringView(" as ");
            os << e.trait << StringView(">::") << e.item << e.params;
            return;
            break;
        }
        case HIRPath::Data::TAG_UfcsUnknown: {
            auto& e = x.data.as_UfcsUnknown();
            os << StringView("<") << e.type << StringView(" as _>::") << e.item << e.params;
            return;
        }
    }
    return;
}

template <>
void stl::output<ZeroCopyOutput, HIRConstGenericUnevaluated>(ZeroCopyOutput& out, const HIRConstGenericUnevaluated& value) {
    value.fmt(out);
}

template <>
void stl::output<ZeroCopyOutput, HIRTraitPath::AtyEqual>(ZeroCopyOutput& os, const HIRTraitPath::AtyEqual& x) {
    os << x.type;
    return;
}

template <>
void stl::output<ZeroCopyOutput, std::pair<const RcString, HIRTraitPath::AtyEqual>>(ZeroCopyOutput& out, const std::pair<const RcString, HIRTraitPath::AtyEqual>& value) {
    out << value.first << StringView(": ") << value.second;
}

template <>
void stl::output<ZeroCopyOutput, std::map<RcString, HIRTraitPath::AtyEqual>>(ZeroCopyOutput& out, const std::map<RcString, HIRTraitPath::AtyEqual>& values) {
    outCont(out, values);
}

template <>
void stl::output<ZeroCopyOutput, std::vector<HIRTraitPath>>(ZeroCopyOutput& out, const std::vector<HIRTraitPath>& values) {
    outCont(out, values);
}

template <>
void stl::output<ZeroCopyOutput, std::pair<const std::string, HIRSimplePath>>(ZeroCopyOutput& out, const std::pair<const std::string, HIRSimplePath>& value) {
    out << StringView("(") << value.first << StringView(", ") << value.second << StringView(")");
}
