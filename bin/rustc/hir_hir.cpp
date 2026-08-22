#include "hir_hir.h"

#include <std/mem/obj_pool.h>
#include <std/lib/vector.h>

#include "floats.h"
#include "mir_mir.h"
#include "hir_expr.h"
#include "wire_board.h"
#include "trans_target.h"
#include "hir_item_path.h"
#include "hir_expr_state.h"
#include "hir_typeck_common.h"
#include "mir_main_bindings.h"
#include "hir_typeck_expr_visit.h" // for invoking typecheck
#include "hir_conv_main_bindings.h"
#include "macro_rules_macro_rules.h" // Used to update the crate name
#include "hir_expand_main_bindings.h"

#include <optional>
#include <algorithm>

::std::ostream& operator<<(::std::ostream& os, const HIRPublicity& x) {
    if (!x.visPath) {
        os << "pub";
    } else if (*x.visPath == *HIRPublicity::nonePath) {
        os << "priv";
    } else {
        os << "pub(" << *x.visPath << ")";
    }
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const HIRConstGeneric& x) {
        switch (x.tag()) {
            case HIRConstGeneric::TAG_Infer: {
                auto& e = x.as_Infer();
                os << "Infer";
                if (e.index != ~0u) {
                    os << "(";
                    os << e.index;
                    os << ")";
                }
                break;
            }
            case HIRConstGeneric::TAG_Unevaluated: {
                auto& e = x.as_Unevaluated();
                os << "Unevaluated(";
                e->fmt(os);
                os << ")";
                break;
            }
            case HIRConstGeneric::TAG_Generic: {
                auto& e = x.as_Generic();
                os << "Generic(" << e << ")";
                break;
            }
            case HIRConstGeneric::TAG_Evaluated: {
                auto& e = x.as_Evaluated();
                os << "Evaluated(" << *e << ")";
                break;
            }
        }
        return os;
}

bool HIRConstGeneric::operator==(const HIRConstGeneric& x) const {
    if (this->tag() != x.tag()) {
        return false;
    }
        switch ((*this).tag()) {
            case HIRConstGeneric::TAG_Infer: {
                auto& te = (*this).as_Infer();
                auto& xe = x.as_Infer();
                return te.index == xe.index;
            }
            case HIRConstGeneric::TAG_Unevaluated: {
                auto& te = (*this).as_Unevaluated();
                auto& xe = x.as_Unevaluated();
                return te->equivalent(*xe);
            }
            case HIRConstGeneric::TAG_Generic: {
                auto& te = (*this).as_Generic();
                auto& xe = x.as_Generic();
                return te == xe;
            }
            case HIRConstGeneric::TAG_Evaluated: {
                auto& te = (*this).as_Evaluated();
                auto& xe = x.as_Evaluated();
                return EncodedLiteralSlice(*te) == EncodedLiteralSlice(*xe);
            }
        }
        return true;
}

Ordering HIRConstGeneric::ord(const HIRConstGeneric& x) const {
    if (auto cmp = ::ord(static_cast<int>(this->tag()), static_cast<int>(x.tag()))) {
        return cmp;
    }
        switch ((*this).tag()) {
            case HIRConstGeneric::TAG_Infer: {
                auto& te = (*this).as_Infer();
                auto& xe = x.as_Infer();
                if (auto cmp = ::ord(te.index, xe.index)) {
                    return cmp;
                }
                break;
            }
            case HIRConstGeneric::TAG_Unevaluated: {
                auto& te = (*this).as_Unevaluated();
                auto& xe = x.as_Unevaluated();
                if (te->equivalent(*xe)) {
                    return OrdEqual;
                }
                return te->ord(*xe);
            }
            case HIRConstGeneric::TAG_Generic: {
                auto& te = (*this).as_Generic();
                auto& xe = x.as_Generic();
                if (auto cmp = ::ord(te, xe)) {
                    return cmp;
                }
                break;
            }
            case HIRConstGeneric::TAG_Evaluated: {
                auto& te = (*this).as_Evaluated();
                auto& xe = x.as_Evaluated();
                if (auto cmp = ::ord(EncodedLiteralSlice(*te), EncodedLiteralSlice(*xe))) {
                    return cmp;
                }
                break;
            }
        }
        return OrdEqual;
}

::std::ostream& operator<<(::std::ostream& os, const HIRConstGenericUnevaluated& x) {
    x.fmt(os);
    return os;
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

namespace {
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
                    return l == r;
                }
            }
            throw "";
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
}

bool HIRConstGenericUnevaluated::equivalent(const HIRConstGenericUnevaluated& x) const {
    return selfType == x.selfType && constExprNodesEqual(*this, **this->expr, x, **x.expr);
}

Ordering HIRConstGenericUnevaluated::ord(const HIRConstGenericUnevaluated& x) const {
    if (this->expr.get() != x.expr.get()) {
        // If only one has populated MIR, they can't be equal (sort populated MIR after)
        if (!this->expr->mir != !x.expr->mir) {
            return (this->expr->mir ? OrdGreater : OrdLess);
        }

        // HACK: If the inner is a const param on both, sort based on that.
        // - Very similar to the ordering of ASTType*::Generic
        const auto* tn = cast<const HIRExprNodeConstParam>(&**this->expr);
        const auto* xn = cast<const HIRExprNodeConstParam>(&**x.expr);
        if (tn && xn) {
            // Is this valid? What if they're from different scopes?
            return ::ord(tn->binding, xn->binding);
        }

        // EVIL OPTION: Just compare the string representations
        // - The fmt() routine prints MIR blocks (when populated) or the source expression
        //   (when not) in a deterministic, pointer-free form, so this gives a stable order.
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

void HIRConstGenericUnevaluated::fmt(::std::ostream& os) const {
    os << "{";
    if (this->selfType) {
        os << "S=" << this->selfType;
    }
    os << "0=" << this->paramsImpl;
    os << "1=" << this->paramsItem;
    os << "}";
    if (expr->mir) {
        for (const auto& b : expr->mir->blocks) {
            os << "bb" << (&b - expr->mir->blocks.data()) << ":{ ";
            for (const auto& s : b.statements) {
                os << s << "; ";
            }
            os << b.terminator;
            os << " }";
        }
    } else {
        struct NoNewline: public ::std::ostream, ::std::streambuf {
            ::std::ostream& inner;

            NoNewline(::std::ostream& inner)
                : ::std::ostream(this)
                , inner(inner)
            {
            }

            int overflow(int c) override {
                switch (c) {
                    case '\n':
                        inner.put(' ');
                        break;
                    default:
                        inner.put(c);
                        break;
                }
                return 0;
            }
        } innerOs(os);

        HIRDumpExpr(innerOs, *expr);
    }
}

::std::ostream& operator<<(::std::ostream& os, const HIRStruct::Repr& x) {
    os << "repr(";
    switch (x) {
        case HIRStruct::Repr::Rust:
            os << "Rust";
            break;
        case HIRStruct::Repr::C:
            os << "C";
            break;
        case HIRStruct::Repr::Simd:
            os << "simd";
            break;
        case HIRStruct::Repr::Transparent:
            os << "transparent";
            break;
    }
    os << ")";
    return os;
}

HIRConstGeneric HIRConstGeneric::clone() const {
    switch ((*this).tag()) {
        case HIRConstGeneric::TAG_Infer: {
            auto& e = (*this).as_Infer();
            return e;
        }
        case HIRConstGeneric::TAG_Unevaluated: {
            auto& e = (*this).as_Unevaluated();
            return ::std::make_unique<HIRConstGenericUnevaluated>(e->clone());
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
    throw "";
}

::std::shared_ptr<HIRSimplePath> HIRPublicity::nonePath = ::std::make_shared<HIRSimplePath>(HIRSimplePath{"#", {}});

bool HIRPublicity::isVisible(const HIRSimplePath& p) const {
    DEBUG(*this << " " << p);
    // No path = global public
    if (!visPath) {
        return true;
    }
    // Empty simple path = full private
    if (*visPath == *nonePath) {
        return false;
    }
    // `p` must be a child of vis_path (i.e. starts with it)
    return p.startsWith(*visPath);
}

HIRTypeRef HIRFunction::makePtrTy(const Span& sp, const Monomorphiser& ms) const {
    HIRTypeDataFunctionPointer ft;
    ft.isUnsafe = this->unsafe;
    ft.isVariadic = this->variadic;
    ft.abi = this->abi;
    ft.rettype = ms.monomorphType(sp, this->returnType);
    ft.argTypes.reserve(this->fixedArgCount());
    for (size_t i = 0; i < this->fixedArgCount(); i++) {
        ft.argTypes.push_back(ms.monomorphType(sp, this->args[i].second));
    }
    return ms.typeInterner().function(std::move(ft));
}

HIRTypeRef fnPtrTupleConstructor(const Span& sp, const Monomorphiser& ms, HIRTypeRef retTy, const tTupleFields& fields) {
    HIRTypeDataFunctionPointer ft;
    ft.isUnsafe = false;
    ft.isVariadic = false;
    ft.abi = RcString::newInterned(ABI_RUST);
    ft.rettype = std::move(retTy);
    ft.argTypes.reserve(fields.size());
    for (const auto& fld : fields) {
        ft.argTypes.push_back(ms.monomorphType(sp, fld.ent));
    }
    return ms.typeInterner().function(std::move(ft));
}

size_t HIREnum::findVariant(const RcString& name) const {
    if (data.is_Value()) {
        const auto& e = data.as_Value();
        auto it = ::std::find_if(e.variants.begin(), e.variants.end(), [&](const auto& x) {
            return x.name == name;
        });
        if (it == e.variants.end()) {
            return SIZE_MAX;
        }
        return it - e.variants.begin();
    } else {
        const auto& e = data.as_Data();

        auto it = ::std::find_if(e.begin(), e.end(), [&](const auto& x) {
            return x.name == name;
        });
        if (it == e.end()) {
            return SIZE_MAX;
        }
        return it - e.begin();
    }
}

bool HIREnum::isValue() const {
    return this->data.is_Value();
}

U128 HIREnum::getDiscriminant(size_t idx) const {
    if (data.is_Value()) {
        return this->getValue(idx);
    }
    const auto& variants = data.as_Data();
    assert(idx < variants.size());
    return variants[idx].discriminantValue;
}

U128 HIREnum::getValue(size_t idx) const {
    if (data.is_Value()) {
        const auto& e = data.as_Value();
        assert(idx < e.variants.size());

        return e.variants[idx].val;
    } else {
        assert(!"TODO: Enum::get_value on non-value enum?");
        throw "";
    }
}

/*static*/ HIRCoreType HIREnum::getReprType(Repr r) {
    switch (r) {
        case HIREnum::Repr::Auto:
            return HIRCoreType::Isize;
            break;
        case HIREnum::Repr::Usize:
            return HIRCoreType::Usize;
            break;
        case HIREnum::Repr::U8:
            return HIRCoreType::U8;
            break;
        case HIREnum::Repr::U16:
            return HIRCoreType::U16;
            break;
        case HIREnum::Repr::U32:
            return HIRCoreType::U32;
            break;
        case HIREnum::Repr::U64:
            return HIRCoreType::U64;
            break;
        case HIREnum::Repr::U128:
            return HIRCoreType::U128;
            break;
        case HIREnum::Repr::Isize:
            return HIRCoreType::Isize;
            break;
        case HIREnum::Repr::I8:
            return HIRCoreType::I8;
            break;
        case HIREnum::Repr::I16:
            return HIRCoreType::I16;
            break;
        case HIREnum::Repr::I32:
            return HIRCoreType::I32;
            break;
        case HIREnum::Repr::I64:
            return HIRCoreType::I64;
            break;
        case HIREnum::Repr::I128:
            return HIRCoreType::I128;
            break;
    }
    throw "";
}

const HIRSimplePath& HIRCrate::getLangItemPath(const Span& sp, const char* name) const {
    auto it = this->langItems.find(name);
    if (it == this->langItems.end()) {
        ERROR(sp, E0000, "Undefined language item '" << name << "' required");
    }
    return it->second;
}

const HIRSimplePath& HIRCrate::getLangItemPathOpt(const char* name) const {
    static HIRSimplePath emptyPath;
    auto it = this->langItems.find(name);
    if (it == this->langItems.end()) {
        return emptyPath;
    }
    return it->second;
}

namespace {
    const HIRModule& getContainingModule(const HIRCrate& crate, const Span& sp, const HIRSimplePath& path, bool ignoreCrateName, bool ignoreLastNode) {
        ASSERT_BUG(sp, path.components().size() > 0u, "Invalid path (no nodes) - " << path);
        ASSERT_BUG(sp, path.components().size() > (ignoreLastNode ? 1u : 0u), "Invalid path (only one node with `ignore_last_node` - " << path);

        const HIRModule* mod;
        if (!ignoreCrateName && path.crateName() != crate.crateName) {
            ASSERT_BUG(sp, crate.extCrates.count(path.crateName()) > 0, "Crate '" << path.crateName() << "' not loaded for " << path);
            mod = &crate.extCrates.at(path.crateName()).data->rootModule;
        } else {
            mod = &crate.rootModule;
        }
        for (unsigned int i = 0; i < path.components().size() - (ignoreLastNode ? 2 : 1); i++) {
            const auto& pc = path.components()[i];
            auto it = mod->modItems.find(pc);
            if (it == mod->modItems.end()) {
                BUG(sp, "Couldn't find component " << i << " of " << path);
            }
            if (const auto* e = it->second->ent.opt_Module()) {
                mod = e;
            } else {
                BUG(sp, "Node " << i << " of path " << path << " wasn't a module");
            }
        }
        return *mod;
    }
}

const HIRMacroItem& HIRCrate::getMacroitemByPath(const Span& sp, const HIRSimplePath& path, bool ignoreCrateName, bool ignoreLastNode) const {
    const auto& mod = getContainingModule(*this, sp, path, ignoreCrateName, ignoreLastNode);

    auto it = mod.macroItems.find(ignoreLastNode ? path.components()[path.components().size() - 2] : path.components().back());
    if (it == mod.macroItems.end()) {
        BUG(sp, "Could not find macro name in " << path);
    }

    return it->second->ent;
}

const HIRTypeItem& HIRCrate::getTypeitemByPath(const Span& sp, const HIRSimplePath& path, bool ignoreCrateName, bool ignoreLastNode) const {
    const auto& mod = getContainingModule(*this, sp, path, ignoreCrateName, ignoreLastNode);

    auto it = mod.modItems.find(ignoreLastNode ? path.components()[path.components().size() - 2] : path.components().back());
    if (it == mod.modItems.end()) {
        BUG(sp, "Could not find type " << path);
    }

    return it->second->ent;
}

const HIRTypeItem* HIRCrate::getTypeitemByPathOpt(const HIRSimplePath& path) const {
    if (path.components().empty()) {
        return nullptr;
    }
    const HIRModule* mod;
    if (path.crateName() == this->crateName) {
        mod = &this->rootModule;
    } else {
        auto crateIt = this->extCrates.find(path.crateName());
        if (crateIt == this->extCrates.end()) {
            return nullptr;
        }
        mod = &crateIt->second.data->rootModule;
    }
    for (size_t i = 0; i + 1 < path.components().size(); i++) {
        auto it = mod->modItems.find(path.components()[i]);
        if (it == mod->modItems.end()) {
            return nullptr;
        }
        mod = it->second->ent.opt_Module();
        if (!mod) {
            return nullptr;
        }
    }
    auto it = mod->modItems.find(path.components().back());
    return it == mod->modItems.end() ? nullptr : &it->second->ent;
}

const HIRModule& HIRCrate::getModByPath(const Span& sp, const HIRSimplePath& path, bool ignoreLastNode /*=false*/, bool ignoreCrateName /*=false*/) const {
    if (ignoreLastNode) {
        ASSERT_BUG(sp, path.components().size() > 0, "get_mod_by_path received invalid path with ignore_last_node=true - " << path);
    }
    // Special handling for empty paths with `ignore_last_node`
    if (path.components().size() == (ignoreLastNode ? 1 : 0)) {
        if (!ignoreCrateName && path.crateName() != crateName) {
            ASSERT_BUG(sp, extCrates.count(path.crateName()) > 0, "Crate '" << path.crateName() << "' not loaded");
            return extCrates.at(path.crateName()).data->rootModule;
        } else {
            return this->rootModule;
        }
    } else {
        const auto& ti = this->getTypeitemByPath(sp, path, ignoreCrateName, ignoreLastNode);
        if (auto* e = ti.opt_Module()) {
            return *e;
        } else {
            if (ignoreLastNode) {
                BUG(sp, "Parent path of " << path << " didn't point to a module");
            } else {
                BUG(sp, "Module path " << path << " didn't point to a module");
            }
        }
    }
}

const HIRTrait& HIRCrate::getTraitByPath(const Span& sp, const HIRSimplePath& path) const {
    const auto& ti = this->getTypeitemByPath(sp, path);
    if (ti.is_Trait()) {
        auto& e = ti.as_Trait();
        return e;
    }
    else {
        BUG(sp, "Trait path " << path << " didn't point to a trait (" << ti.tagStr() << ")");
    }
}

::std::optional<size_t> HIRCrate::findMostSpecificTrait(const Span& sp, const ::std::vector<HIRSimplePath>& candidates) const {
    ::std::optional<size_t> selected;
    for (size_t candidateIndex = 0; candidateIndex < candidates.size(); candidateIndex++) {
        const auto& candidate = candidates[candidateIndex];
        const auto& trait = this->getTraitByPath(sp, candidate);
        bool isSubtraitOfAll = true;

        for (const auto& other : candidates) {
            if (candidate == other) {
                continue;
            }
            const bool hasSupertrait = ::std::any_of(trait.allParentTraits.begin(), trait.allParentTraits.end(), [&](const auto& parent) {
                return parent.path.path == other;
            });
            if (!hasSupertrait) {
                isSubtraitOfAll = false;
                break;
            }
        }

        if (!isSubtraitOfAll) {
            continue;
        }
        if (selected && candidates[*selected] != candidate) {
            return {};
        }
        if (!selected) {
            selected = candidateIndex;
        }
    }
    return selected;
}

const HIRStruct& HIRCrate::getStructByPath(const Span& sp, const HIRSimplePath& path) const {
    const auto& ti = this->getTypeitemByPath(sp, path);
    if (ti.is_Struct()) {
        auto& e = ti.as_Struct();
        return e;
    }
    else {
        BUG(sp, "Struct path " << path << " didn't point to a struct (" << ti.tagStr() << ")");
    }
}

const HIRUnion& HIRCrate::getUnionByPath(const Span& sp, const HIRSimplePath& path) const {
    const auto& ti = this->getTypeitemByPath(sp, path);
    if (ti.is_Union()) {
        auto& e = ti.as_Union();
        return e;
    }
    else {
        BUG(sp, "Path " << path << " didn't point to a union (" << ti.tagStr() << ")");
    }
}

const HIREnum& HIRCrate::getEnumByPath(const Span& sp, const HIRSimplePath& path, bool ignoreCrateName, bool ignoreLastNode) const {
    const auto& ti = this->getTypeitemByPath(sp, path, ignoreCrateName, ignoreLastNode);
    if (ti.is_Enum()) {
        auto& e = ti.as_Enum();
        return e;
    }
    else {
        BUG(sp, "Enum path " << path << " didn't point to an enum (" << ti.tagStr() << ")");
    }
}

const HIRValueItem& HIRCrate::getValitemByPath(const Span& sp, const HIRSimplePath& path, bool ignoreCrateName) const {
    if (path.crateName() == "#intrinsics") {
        ASSERT_BUG(sp, path.components().size() == 1, "");
        if (path.components().back() == "offset_of") {
            if (!intrinsicOffsetof.as_Function()) {
                auto* v = pool->make<HIRFunction>(HIRFunction{HIRFunction::Receiver::Free, HIRGenericParams{}, {}, types.primitive(HIRCoreType::Usize), {}});
                v->variadic = true;
                v->params.types.push_back(HIRTypeParamDef{RcString::newInterned("T"), types.infer(), false});
                v->params.paramKinds.pushBack(HIRGenericParamKind::Type);
                intrinsicOffsetof = HIRValueItem::make_Function(v);
            }
            return intrinsicOffsetof;
        }
        TODO(sp, "Get intrinsic " << path.components().back());
    }
    if (path.crateName() == this->crateName && path.components().size() == 1) {
        auto i = std::find_if(newValues.begin(), newValues.end(), [&](const auto& v) {
            return v.first == path.components().back();
        });
        if (i != newValues.end()) {
            return i->second->ent;
        }
    }
    const auto& mod = getContainingModule(*this, sp, path, ignoreCrateName, /*ignore_last_node=*/false);

    auto it = mod.valueItems.find(path.components().back());
    if (it == mod.valueItems.end()) {
        BUG(sp, "Could not find value name " << path);
    }

    // A `use` in the value namespace names the item it imports: `use m::f as
    // main;` makes the crate's `main` that import, and every consumer of a
    // value path wants what stands behind it. An enum variant is not one --
    // its import carries the variant index and is read as the import it is.
    if (const auto* imp = it->second->ent.opt_Import(); imp && !imp->isVariant && imp->path != path) {
        return this->getValitemByPath(sp, imp->path, ignoreCrateName);
    }

    return it->second->ent;
}

const HIRFunction& HIRCrate::getFunctionByPath(const Span& sp, const HIRSimplePath& path) const {
    const auto& ti = this->getValitemByPath(sp, path);
    if (ti.is_Function()) {
        return *ti.as_Function();
    }
    else {
        BUG(sp, "Function path " << path << " didn't point to an function (" << ti.tagStr() << ")");
    }
}

bool HIRCrate::functionTracksCaller(const Span& sp, const HIRPath& path, const HIRFunction& function) const {
    if (function.markings.trackCaller) {
        return true;
    }
    if (const auto* known = path.data.opt_UfcsKnown()) {
        const auto& trait = getTraitByPath(sp, known->trait.path);
        const auto item = trait.values.find(known->item);
        if (item != trait.values.end() && item->second.is_Function()) {
            return item->second.as_Function().markings.trackCaller;
        }
    }
    return false;
}

const HIRStatic& HIRCrate::getStaticByPath(const Span& sp, const HIRSimplePath& path) const {
    const auto& m = this->getModByPath(sp, path, /*ignore_last*/ true);
    auto it = m.valueItems.find(path.components().back());
    if (it != m.valueItems.end()) {
        ASSERT_BUG(sp, it->second->ent.is_Static(), "`static` path " << path << " didn't point to a static - " << it->second->ent.tagStr());
        return *it->second->ent.as_Static();
    }
    for (const auto& e : m.inlineStatics) {
        if (e.first == path.components().back()) {
            return *e.second;
        }
    }
    if (path.crateName() == this->crateName && path.components().size() == 1) {
        auto i = std::find_if(newValues.begin(), newValues.end(), [&](const auto& v) {
            return v.first == path.components().back();
        });
        if (i != newValues.end()) {
            return *i->second->ent.as_Static();
        }
    }
    BUG(sp, "`static` path " << path << " can't be found");
}

void HIRCrate::postLoadUpdate(const RcString& name) {
    // TODO: Do a pass across m_hir that
    // 1. Updates all absolute paths with the crate name
    // 2. Sets binding pointers where required
    // 3. Updates macros with the crate name
}

namespace {
    bool isUnboundedInfer(const HIRTypeData* type) {
        if (const auto* e = type->opt_Infer()) {
            return e->tyClass == HIRInferClass::None;
        } else {
            return false;
        }
    }

    class ImplMatcher: public HIRMatchGenerics {
        // Borrowed reused scratch (see matchesTypeRoot); nullptr = unbound.
        stl::Vector<HIRTypeRef>& implTypes;

    public:
        ImplMatcher(stl::Vector<HIRTypeRef>& buf, const HIRGenericParams& implGenerics)
            : implTypes(buf)
        {
            implTypes.clear();
            implTypes.zero(implGenerics.types.size());
        }

        HIRCompare matchTy(const HIRGenericRef& g, const HIRTypeData* ty, tCbResolveType resolveCb) override {
            assert(g.binding < implTypes.length());
            if (implTypes[g.binding]) {
                return implTypes[g.binding]->compareWithPlaceholders(Span(), ty, resolveCb);
            }
            implTypes.mut(g.binding) = ty;
            return HIRCompare::Equal;
        }

        HIRCompare matchVal(const HIRGenericRef& g, const HIRConstGeneric& sz) override {
            // TODO
            return HIRCompare::Equal;
        }

        HIRTypeRef mappedType(unsigned binding) const {
            return binding < implTypes.length() ? implTypes[binding] : HIRTypeRef();
        }
    };

    bool matchesTypeRoot(const HIRGenericParams& params, const HIRTypeData* implTy, const HIRTypeData* matchType, tCbResolveType tyRes) {
        // A nominal path deserialises without its pointer-valued binding
        // metadata. Its SimplePath is nevertheless complete and is exactly
        // what the impl index and matcher use. Only an unresolved UFCS path is
        // still too early to select an inherent impl.
        const auto* matchPath = matchType->opt_Path();
        if (isUnboundedInfer(matchType) || (matchPath && matchPath->binding.is_Unbound() && !matchPath->path.data.is_Generic())) {
            return false;
        }
        // Depth-indexed pool of reused binding buffers: ~1.7M allocations on
        // libcore came from a fresh vector per match. The depth index keeps a
        // re-entrant match (via the resolve callback) off the same buffer.
        static stl::Vector<HIRTypeRef> matcherBufs[8];
        static unsigned matcherDepth = 0;
        ASSERT_BUG(Span(), matcherDepth < 8, "impl matcher nested too deep");
        struct DepthGuard {
            unsigned& depth;

            ~DepthGuard() {
                depth--;
            }
        } depthGuard{matcherDepth};
        ImplMatcher m{matcherBufs[matcherDepth++], params};
        auto cmp = implTy->matchTestGenericsFuzz(Span(), matchType, tyRes, m);
        return cmp != HIRCompare::Unequal;
    }
}

bool HIRTraitImpl::matchesType(const HIRTypeData* type, tCbResolveType tyRes) const {
    // NOTE: Don't return any impls when the type is an unbouned ivar. Wouldn't be able to pick anything anyway
    // TODO: For `Unbound`, it could be valid, if the target is a generic.
    // - Pure infer could also be useful (for knowing if there's any other potential impls)

    // HACK: Assume an unbounded matches
    if (isUnboundedInfer(type)) {
        return true;
    }
    return matchesTypeRoot(params, this->type, type, tyRes);
}

bool HIRTypeImpl::matchesType(const HIRTypeData* type, tCbResolveType tyRes) const {
    return matchesTypeRoot(params, this->type, type, tyRes);
}

bool HIRMarkerImpl::matchesType(const HIRTypeData* type, tCbResolveType tyRes) const {
    return matchesTypeRoot(params, this->type, type, tyRes);
}

namespace {

    struct TypeOrdSpecificMixedOrdering {};

    ::Ordering typelistOrdSpecific(const Span& sp, const ThinVector<HIRTypeRef>& left, const ThinVector<HIRTypeRef>& right);
    ::Ordering typelistOrdSpecific(const Span& sp, const ::std::vector<HIRTypeRef>& left, const ::std::vector<HIRTypeRef>& right);

    ::Ordering arraySizeOrdSpecific(const Span& sp, const HIRArraySize& left, const HIRArraySize& right) {
        if (left == right) {
            return ::OrdEqual;
        }
        const bool leftOpen = left.is_Unevaluated();
        const bool rightOpen = right.is_Unevaluated();
        if (leftOpen != rightOpen) {
            return leftOpen ? ::OrdLess : ::OrdGreater;
        }
        if (leftOpen) {
            // Two independently named const parameters are equally general.
            // Relations between them are accounted for by the surrounding
            // impl matcher; neither is more specific on syntax alone.
            return ::OrdEqual;
        }
        BUG(sp, "Mismatched const values - " << left << " and " << right);
    }

    ::Ordering combineSpecificity(::Ordering left, ::Ordering right) {
        if (left == ::OrdEqual) {
            return right;
        }
        if (right == ::OrdEqual || left == right) {
            return left;
        }
        throw TypeOrdSpecificMixedOrdering{};
    }

    ::Ordering typeOrdSpecific(const Span& sp, const HIRTypeData* left, const HIRTypeData* right) {
        // TODO: What happens if you get `impl<T> Foo<T> for T` vs `impl<T,U> Foo<U> for T`

        // A generic can't be more specific than any other type we can see
        // - It's equally as specific as another Generic, so still false
        if (left->is_Generic()) {
            return right->is_Generic() ? ::OrdEqual : ::OrdLess;
        }
        // - A generic is always less specific than anything but itself (handled above)
        if (right->is_Generic()) {
            return ::OrdGreater;
        }

        if (left == right) {
            return ::OrdEqual;
        }

        switch ((*left).tag()) {
            case HIRTypeData::TAG_Generic: {
                throw "";
            }
            case HIRTypeData::TAG_Infer: {
                BUG(sp, "Hit infer");
                break;
            }
            case HIRTypeData::TAG_Diverge: {
                BUG(sp, "Hit diverge");
                break;
            }
            case HIRTypeData::TAG_NodeType: {
                BUG(sp, "Hit " << left);
                break;
            }
            case HIRTypeData::TAG_Primitive: {
                auto& le = (*left).as_Primitive();
                if (const auto* re = right->opt_Primitive()) {
                    if (le != *re) {
                        BUG(sp, "Mismatched types - " << left << " and " << right);
                    }
                    return ::OrdEqual;
                } else {
                    BUG(sp, "Mismatched types - " << left << " and " << right);
                }
                break;
            }
            case HIRTypeData::TAG_Path: {
                auto& le = (*left).as_Path();
                if (!right->is_Path() || le.path.data.tag() != right->as_Path().path.data.tag()) {
                    BUG(sp, "Mismatched types - " << left << " and " << right);
                }
                switch (le.path.data.tag()) {
                    case HIRPathData::TAG_Generic: {
                        auto& lpe = le.path.data.as_Generic();
                        auto& rpe = right->as_Path().path.data.as_Generic();
                        if (lpe.path != rpe.path) BUG(sp, "Mismatched types - " << left << " and " << right); return typelistOrdSpecific(sp, lpe.params.types, rpe.params.types);
                        break;
                    }
                    case HIRPathData::TAG_UfcsUnknown: {
                        break;
                    }
                    case HIRPathData::TAG_UfcsKnown: {
                        break;
                    }
                    case HIRPathData::TAG_UfcsInherent: {
                        break;
                    }
                }
                TODO(sp, "Path - " << le.path << " and " << right);
                break;
            }
            case HIRTypeData::TAG_TraitObject: {
                auto& le = (*left).as_TraitObject();
                ASSERT_BUG(sp, right->is_TraitObject(), "Mismatched types - " << left << " vs " << right);
                const auto& re = right->as_TraitObject();
                ASSERT_BUG(sp, le.trait.path.path == re.trait.path.path, "Mismatched types - " << left << " vs " << right);
                ASSERT_BUG(sp, le.markers.size() == re.markers.size(), "Mismatched types - " << left << " vs " << right);

                auto ord = typelistOrdSpecific(sp, le.trait.path.params.types, re.trait.path.params.types);
                if (ord != ::OrdEqual) {
                    return ord;
                }
                for (size_t i = 0; i < le.markers.size(); i++) {
                    ASSERT_BUG(sp, le.markers[i].path == re.markers[i].path, "Mismatched types - " << left << " vs " << right);
                    ord = typelistOrdSpecific(sp, le.markers[i].params.types, re.markers[i].params.types);
                    if (ord != ::OrdEqual) {
                        return ord;
                    }
                }
                return ::OrdEqual;
            }
            case HIRTypeData::TAG_ErasedType: {
                TODO(sp, "ErasedType - " << left);
                break;
            }
            case HIRTypeData::TAG_NamedFunction: {
                BUG(sp, "Hit function type");
                break;
            }
            case HIRTypeData::TAG_Function: {
                if (/*const auto* re =*/right->opt_Function()) {
                    if (left == right) {
                        return ::OrdEqual;
                    }
                    TODO(sp, "Function - " << left << " vs " << right);
                } else {
                    BUG(sp, "Mismatched types - " << left << " and " << right);
                }
                break;
            }
            case HIRTypeData::TAG_Tuple: {
                auto& le = (*left).as_Tuple();
                if (const auto* re = right->opt_Tuple()) {
                    return typelistOrdSpecific(sp, le, *re);
                } else {
                    BUG(sp, "Mismatched types - " << left << " and " << right);
                }
                break;
            }
            case HIRTypeData::TAG_Slice: {
                auto& le = (*left).as_Slice();
                if (const auto* re = right->opt_Slice()) {
                    return typeOrdSpecific(sp, le.inner, re->inner);
                } else {
                    BUG(sp, "Mismatched types - " << left << " and " << right);
                }
                break;
            }
            case HIRTypeData::TAG_Array: {
                auto& le = (*left).as_Array();
                if (const auto* re = right->opt_Array()) {
                    return combineSpecificity(typeOrdSpecific(sp, le.inner, re->inner), arraySizeOrdSpecific(sp, le.size, re->size));
                } else {
                    BUG(sp, "Mismatched types - " << left << " and " << right);
                }
                break;
            }
            case HIRTypeData::TAG_Pointer: {
                auto& le = (*left).as_Pointer();
                if (const auto* re = right->opt_Pointer()) {
                    if (le.type != re->type) {
                        BUG(sp, "Mismatched types - " << left << " and " << right);
                    }
                    return typeOrdSpecific(sp, le.inner, re->inner);
                } else {
                    BUG(sp, "Mismatched types - " << left << " and " << right);
                }
                break;
            }
            case HIRTypeData::TAG_Borrow: {
                auto& le = (*left).as_Borrow();
                if (const auto* re = right->opt_Borrow()) {
                    if (le.type != re->type) {
                        BUG(sp, "Mismatched types - " << left << " and " << right);
                    }
                    return typeOrdSpecific(sp, le.inner, re->inner);
                } else {
                    BUG(sp, "Mismatched types - " << left << " and " << right);
                }
                break;
            }
        }
        throw "Fell off end of type_ord_specific";
    }

    ::Ordering typelistOrdSpecific(const Span& sp, const ThinVector<HIRTypeRef>& le, const ThinVector<HIRTypeRef>& re) {
        auto rv = ::OrdEqual;
        assert(le.size() == re.size());
        for (unsigned int i = 0; i < le.size(); i++) {
            auto a = typeOrdSpecific(sp, le[i], re[i]);
            if (a != ::OrdEqual) {
                if (rv != ::OrdEqual && a != rv) {
                    DEBUG("Inconsistent ordering between type lists - i=" << i << " [" << le << "] vs [" << re << "]");
                    throw TypeOrdSpecificMixedOrdering{};
                }
                rv = a;
            }
        }
        return rv;
    }

    ::Ordering typelistOrdSpecific(const Span& sp, const ::std::vector<HIRTypeRef>& le, const ::std::vector<HIRTypeRef>& re) {
        auto rv = ::OrdEqual;
        assert(le.size() == re.size());
        for (unsigned int i = 0; i < le.size(); i++) {
            auto a = typeOrdSpecific(sp, le[i], re[i]);
            if (a != ::OrdEqual) {
                if (rv != ::OrdEqual && a != rv) {
                    DEBUG("Inconsistent ordering between type lists - i=" << i << " [" << le << "] vs [" << re << "]");
                    throw TypeOrdSpecificMixedOrdering{};
                }
                rv = a;
            }
        }
        return rv;
    }
}

namespace {
    void addBoundFromTrait(HIRTypeInterner& types, ::std::vector<HIRGenericBound>& rv, const HIRTypeData* type, const HIRTraitPath& curTrait, bool isTrivial) {
        static Span sp;
        assert(curTrait.traitPtr);
        const auto& tr = *curTrait.traitPtr;
        auto monomorphCb = MonomorphStatePtr(types, type, &curTrait.path.params, nullptr);

        for (const auto& traitPathRaw : tr.allParentTraits) {
            // 1. Monomorph
            auto traitPathMono = monomorphCb.monomorphTraitpath(sp, traitPathRaw, false);
            // 2. Add
            rv.push_back(HIRGenericBound::make_TraitBound({type, mv$(traitPathMono), HIRBoundConstness::Never, isTrivial}));
        }

        // TODO: Add traits from `Self: Foo` bounds?
        // TODO: Move associated types to the source trait.
    }

    ::std::vector<HIRGenericBound> flattenBounds(HIRTypeInterner& types, const ::std::vector<HIRGenericBound>& bounds) {
        ::std::vector<HIRGenericBound> rv;
        for (const auto& b : bounds) {
            rv.push_back(b.clone());
            if (const auto* be = b.opt_TraitBound()) {
                addBoundFromTrait(types, rv, be->type, be->trait, be->isTrivial);
            }
        }
        ::std::sort(rv.begin(), rv.end(), [](const auto& a, const auto& b) {
            return ::ord(a, b) == OrdLess;
        });
        return rv;
    }

    bool matchImplHead(const Span& sp, const HIRTraitImpl& pattern, const HIRTraitImpl& value, ImplMatcher& matcher) {
        auto resolve = HIRResolvePlaceholdersNop();
        if (pattern.type->matchTestGenericsFuzz(sp, value.type, resolve, matcher) == HIRCompare::Unequal) {
            return false;
        }
        return pattern.traitArgs.matchTestGenericsFuzz(sp, value.traitArgs, resolve, matcher) != HIRCompare::Unequal;
    }

    class ImplHeadMonomorphiser: public Monomorphiser {
        const ImplMatcher& matcher;
        mutable bool complete_ = true;

    public:
        ImplHeadMonomorphiser(HIRTypeInterner& types, const ImplMatcher& matcher)
            : Monomorphiser(types)
            , matcher(matcher)
        {
        }

        HIRTypeRef getType(const Span&, const HIRGenericRef& generic) const override {
            if (generic.group() == 0) {
                auto mapped = matcher.mappedType(generic.binding);
                if (mapped) {
                    return mapped;
                }
            }
            complete_ = false;
            return types.generic(generic.name, generic.binding);
        }

        HIRConstGeneric getValue(const Span&, const HIRGenericRef& generic) const override {
            complete_ = false;
            return HIRConstGeneric(generic);
        }

        bool complete() const {
            return complete_;
        }
    };

    bool mappedBoundsImplied(const Span& sp, HIRTypeInterner& types, const HIRTraitImpl& child, const HIRTraitImpl& parent, const ImplMatcher& parentMatcher) {
        const auto childBounds = flattenBounds(types, child.params.bounds);
        const auto parentBounds = flattenBounds(types, parent.params.bounds);
        ImplHeadMonomorphiser monomorph(types, parentMatcher);

        for (const auto& bound : parentBounds) {
            const auto* traitBound = bound.opt_TraitBound();
            if (!traitBound) {
                return false;
            }

            auto type = monomorph.monomorphType(sp, traitBound->type);
            auto trait = monomorph.monomorphTraitpath(sp, traitBound->trait, true);
            if (!monomorph.complete()) {
                return false;
            }

            const bool found = std::any_of(childBounds.begin(), childBounds.end(), [&](const HIRGenericBound& candidate) {
                const auto* childTrait = candidate.opt_TraitBound();
                return childTrait && childTrait->type == type && childTrait->trait == trait && childTrait->constness == traitBound->constness;
            });
            if (!found) {
                return false;
            }
        }
        return true;
    }
}

bool HIRTraitImpl::moreSpecificThan(HIRTypeInterner& types, const HIRTraitImpl& other) const {
    static const Span _sp;
    const Span& sp = _sp;
    TRACE_FUNCTION;

    // >> https://github.com/rust-lang/rfcs/blob/master/text/1210-impl-specialization.md#defining-the-precedence-rules
    // 1. If this->m_type is less specific than other.m_type: return false
    try {
        // If any in te.impl->m_params is less specific than oe.impl->m_params: return false
        auto ord = typelistOrdSpecific(sp, this->traitArgs.types, other.traitArgs.types);
        if (ord != ::OrdEqual) {
            DEBUG("- Trait arguments " << (ord == ::OrdLess ? "less" : "more") << " specific");
            return ord == ::OrdGreater;
        }

        ord = typeOrdSpecific(sp, this->type, other.type);
        // If `*this` < `other` : false
        if (ord != ::OrdEqual) {
            DEBUG("- Type " << this->type << " " << (ord == ::OrdLess ? "less" : "more") << " specific than " << other.type);
            return ord == ::OrdGreater;
        }
    } catch (const TypeOrdSpecificMixedOrdering& e) {
        BUG(sp, "Mixed ordering in more_specific_than");
    }

    // The syntax-only ordering above treats every generic as equally open.
    // It therefore cannot see that `(T, T)` accepts a strict subset of the
    // values accepted by `(T, U)`. Match both impl heads while preserving each
    // pattern generic's assignments. A one-way match is a strict head
    // relation; the parent's predicates are then compared after applying that
    // assignment, so `T: Clone, U: Clone` correctly collapses to one bound.
    stl::Vector<HIRTypeRef> parentMappings;
    ImplMatcher parentMatcher(parentMappings, other.params);
    const bool parentMatchesChild = matchImplHead(sp, other, *this, parentMatcher);

    stl::Vector<HIRTypeRef> childMappings;
    ImplMatcher childMatcher(childMappings, params);
    const bool childMatchesParent = matchImplHead(sp, *this, other, childMatcher);

    if (parentMatchesChild != childMatchesParent) {
        return parentMatchesChild && mappedBoundsImplied(sp, types, *this, other, parentMatcher);
    }

    //}
    // 3. Compare bound set, if there is a rule in oe that is missing from te; return false
    // TODO: Cache these lists (calculate after outer typecheck?)
    auto boundsT = flattenBounds(types, params.bounds);
    auto boundsO = flattenBounds(types, other.params.bounds);

    DEBUG("bounds_t = " << boundsT);
    DEBUG("bounds_o = " << boundsO);

    // If there are less bounds in this impl, it can't be more specific.
    if (boundsT.size() < boundsO.size()) {
        DEBUG("Bound count");
        return false;
    }

    auto itT = boundsT.begin();
    auto itO = boundsO.begin();
    bool isEqual = true;
    while (itT != boundsT.end() && itO != boundsO.end()) {
        auto cmp = ::ord(*itT, *itO);
        // Equal bounds? advance both
        if (cmp == OrdEqual) {
            ++itT;
            ++itO;
            continue;
        }

        // If the two bounds are similar
        if (itT->tag() == itO->tag() && itT->is_TraitBound()) {
            const auto& bT = itT->as_TraitBound();
            const auto& bO = itO->as_TraitBound();
            // Check if the type is equal
            if (bT.type == bO.type && bT.trait.path.path == bO.trait.path.path) {
                const auto& paramsT = bT.trait.path.params;
                const auto& paramsO = bO.trait.path.params;
                switch (typelistOrdSpecific(sp, paramsT.types, paramsO.types)) {
                    case ::OrdLess:
                        return false;
                    case ::OrdGreater:
                        return true;
                    case ::OrdEqual:
                        break;
                }
                // TODO: Find cases where there's `T: Foo<T>` and `T: Foo<U>`
                for (unsigned int i = 0; i < paramsT.types.size(); i++) {
                    if (paramsT.types[i] != paramsO.types[i] && paramsT.types[i] == bT.type) {
                        return true;
                    }
                }
                TODO(sp, *itT << " ?= " << *itO);
            }
        }

        if (cmp == OrdLess) {
            isEqual = false;
            ++itT;
        } else {
            //++ it_o;
            DEBUG(*itT << " ?= " << *itO << " : " << cmp);
            return false;
        }
    }
    if (itT != boundsT.end()) {
        DEBUG("Remaining local bounds - " << *itT);
        return true;
    } else {
        DEBUG("Out of local bounds, equal or less specific");
        return !isEqual;
    }
}

namespace {

    struct ImplTyMatcher: public HIRMatchGenerics, public Monomorphiser {
        ::std::vector<::std::optional<HIRTypeRef>> implTys;
        ::std::vector<::std::optional<HIRConstGeneric>> implVals;

        explicit ImplTyMatcher(HIRTypeInterner& types)
            : Monomorphiser(types)
        {
        }

        HIRCompare matchTy(const HIRGenericRef& g, const HIRTypeData* ty, tCbResolveType _resolve_cb) override {
            assert(g.binding < implTys.size());
            if (implTys.at(g.binding)) {
                DEBUG("Compare " << ty << " and " << *implTys.at(g.binding));
                return (ty == *implTys.at(g.binding) ? HIRCompare::Equal : HIRCompare::Unequal);
            } else {
                implTys.at(g.binding) = ty;
                return HIRCompare::Equal;
            }
        }

        HIRCompare matchVal(const HIRGenericRef& g, const HIRConstGeneric& sz) override {
            assert(g.binding < implVals.size());
            if (implVals.at(g.binding)) {
                DEBUG("Compare " << sz << " and " << *implVals.at(g.binding));
                return (sz == *implVals.at(g.binding) ? HIRCompare::Equal : HIRCompare::Unequal);
            } else {
                implVals.at(g.binding) = sz.clone();
                return HIRCompare::Equal;
            }
        }

        HIRTypeRef getType(const Span& sp, const HIRGenericRef& g) const override {
            ASSERT_BUG(sp, g.group() == 0, "");
            ASSERT_BUG(sp, g.idx() < implTys.size(), "");
            if (!implTys[g.idx()]) {
                DEBUG("get_type - not populated, " << g);
                return types.generic(RcString(FMT("placeholder_" << &implTys << "_" << g.idx())), HIRGenericRef(RcString(), GENERICPlaceholder, g.idx()).binding);
            }
            return *implTys[g.idx()];
        }

        HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& g) const override {
            ASSERT_BUG(sp, g.group() == 0, "");
            ASSERT_BUG(sp, g.idx() < implVals.size(), "");
            ASSERT_BUG(sp, implVals[g.idx()], "");
            return implVals[g.idx()]->clone();
        }

        void reinit(const HIRGenericParams& params) {
            this->implTys.clear();
            this->implVals.clear();
            this->implTys.resize(params.types.size());
            this->implVals.resize(params.values.size());
        }

        void fmt(::std::ostream& os) const {
            for (const auto& p : this->implTys) {
                if (p) {
                    os << *p;
                } else {
                    os << "?";
                }
                os << ",";
            }
            for (const auto& p : this->implVals) {
                if (p) {
                    os << *p;
                } else {
                    os << "?";
                }
                os << ",";
            }
        }
    };
}

// Returns `true` if the two impls overlap in the types they will accept
bool HIRTraitImpl::overlapsWith(const HIRCrate& crate, const HIRTraitImpl& other) const {
    // TODO: Pre-calculate impl trees (with pointers to parent impls)
    struct H {
        static bool typesOverlap(const HIRPathParams& a, const HIRPathParams& b) {
            for (unsigned int i = 0; i < ::std::min(a.types.size(), b.types.size()); i++) {
                if (!H::typesOverlap(a.types[i], b.types[i])) {
                    return false;
                }
            }
            return true;
        }

        static bool typesOverlapPath(const HIRPath& a, const HIRPath& b) {
            if (a.data.tag() != b.data.tag()) {
                return false;
            }
            switch (a.data.tag()) {
                case HIRPathData::TAG_Generic: {
                    auto& ape = a.data.as_Generic();
                    auto& bpe = b.data.as_Generic();
                    if (ape.path != bpe.path) return false; return H::typesOverlap(ape.params, bpe.params);
                    break;
                }
                case HIRPathData::TAG_UfcsUnknown: {
                    break;
                }
                case HIRPathData::TAG_UfcsKnown: {
                    break;
                }
                case HIRPathData::TAG_UfcsInherent: {
                    break;
                }
            }
            DEBUG("TODO: Path - " << a << " and " << b);
            return false;
        }

        static bool typesOverlap(const HIRTypeData* a, const HIRTypeData* b) {
            static Span sp;
            if (a == b) {
                return true;
            }
            if (a->is_Generic() || b->is_Generic()) {
                return true;
            }
            // TODO: Unbound/Opaque paths?
            if (a->tag() != b->tag()) {
                return false;
            }
            switch ((*a).tag()) {
                case HIRTypeData::TAG_Generic: {
                    break;
                }
                case HIRTypeData::TAG_Infer: {
                    break;
                }
                case HIRTypeData::TAG_Diverge: {
                    break;
                }
                case HIRTypeData::TAG_NodeType: {
                    BUG(sp, "Hit node-magic type (closure/generator/async) - " << a << " " << b);
                    break;
                }
                case HIRTypeData::TAG_Primitive: {
                    auto& ae = (*a).as_Primitive();
                    auto& be = (*b).as_Primitive();
                    if (ae != be) {
                        return false;
                    }
                    break;
                }
                case HIRTypeData::TAG_Path: {
                    auto& ae = (*a).as_Path();
                    auto& be = (*b).as_Path();
                    return typesOverlapPath(ae.path, be.path);
                    //TODO(sp, "Path - " << ae.path << " and " << be.path);
                    break;
                }
                case HIRTypeData::TAG_TraitObject: {
                    auto& ae = (*a).as_TraitObject();
                    auto& be = (*b).as_TraitObject();
                    if (ae.trait.path.path != be.trait.path.path) {
                        return false;
                    }
                    if (!H::typesOverlap(ae.trait.path.params, be.trait.path.params)) {
                        return false;
                    }
                    // Marker traits only overlap if the lists are the same (with overlap)
                    if (ae.markers.size() != be.markers.size()) {
                        return false;
                    }
                    for (size_t i = 0; i < ae.markers.size(); i++) {
                        if (ae.markers[i].path != be.markers[i].path) {
                            return false;
                        }
                        if (!H::typesOverlap(ae.markers[i].params, be.markers[i].params)) {
                            return false;
                        }
                    }
                    return true;
                }
                case HIRTypeData::TAG_ErasedType: {
                    TODO(sp, "ErasedType - " << a);
                    break;
                }
                case HIRTypeData::TAG_NamedFunction: {
                    auto& ae = (*a).as_NamedFunction();
                    auto& be = (*b).as_NamedFunction();
                    return typesOverlapPath(ae.path, be.path);
                }
                case HIRTypeData::TAG_Function: {
                    auto& ae = (*a).as_Function();
                    auto& be = (*b).as_Function();
                    if (ae.isUnsafe != be.isUnsafe) {
                        return false;
                    }
                    if (ae.abi != be.abi) {
                        return false;
                    }
                    if (ae.argTypes.size() != be.argTypes.size()) {
                        return false;
                    }
                    for (unsigned int i = 0; i < ae.argTypes.size(); i++) {
                        if (!H::typesOverlap(ae.argTypes[i], be.argTypes[i])) {
                            return false;
                        }
                    }
                    if (!H::typesOverlap(ae.rettype, be.rettype)) {
                        return false;
                    }
                    break;
                }
                case HIRTypeData::TAG_Tuple: {
                    auto& ae = (*a).as_Tuple();
                    auto& be = (*b).as_Tuple();
                    if (ae.size() != be.size()) {
                        return false;
                    }
                    for (unsigned int i = 0; i < ae.size(); i++) {
                        if (!H::typesOverlap(ae[i], be[i])) {
                            return false;
                        }
                    }
                    break;
                }
                case HIRTypeData::TAG_Slice: {
                    auto& ae = (*a).as_Slice();
                    auto& be = (*b).as_Slice();
                    return H::typesOverlap(ae.inner, be.inner);
                }
                case HIRTypeData::TAG_Array: {
                    auto& ae = (*a).as_Array();
                    auto& be = (*b).as_Array();
                    if (ae.size != be.size) {
                        return false;
                    }
                    return H::typesOverlap(ae.inner, be.inner);
                }
                case HIRTypeData::TAG_Pointer: {
                    auto& ae = (*a).as_Pointer();
                    auto& be = (*b).as_Pointer();
                    if (ae.type != be.type) {
                        return false;
                    }
                    return H::typesOverlap(ae.inner, be.inner);
                }
                case HIRTypeData::TAG_Borrow: {
                    auto& ae = (*a).as_Borrow();
                    auto& be = (*b).as_Borrow();
                    if (ae.type != be.type) {
                        return false;
                    }
                    return H::typesOverlap(ae.inner, be.inner);
                }
            }
            return true;
        }
    };

    // Quick Check: If the types are equal, they do overlap
    if (this->type == other.type && this->traitArgs == other.traitArgs) {
        return true;
    }

    // 1. Are the impl types of the same form (or is one generic)
    if (!H::typesOverlap(this->type, other.type)) {
        return false;
    }
    if (!H::typesOverlap(this->traitArgs, other.traitArgs)) {
        return false;
    }

    DEBUG("TODO: Handle potential overlap (when not exactly equal)");
    Span sp;

    // TODO: Use `type_ord_specific` but treat any case of mixed ordering as this returning `false`
    try {
        typeOrdSpecific(sp, this->type, other.type);
        typelistOrdSpecific(sp, this->traitArgs.types, other.traitArgs.types);
    } catch (const TypeOrdSpecificMixedOrdering& /*e*/) {
        return false;
    }

    // TODO: Detect `impl<T> Foo<T> for Bar<T>` vs `impl<T> Foo<&T> for Bar<T>`
    // > Create values for impl params from the type, then check if the trait params are compatible
    // > Requires two lists, and telling which one to use by the end
    auto cbIdent = HIRResolvePlaceholdersNop();
    bool isReversed = false;
    ImplTyMatcher matcher(crate.types);
    matcher.reinit(this->params);
    if (!this->type->matchTestGenerics(sp, other.type, cbIdent, matcher)) {
        DEBUG("- Type mismatch, try other ordering");
        isReversed = true;
        matcher.reinit(other.params);
        if (!other.type->matchTestGenerics(sp, this->type, cbIdent, matcher)) {
            DEBUG("- Type mismatch in both orderings");
            return false;
        }
        if (other.traitArgs.matchTestGenericsFuzz(sp, this->traitArgs, cbIdent, matcher) != HIRCompare::Equal) {
            DEBUG("- Params mismatch");
            return false;
        }
        // Matched with second ordering
    } else if (this->traitArgs.matchTestGenericsFuzz(sp, other.traitArgs, cbIdent, matcher) != HIRCompare::Equal) {
        DEBUG("- Param mismatch, try other ordering");
        isReversed = true;
        matcher.reinit(other.params);
        if (!other.type->matchTestGenerics(sp, this->type, cbIdent, matcher)) {
            DEBUG("- Type mismatch in alt ordering");
            return false;
        }
        if (other.traitArgs.matchTestGenericsFuzz(sp, this->traitArgs, cbIdent, matcher) != HIRCompare::Equal) {
            DEBUG("- Params mismatch in alt ordering");
            return false;
        }
        // Matched with second ordering
    } else {
        // Matched with first ordering
    }

    struct H2 {
        static const HIRTypeData* monomorph(const Span& sp, const HIRTypeData* inTy, const Monomorphiser& ms, HIRTypeRef& tmp) {
            if (!monomorphiseTypeNeeded(inTy)) {
                return inTy;
            } else {
                tmp = ms.monomorphType(sp, inTy);
                // TODO: EAT?
                return tmp;
            }
        }

        static const HIRTraitPath& monomorph(const Span& sp, const HIRTraitPath& in, const Monomorphiser& ms, HIRTraitPath& tmp) {
            if (!monomorphiseTraitpathNeeded(in)) {
                return in;
            } else {
                tmp = ms.monomorphTraitpath(sp, in, true);
                // TODO: EAT?
                return tmp;
            }
        }

        static bool checkBounds(const HIRCrate& crate, const HIRTraitImpl& id, const Monomorphiser& ms, const HIRTraitImpl& gSrc) {
            TRACE_FUNCTION;
            static Span sp;
            for (const auto& tb : id.params.bounds) {
                DEBUG(tb);
                if (tb.is_TraitBound()) {
                    HIRTypeRef tmpTy;
                    HIRTraitPath tmpTp;
                    const auto& ty = H2::monomorph(sp, tb.as_TraitBound().type, ms, tmpTy);
                    const auto& trait = H2::monomorph(sp, tb.as_TraitBound().trait, ms, tmpTp);
                    ;

                    // Determine if `ty` would be bounded (it's an ATY or generic)
                    if (ty->is_Generic()) {
                        bool found = false;
                        for (const auto& bound : gSrc.params.bounds) {
                            if (const auto* be = bound.opt_TraitBound()) {
                                if (be->type != ty) {
                                    continue;
                                }
                                if (be->trait != trait) {
                                    continue;
                                }
                                found = true;
                            }
                        }
                        if (!found) {
                            DEBUG("No matching bound for " << ty << " : " << trait << " in source bounds - " << gSrc.params.fmtBounds());
                            return false;
                        }
                    } else if (((*ty).is_Path() && ((*ty).as_Path().binding.is_Opaque()))) {
                        TODO(sp, "Check bound " << ty << " : " << trait << " in source bounds or trait bounds");
                    } else {
                        // Search the crate for an impl
                        auto cbIdent = HIRResolvePlaceholdersNop();
                        bool rv = crate.findTraitImpls(trait.path.path, ty, cbIdent, [&](const HIRTraitImpl& ti) -> bool {
                            DEBUG("impl" << ti.params.fmtArgs() << " " << trait.path.path << ti.traitArgs << " for " << ti.type << ti.params.fmtBounds());

                            ImplTyMatcher matcher(crate.types);
                            matcher.reinit(ti.params);
                            // 1. Triple-check the type matches (and get generics)
                            if (!ti.type->matchTestGenerics(sp, ty, cbIdent, matcher)) {
                                return false;
                            }
                            // 2. Check trait params
                            assert(trait.path.params.types.size() == ti.traitArgs.types.size());
                            for (size_t i = 0; i < trait.path.params.types.size(); i++) {
                                if (!ti.traitArgs.types[i]->matchTestGenerics(sp, trait.path.params.types[i], cbIdent, matcher)) {
                                    return false;
                                }
                            }
                            // 3. Check bounds on the impl
                            if (!H2::checkBounds(crate, ti, matcher, gSrc)) {
                                return false;
                            }
                            // 4. Check ATY bounds on the trait path
                            for (const auto& atyb : trait.typeBounds) {
                                if (ti.types.count(atyb.first) == 0) {
                                    DEBUG("Associated type '" << atyb.first << "' not in trait impl, assuming good");
                                } else {
                                    const auto& aty = ti.types.at(atyb.first);
                                    if (!aty.data->matchTestGenerics(sp, atyb.second.type, cbIdent, matcher)) {
                                        return false;
                                    }
                                }
                            }
                            // All those pass? It's good.
                            return true;
                        });
                        if (!rv) {
                            return false;
                        }
                    }
                } else {
                    // TODO: Other bound types?
                }
            }
            // No bounds failed, it's good
            return true;
        }
    };

    // The two impls could overlap, pending on trait bounds
    if (isReversed) {
        DEBUG("(reversed) impl params " << FMT_CB(os, matcher.fmt(os);));
        // Check bounds on `other` using these params
        // TODO: Take a callback that does the checks. Or somehow return a "maybe overlaps" result?
        return H2::checkBounds(crate, other, matcher, *this);
    } else {
        DEBUG("impl params " << FMT_CB(os, matcher.fmt(os);));
        // Check bounds on `*this`
        return H2::checkBounds(crate, *this, matcher, other);
    }
}

namespace {
    template <typename List, typename Callback>
    bool findImplsList(const List& implList, const HIRTypeData* type, tCbResolveType tyRes, Callback& callback) {
        for (const auto& impl : implList) {
            if (impl->matchesType(type, tyRes)) {
                if (callback.visit(*impl)) {
                    return true;
                }
            }
        }
        return false;
    }
}

namespace {
    bool findTraitImplsInt(const HIRCrate& crate, const HIRSimplePath& trait, const HIRTypeData* type, tCbResolveType tyRes, HIRTraitImplCallback& callback) {
        auto it = crate.traitImpls.find(trait);
        if (it != crate.traitImpls.end()) {
            // 1. Find named impls (associated with named types)
            if (const auto* implList = it->second.getListForType(type)) {
                if (findImplsList(*implList, type, tyRes, callback)) {
                    return true;
                }
            }
            // - If the type is an ivar, search all types
            if (type->is_Infer() && !type->as_Infer().isLit()) {
                DEBUG("Search all lists");
                for (const auto& list : it->second.named) {
                    if (findImplsList(list.second, type, tyRes, callback)) {
                        return true;
                    }
                }
            }

            // 2. Search fully generic list.
            if (findImplsList(it->second.generic, type, tyRes, callback)) {
                return true;
            }
        }

        return false;
    }

}

bool HIRCrate::findTraitImplsCb(const HIRSimplePath& trait, const HIRTypeData* type, tCbResolveType tyRes, HIRTraitImplCallback& callback) const {
    if (this->allTraitImpls.size() > 0) {
        auto it = this->allTraitImpls.find(trait);
        if (it != this->allTraitImpls.end()) {
            // 1. Find named impls (associated with named types)
            if (const auto* implList = it->second.getListForType(type)) {
                if (findImplsList(*implList, type, tyRes, callback)) {
                    return true;
                }
            }
            // - If the type is an ivar, search all types
            if (type->is_Infer() && !type->as_Infer().isLit()) {
                DEBUG("Search all lists");
                for (const auto& list : it->second.named) {
                    if (findImplsList(list.second, type, tyRes, callback)) {
                        return true;
                    }
                }
            }

            // 2. Search fully generic list.
            if (findImplsList(it->second.generic, type, tyRes, callback)) {
                return true;
            }
        }

        return false;
    }

    // TODO: Determine the source crates for this type and trait (coherence) and only search those
    if (findTraitImplsInt(*this, trait, type, tyRes, callback)) {
        return true;
    }
    for (const auto& ec : this->extCrates) {
        if (findTraitImplsInt(*ec.second.data, trait, type, tyRes, callback)) {
            return true;
        }
    }
    return false;
}

namespace {
    bool findAutoTraitImplsInt(const HIRCrate& crate, const HIRSimplePath& trait, const HIRTypeData* type, tCbResolveType tyRes, HIRMarkerImplCallback& callback) {
        auto it = crate.markerImpls.find(trait);
        if (it != crate.markerImpls.end()) {
            // 1. Find named impls (associated with named types)
            if (const auto* implList = it->second.getListForType(type)) {
                if (findImplsList(*implList, type, tyRes, callback)) {
                    return true;
                }
            }

            // 2. Search fully generic list.
            if (findImplsList(it->second.generic, type, tyRes, callback)) {
                return true;
            }
        }

        return false;
    }
}

bool HIRCrate::findAutoTraitImplsCb(const HIRSimplePath& trait, const HIRTypeData* type, tCbResolveType tyRes, HIRMarkerImplCallback& callback) const {
    if (this->allMarkerImpls.size() > 0) {
        auto it = this->allMarkerImpls.find(trait);
        if (it != this->allMarkerImpls.end()) {
            // 1. Find named impls (associated with named types)
            if (const auto* implList = it->second.getListForType(type)) {
                if (findImplsList(*implList, type, tyRes, callback)) {
                    return true;
                }
            }

            // 2. Search fully generic list.
            if (findImplsList(it->second.generic, type, tyRes, callback)) {
                return true;
            }
        }

        return false;
    }

    if (findAutoTraitImplsInt(*this, trait, type, tyRes, callback)) {
        return true;
    }
    for (const auto& ec : this->extCrates) {
        if (findAutoTraitImplsInt(*ec.second.data, trait, type, tyRes, callback)) {
            return true;
        }
    }
    return false;
}

namespace {
    bool findTypeImplsInt(const HIRCrate& crate, const HIRTypeData* type, tCbResolveType tyRes, HIRTypeImplCallback& callback) {
        // 1. Find named impls (associated with named types)
        if (const auto* implList = crate.typeImpls.getListForType(type)) {
            if (findImplsList(*implList, type, tyRes, callback)) {
                return true;
            }
        }

        // 2. Search fully generic list?
        if (findImplsList(crate.typeImpls.generic, type, tyRes, callback)) {
            return true;
        }

        return false;
    }
}

bool HIRCrate::findTypeImplsCb(const HIRTypeData* type, tCbResolveType tyRes, HIRTypeImplCallback& callback) const {
    if (allTraitImpls.size() > 0) {
        // 1. Find named impls (associated with named types)
        if (const auto* implList = this->allTypeImpls.getListForType(type)) {
            if (findImplsList(*implList, type, tyRes, callback)) {
                return true;
            }
        }

        // 2. Search fully generic list?
        if (findImplsList(this->allTypeImpls.generic, type, tyRes, callback)) {
            return true;
        }

        return false;
    }
    // TODO: Determine the source crate for this type (coherence) and only search that

    // > Current crate
    if (findTypeImplsInt(*this, type, tyRes, callback)) {
        return true;
    }
    for (const auto& ec : this->extCrates) {
        if (findTypeImplsInt(*ec.second.data, type, tyRes, callback)) {
            return true;
        }
    }
    return false;
}

const MIRFunction* HIRCrate::getOrGenMir(const WireBoard& wb, const HIRItemPath& ip, const HIRExprPtr& ep, const HIRFunction::argsT& args, HIRTypeRef& retTy) const {
    if (!ep) {
        // No HIR, so has to just have MIR - from a extern crate most likely
        ASSERT_BUG(Span(), ep.mir, "No HIR (!ep) and no MIR (!ep.m_mir) for " << ip);
        return &*ep.mir;
    } else {
        if (!ep.mir) {
            TRACE_FUNCTION_F(ip);
            ASSERT_BUG(Span(), ep.state, "No ExprState for " << ip);

            auto& epMut = const_cast<HIRExprPtr&>(ep);

            HIRGenericPath currentTrait;
            if (ep.state->currentTraitImpl) {
                currentTrait.path = ep.state->currentTraitPath;
                currentTrait.params = ep.state->currentTraitImpl->traitArgs.clone();
                // Lazy processing can be requested from Resolve UFCS Outer,
                // before the whole-crate Self-expansion pass has run.  Give
                // this body and its signature the same owner substitution.
                ConvertHIRExpandAliasesSelfExpr(*this, ep.state->currentTraitImpl->type, const_cast<HIRFunction::argsT&>(args), retTy, epMut);
            }

            // TODO: Ensure that all referenced items have constants evaluated
            if (ep.state->stage < HIRExprState::Stage::ConstEval) {
                if (ep.state->stage == HIRExprState::Stage::ConstEvalRequest) {
                    ERROR(Span(), E0000, "Loop in constant evaluation");
                }
                ep.state->stage = HIRExprState::Stage::ConstEvalRequest;
                ConvertHIRResolveUFCSExpr(wb, *this, ip, epMut);
                ConvertHIRConstantEvaluateExpr(wb, *this, ip, epMut);
                ep.state->stage = HIRExprState::Stage::ConstEval;
            }

            // Ensure typechecked
            if (ep.state->stage < HIRExprState::Stage::Typecheck) {
                if (ep.state->stage == HIRExprState::Stage::TypecheckRequest) {
                    ERROR(Span(), E0000, "Loop in constant evaluation");
                }
                ep.state->stage = HIRExprState::Stage::TypecheckRequest;

                // TODO: Set debug/timing stage
                // - Can store that on the Expr, OR get it from the item path
                TypeckModuleState ms{wb};
                //ms.prepare_from_path( ip );   // <- Ideally would use this, but it's a lot of code for one usage
                ms.implGenerics = ep.state->implGenerics;
                ms.itemGenerics = ep.state->itemGenerics;
                ms.currentTrait = ep.state->currentTraitImpl ? &currentTrait : nullptr;
                ms.currentTraitImpl = ep.state->currentTraitImpl;
                ms.traits = ep.state->traits;
                ms.modPaths.push_back(ep.state->modPath);
                TypecheckCode(ms, const_cast<HIRFunction::argsT&>(args), retTy, epMut);
                // NOTE: This is already set by the above function
                ASSERT_BUG(Span(), ep.state->stage == HIRExprState::Stage::Typecheck, "Typecheck_Code didn't set stage");
            }
            if (ep.state->stage < HIRExprState::Stage::PostTypecheck) {
                HIRExpandAnnotateUsageExpr(wb, *this, ip, epMut);
                HIRExpandStaticBorrowConstantsMarkExpr(wb, *this, ip, epMut);
            }
            if (ep.state->stage < HIRExprState::Stage::Sbc) {
                if (ep.state->stage == HIRExprState::Stage::SbcRequest) {
                    ERROR(Span(), E0000, "Loop in constant evaluation");
                }
                ep.state->stage = HIRExprState::Stage::SbcRequest;
                HIRExpandClosuresExpr(wb, *this, retTy, epMut);
                HIRExpandStaticBorrowConstantsExpr(wb, *this, ip, epMut);
            }
            if (ep.state->stage < HIRExprState::Stage::Expand) {
                if (ep.state->stage == HIRExprState::Stage::ExpandRequest) {
                    ERROR(Span(), E0000, "Loop in constant evaluation");
                }
                ep.state->stage = HIRExprState::Stage::ExpandRequest;
                HIRExpandUfcsEverythingExpr(wb, *this, epMut, ep.state->currentTraitImpl);
                HIRExpandReborrowsExpr(wb, *this, epMut);
                //HIR_Expand_ErasedType(*this, ep_mut);    // - Maybe?

                ep.state->stage = HIRExprState::Stage::Expand;
            }
            // Generate MIR
            if (ep.state->stage < HIRExprState::Stage::Mir) {
                if (ep.state->stage == HIRExprState::Stage::MirRequest) {
                    ERROR(Span(), E0000, "Loop in constant evaluation");
                }
                ep.state->stage = HIRExprState::Stage::MirRequest;
                HIRGenerateMIRExpr(wb, *this, ip, epMut, args, retTy);
                ep.state->stage = HIRExprState::Stage::Mir;
            }
            assert(ep.mir);
        }
        return &*ep.mir;
    }
}

HIRTypeRef HIRTrait::getVtableType(const Span& sp, const HIRCrate& crate, const HIRTypeData::Data_TraitObject& te) const {
    assert(te.trait.traitPtr == this);

    const auto& vtableTySpath = this->vtablePath;
    const auto& vtableRef = crate.getStructByPath(sp, vtableTySpath);
    // Copy the param set from the trait in the trait object
    HIRPathParams vtableParams = te.trait.path.params.clone();
    vtableParams.types.resize(te.trait.path.params.types.size() + this->typeIndexes.size());
    // - Include associated types on bound
    for (const auto& tyB : te.trait.typeBounds) {
        if (this->typeIndexes.count(tyB.first) == 0) {
            WARNING(sp, W0000, "Trait object path " << te.trait << " references a type with no vtable type index");
            continue;
        }
        auto idx = this->typeIndexes.at(tyB.first);
        vtableParams.types.at(idx) = tyB.second.type;
    }
    return crate.types.path(HIRGenericPath(vtableTySpath, mv$(vtableParams)), &vtableRef);
}

unsigned HIRTrait::getVtableValueIndex(const HIRGenericPath& traitPath, const RcString& name) const {
    auto its = this->valueIndexes.equal_range(name);
    for (auto it = its.first; it != its.second; ++it) {
        DEBUG(traitPath << " :: " << name << " - " << it->second.second);
        if (it->second.second.path == traitPath.path) {
            // TODO: Match generics using match_test_generics comparing to the trait args
            assert(it->second.first > 0);
            return it->second.first;
        }
    }
    return 0;
}

unsigned HIRTrait::getVtableParentIndex(HIRTypeInterner& types, const Span& sp, const HIRPathParams& thisParams, const HIRGenericPath& traitPath) const {
    for (const auto& pt : this->allParentTraits) {
        if (pt.path.path == traitPath.path) {
            auto p = MonomorphStatePtr(types, nullptr, &thisParams, nullptr).monomorphGenericpath(sp, pt.path);
            if (p == traitPath) {
                return vtableParentTraitsStart + (&pt - this->allParentTraits.data());
            }
        }
    }
    return 0;
}

::std::pair<const HIRAssociatedType*, const HIRPathParams*> HIRTrait::getAtyDef(const RcString& name) const {
    auto it = types.find(name);
    if (it != types.end()) {
        return std::make_pair(&it->second, nullptr);
    }
    for (const auto& parent : allParentTraits) {
        it = parent.traitPtr->types.find(name);
        if (it != parent.traitPtr->types.end()) {
            return std::make_pair(&it->second, &parent.path.params);
        }
    }
    return std::make_pair(nullptr, nullptr);
}

/// Helper for getting the struct associated with a pattern path
const HIRStruct& patternGetStruct(const Span& sp, const HIRPath& path, const HIRPattern::PathBinding& binding, bool isTuple) {
    const HIRStruct* strP = nullptr;
    switch (binding.tag()) {
        case HIRPatternPathBinding::TAG_Unbound: {
            BUG(sp, "Unexpected unbound named pattern - " << path);
            break;
        }
        case HIRPatternPathBinding::TAG_Struct: {
            auto& be = binding.as_Struct();
            strP = be;
            break;
        }
        case HIRPatternPathBinding::TAG_Union: {
            BUG(sp, "Tuple pattern used on union " << path);
            break;
        }
        case HIRPatternPathBinding::TAG_Enum: {
            auto& be = binding.as_Enum();
            const auto& enm = *be.ptr;
            if (isTuple) {
                ASSERT_BUG(sp, enm.data.is_Data(), "PathTuple pattern with non-data enum - " << path);
            } else {
                ASSERT_BUG(sp, enm.data.is_Data(), "PathNamed pattern with non-data enum - " << path);
            }
            const auto& enmD = enm.data.as_Data();
            ASSERT_BUG(sp, be.varIdx < enmD.size(), "Variant index " << be.varIdx << " out of range - " << path);
            if (isTuple) {
                ASSERT_BUG(sp, !enmD[be.varIdx].isStruct, "PathTuple pattern with brace enum variant - " << path);
            } else {
                ASSERT_BUG(sp, enmD[be.varIdx].isStruct, "PathNamed pattern with non-brace enum variant - " << path);
            }
            strP = enmD[be.varIdx].type->as_Path().binding.as_Struct();
            break;
        }
    }
    const auto& str = *strP;

    if(isTuple) {
        ASSERT_BUG(sp, str.data.is_Tuple(), "PathTuple pattern with non-tuple struct - " << str.data.tagStr());
    }
    else {
        ASSERT_BUG(sp, str.data.is_Named(), "Struct pattern on non-brace struct");
    }
    return str;
}

const tTupleFields& patternGetTuple(const Span& sp, const HIRPath& path, const HIRPattern::PathBinding& binding) {
    return patternGetStruct(sp, path, binding, true).data.as_Tuple();
}

const tStructFields& patternGetNamed(const Span& sp, const HIRPath& path, const HIRPattern::PathBinding& binding) {
    if (binding.is_Union()) {
        return binding.as_Union()->variants;
    }
    return patternGetStruct(sp, path, binding, false).data.as_Named();
}

// ---
EncodedLiteral EncodedLiteral::makeUsize(u64 v) {
    EncodedLiteral rv;
    rv.bytes.resize(8); // 64-bit pointers only (32-bit targets are unsupported)
    rv.writeUsize(0, v);
    return rv;
}

EncodedLiteral EncodedLiteral::clone() const {
    EncodedLiteral rv;
    rv.bytes = bytes;
    rv.relocations.reserve(relocations.size());
    for (const auto& r : relocations) {
        if (r.p) {
            rv.relocations.push_back(Reloc::newNamed(r.ofs, r.len, r.p->clone(), r.preserveTrackCaller));
        } else {
            rv.relocations.push_back(Reloc::newBytes(r.ofs, r.len, r.bytes));
        }
    }
    return rv;
}

void EncodedLiteral::writeUint(size_t ofs, size_t size, u64 v) {
    assert(ofs + size <= bytes.size());
    for (size_t i = 0; i < size; i++) {
        size_t bit = i * 8; // little-endian only (big-endian targets are unsupported)
        if (bit < 64) {
            auto b = static_cast<u8>(v >> bit);
            bytes[ofs + i] = b;
        }
    }
}

void EncodedLiteral::writeUsize(size_t ofs, u64 v) {
    this->writeUint(ofs, 8, v); // 64-bit pointers only
}

u64 EncodedLiteral::readUsize(size_t ofs) const {
    return EncodedLiteralSlice(*this).slice(ofs).readUint(8).truncateU64(); // 64-bit pointers only
}

U128 EncodedLiteralSlice::readUint(size_t size /*=0*/) const {
    if (size == 0) {
        size = this->size;
    }
    ASSERT_BUG(Span(), size <= this->size, "Over-large read (" << size << " > " << this->size << ")");
    U128 v(0);
    for (size_t i = 0; i < size; i++) {
        size_t bit = i * 8; // little-endian only (big-endian targets are unsupported)
        if (bit < 128) {
            v |= U128(base.bytes[ofs + i]) << bit;
        }
    }
    DEBUG("(" << size << ") = " << v);
    return v;
}

S128 EncodedLiteralSlice::readSint(size_t size /*=0*/) const {
    if (size == 0) {
        size = this->size;
    }
    auto v = readUint(size);
    if (size < 128 / 8 && ((v >> (8 * size - 1)) != 0)) {
        // Sign extend
        v |= U128(UINT64_MAX, UINT64_MAX) << (8 * size);
    }
    DEBUG("(" << size << ") = " << v);
    return S128(v);
}

FloatValue EncodedLiteralSlice::readFloat(size_t size /*=0*/) const {
    if (size == 0) {
        size = this->size;
    }
    assert(size <= this->size);
    switch (size) {
        case 2: {
            F16 v;
            memcpy(&v, &base.bytes[ofs], 2);
            return FloatValue(static_cast<float>(v));
        }
        case 4: {
            float v;
            memcpy(&v, &base.bytes[ofs], 4);
            return v;
        }
        case 8: {
            double v;
            memcpy(&v, &base.bytes[ofs], 8);
            return v;
        }
        case 16: {
            F128 v;
            memcpy(&v, &base.bytes[ofs], 16);
            return v;
        }
        default:
            BUG(Span(), "Unexpected float size");
    }
}

const Reloc* EncodedLiteralSlice::getReloc() const {
    for (const auto& r : base.relocations) {
        if (r.ofs == ofs) {
            return &r;
        }
    }
    return nullptr;
}

bool EncodedLiteralSlice::operator==(const EncodedLiteralSlice& x) const {
    if (size != x.size) {
        return false;
    }
    for (size_t i = 0; i < size; i++) {
        if (base.bytes[ofs + i] != x.base.bytes[x.ofs + i]) {
            return false;
        }
    }
    auto it1 = std::find_if(base.relocations.begin(), base.relocations.end(), [&](const Reloc& r) {
        return r.ofs >= ofs;
    });
    auto it2 = std::find_if(x.base.relocations.begin(), x.base.relocations.end(), [&](const Reloc& r) {
        return r.ofs >= x.ofs;
    });
    for (; it1 != base.relocations.end() && it2 != x.base.relocations.end(); ++it1, ++it2) {
        if (it1->ofs - ofs != it2->ofs - x.ofs) {
            return false;
        }
        if (it1->len != it2->len) {
            return false;
        }
        if (bool(it1->p) != bool(it2->p)) {
            return false;
        }
        if (it1->p) {
            if (*it1->p != *it2->p) {
                return false;
            }
        } else {
            if (it1->bytes != it2->bytes) {
                return false;
            }
        }
    }
    return true;
}

Ordering EncodedLiteralSlice::ord(const EncodedLiteralSlice& x) const {
    // NOTE: Check the data first (to maintain some level of lexical ordering)
    auto minSize = std::min(size, x.size);
    for (size_t i = 0; i < minSize; i++) {
        if (auto cmp = ::ord(base.bytes[ofs + i], x.base.bytes[x.ofs + i])) {
            return cmp;
        }
    }
    if (auto cmp = ::ord(size, x.size)) {
        return cmp;
    }

    auto it1 = std::find_if(base.relocations.begin(), base.relocations.end(), [&](const Reloc& r) {
        return r.ofs >= ofs;
    });
    auto it2 = std::find_if(x.base.relocations.begin(), x.base.relocations.end(), [&](const Reloc& r) {
        return r.ofs >= x.ofs;
    });

    for (; it1 != base.relocations.end() && it2 != x.base.relocations.end(); ++it1, ++it2) {
        if (auto cmp = ::ord(it1->ofs - ofs, it2->ofs - x.ofs)) {
            return cmp;
        }
        if (auto cmp = ::ord(it1->len, it2->len)) {
            return cmp;
        }
        if (auto cmp = ::ord(bool(it1->p), bool(it2->p))) {
            return cmp;
        }
        if (it1->p) {
            if (auto cmp = ::ord(*it1->p, *it2->p)) {
                return cmp;
            }
        } else {
            if (auto cmp = ::ord(it1->bytes, it2->bytes)) {
                return cmp;
            }
        }
    }
    return OrdEqual;
}

::std::ostream& operator<<(std::ostream& os, const EncodedLiteralSlice& x) {
    auto it = std::find_if(x.base.relocations.begin(), x.base.relocations.end(), [&](const Reloc& r) {
        return r.ofs >= x.ofs;
    });
    for (size_t i = 0; i < x.size; i++) {
        const char* HEX = "0123456789ABCDEF";
        auto o = x.ofs + i;
        auto b = x.base.bytes[o];
        if (it != x.base.relocations.end() && it->ofs == o) {
            auto& r = *it;
            if (r.p) {
                os << "{&" << *r.p << "}";
            } else {
                os << "{\"" << FmtEscaped(r.bytes) << "\"}";
            }
            ++it;
        }
        os << HEX[b >> 4] << HEX[b & 0xF];
        if ((i + 1) % 8 == 0 && i + 1 < x.size) {
            os << " ";
        }
    }
    return os;
}

HIRPublicity::HIRPublicity(::std::shared_ptr<HIRSimplePath> p)
    : visPath(p)
{
}

HIRPublicity HIRPublicity::newPriv(HIRSimplePath p) {
    size_t nComp = p.components().size();
    while (nComp > 0 && p.components()[nComp - 1].c_str()[0] == '#') {
        nComp--;
    }
    auto s = std::span<const RcString>(p.components().data(), nComp);
    return HIRPublicity(::std::make_shared<HIRSimplePath>(p.crateName(), s));
}

HIRStatic::HIRStatic(HIRLinkage linkage, bool isMut, HIRTypeRef type, HIRExprPtr value)
    : linkage(std::move(linkage))
    , isMut(isMut)
    , type(std::move(type))
    , value(std::move(value))
{
}

HIRConstant::HIRConstant() {
}

HIRConstant::HIRConstant(HIRGenericParams params, HIRTypeRef type, HIRExprPtr value)
    : params(::std::move(params))
    , type(::std::move(type))
    , value(::std::move(value))
{
}

HIRFunction::HIRFunction() {
}

HIRFunction::HIRFunction(Receiver receiver, HIRGenericParams params, argsT args, HIRTypeRef retTy, HIRExprPtr code)
    : receiver(receiver)
    , params(std::move(params))
    , args(std::move(args))
    , variadic(false)
    , returnType(std::move(retTy))
    , code(std::move(code))
{
}

HIRStruct::FieldDefault::FieldDefault(size_t index, HIRExprPtr v)
    : index(index)
    , expr(std::move(v))
{
}

HIRStruct::HIRStruct(HIRGenericParams params, Repr repr, Data data)
    : params(mv$(params))
    , repr(mv$(repr))
    , data(mv$(data))
{
}

HIRStruct::HIRStruct(HIRGenericParams params, Repr repr, Data data, unsigned align, HIRTraitMarkings tm, HIRStructMarkings sm)
    : params(mv$(params))
    , repr(mv$(repr))
    , data(mv$(data))
    , forcedAlignment(align)
    , markings(mv$(tm))
    , structMarkings(mv$(sm))
{
}

HIRAssociatedType::HIRAssociatedType(HIRGenericParams generics, bool isSized, ::std::vector<HIRTraitPath> traitBounds, HIRTypeRef defaultType)
    : generics(::std::move(generics))
    , isSized(isSized)
    , traitBounds(::std::move(traitBounds))
    , hasDefault(defaultType && !defaultType->is_Infer())
    , defaultValue(defaultType)
{
    assert(defaultType);
}

HIRTrait::HIRTrait(HIRGenericParams gps, ::std::vector<HIRTraitPath> parents)
    : params(mv$(gps))
    , parentTraits(mv$(parents))
    , isMarker(false)
    , isConst(false)
    , isCoinductive(false)
    , isFundamental(false)
    , skipArrayDuringMethodDispatch(false)
    , skipBoxedSliceDuringMethodDispatch(false)
    , vtableParentTraitsStart(0)
{
}

HIRModule::HIRModule() {
}

HIRCrate::HIRCrate(stl::ObjPool* pool, HIRTypeInterner& types)
    : pool(pool)
    , types(types)
    , intrinsicOffsetof(HIRValueItem::make_Function(nullptr))
{
}

bool HIRCrate::isOpaqueAliasNamedBy(const HIRTypeDataErasedTypeAliasInner& alias, const HIRSimplePath* names, size_t nameCount) const {
    for (size_t i = 0; i < nameCount; i++) {
        if (names[i] == alias.path) {
            return true;
        }
    }

    // The attribute can name a plain alias of the opaque alias.
    for (size_t i = 0; i < nameCount; i++) {
        const auto& named = names[i];
        const HIRSimplePath* path = &named;
        for (unsigned int depth = 0; depth < 8; depth++) {
            const auto* item = getTypeitemByPathOpt(*path);
            if (!item) {
                break;
            }
            const auto* typeAlias = item->opt_TypeAlias();
            if (!typeAlias || typeAlias->type == HIRTypeRef()) {
                break;
            }
            const auto* erased = typeAlias->type->opt_ErasedType();
            if (!erased || !erased->inner.is_Alias()) {
                break;
            }
            const auto& inner = erased->inner.as_Alias();
            if (inner.inner->path == alias.path) {
                return true;
            }
            path = &inner.inner->path;
        }
    }

    // It can also name a type that contains the opaque alias.
    for (size_t i = 0; i < nameCount; i++) {
        const auto& named = names[i];
        const auto* item = getTypeitemByPathOpt(named);
        if (!item) {
            continue;
        }
        auto containsAlias = [&](const HIRTypeData* type) {
            if (const auto* erased = type->opt_ErasedType()) {
                if (const auto* inner = erased->inner.opt_Alias()) {
                    return inner->inner->path == alias.path;
                }
            }
            return false;
        };
        if (const auto* structure = item->opt_Struct()) {
            switch (structure->data.tag()) {
                case HIRStructData::TAG_Named:
                    for (const auto& field : structure->data.as_Named()) {
                        if (visitTyWith(field.ty, containsAlias)) {
                            return true;
                        }
                    }
                    break;
                case HIRStructData::TAG_Tuple:
                    for (const auto& field : structure->data.as_Tuple()) {
                        if (visitTyWith(field.ent, containsAlias)) {
                            return true;
                        }
                    }
                    break;
                default:
                    break;
            }
        } else if (const auto* typeAlias = item->opt_TypeAlias(); typeAlias && typeAlias->type != HIRTypeRef()) {
            if (visitTyWith(typeAlias->type, containsAlias)) {
                return true;
            }
        }
    }
    return false;
}

const HIRConstant& HIRCrate::getConstantByPath(const Span& sp, const HIRSimplePath& path) const {
    const auto& ti = this->getValitemByPath(sp, path);
    if (ti.is_Constant()) {
        return *ti.as_Constant();
    }
    else {
        BUG(sp, "`const` path " << path << " didn't point to an enum");
    }
}

const MIRFunction* HIRCrate::getOrGenMir(const WireBoard& wb, const HIRItemPath& ip, const HIRFunction& fcn) const {
    auto ty = fcn.returnType;
    return getOrGenMir(wb, ip, fcn.code, fcn.args, ty);
}

const MIRFunction* HIRCrate::getOrGenMir(const WireBoard& wb, const HIRItemPath& ip, const HIRExprPtr& ep, HIRTypeRef& expTy) const {
    static HIRFunction::argsT sArgs;
    return getOrGenMir(wb, ip, ep, sArgs, expTy);
}
