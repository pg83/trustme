#include "hir_hir.h"
#include <algorithm>
#include "hir_typeck_common.h"
#include "hir_typeck_expr_visit.h" // for invoking typecheck
#include "hir_item_path.h"
#include "hir_expr_state.h"
#include "hir_expand_main_bindings.h"
#include "mir_main_bindings.h"
#include "mir_mir.h"
#include "hir_expr.h"
#include "macro_rules_macro_rules.h" // Used to update the crate name
#include "hir_conv_main_bindings.h"
#include "trans_target.h"
#include "floats.h"
#include <optional>

namespace HIR {
    ::std::ostream& operator<<(::std::ostream& os, const Publicity& x) {
        if (!x.vis_path) {
            os << "pub";
        } else if (*x.vis_path == *Publicity::none_path) {
            os << "priv";
        } else {
            os << "pub(" << *x.vis_path << ")";
        }
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const ConstGeneric& x) {
        TU_MATCH_HDRA( (x), {)
        TU_ARMA(Infer, e) {
                os << "Infer";
                if (e.index != ~0u) {
                    os << "(";
                    os << e.index;
                    os << ")";
                }
            }
            TU_ARMA(Unevaluated, e) {
                os << "Unevaluated(";
                e->fmt(os);
                os << ")";
            }
            TU_ARMA(Generic, e) os << "Generic(" << e << ")";
            TU_ARMA(Evaluated, e) os << "Evaluated(" << *e << ")";
        }
        return os;
    }

    bool ConstGeneric::operator==(const ConstGeneric& x) const {
        if (this->tag() != x.tag()) {
            return false;
        }
        TU_MATCH_HDRA( (*this, x), {)
        TU_ARMA(Infer, te, xe) return te.index == xe.index;
            TU_ARMA(Unevaluated, te, xe) return te->equivalent(*xe);
            TU_ARMA(Generic, te, xe) return te == xe;
            TU_ARMA(Evaluated, te, xe) return EncodedLiteralSlice(*te) == EncodedLiteralSlice(*xe);
        }
        return true;
    }

    Ordering ConstGeneric::ord(const ConstGeneric& x) const {
        if (auto cmp = ::ord(static_cast<int>(this->tag()), static_cast<int>(x.tag()))) {
            return cmp;
        }
        TU_MATCH_HDRA( (*this, x), {)
        TU_ARMA(Infer, te, xe) {
                if (auto cmp = ::ord(te.index, xe.index)) {
                    return cmp;
                }
            }
            TU_ARMA(Unevaluated, te, xe) {
                if (te->equivalent(*xe)) {
                    return OrdEqual;
                }
                return te->ord(*xe);
            }
            TU_ARMA(Generic, te, xe) {
                if (auto cmp = ::ord(te, xe)) {
                    return cmp;
                }
            }
            TU_ARMA(Evaluated, te, xe) {
                if (auto cmp = ::ord(EncodedLiteralSlice(*te), EncodedLiteralSlice(*xe))) {
                    return cmp;
                }
            }
        }
        return OrdEqual;
    }

    ::std::ostream& operator<<(::std::ostream& os, const ConstGenericUnevaluated& x) {
        x.fmt(os);
        return os;
    }

    ConstGenericUnevaluated::ConstGenericUnevaluated(HIR::ExprPtr ep)
        : expr(std::make_shared<HIR::ExprPtr>(std::move(ep)))
    {
    }

    ConstGenericUnevaluated ConstGenericUnevaluated::clone() const {
        ConstGenericUnevaluated rv;
        rv.params_impl = params_impl.clone();
        rv.params_item = params_item.clone();
        rv.expr = expr;
        return rv;
    }

    ConstGenericUnevaluated ConstGenericUnevaluated::monomorph(const Span& sp, const Monomorphiser& ms, bool allowInfer /*=true*/) const {
        ConstGenericUnevaluated rv;
        rv.params_impl = ms.monomorph_path_params(sp, params_impl, allowInfer);
        rv.params_item = ms.monomorph_path_params(sp, params_item, allowInfer);
        rv.expr = this->expr;
        return rv;
    }

    namespace {
        const ::HIR::ConstGeneric* getUnevaluatedParam(const ::HIR::ConstGenericUnevaluated& value, unsigned int binding) {
            const ::HIR::PathParams* params = nullptr;
            switch (binding >> 8) {
                case ::HIR::GENERICImpl:
                    params = &value.params_impl;
                    break;
                case ::HIR::GENERICItem:
                    params = &value.params_item;
                    break;
                default:
                    return nullptr;
            }
            const unsigned int index = binding & 0xFF;
            return index < params->values.size() ? &params->values[index] : nullptr;
        }

        bool constExprLiteralsEqual(const ::HIR::ExprNodeLiteral& left, const ::HIR::ExprNodeLiteral& right) {
            if (left.mData.tag() != right.mData.tag()) {
                return false;
            }
            TU_MATCH_HDRA( (left.mData, right.mData), {)
            TU_ARMA(Integer, l, r) return l.mType == r.mType && l.mValue == r.mValue;
                TU_ARMA(Float, l, r) return l.mType == r.mType && l.mValue == r.mValue;
                TU_ARMA(Boolean, l, r) return l == r;
                TU_ARMA(String, l, r) return l == r;
                TU_ARMA(CString, l, r) return l.v == r.v;
                TU_ARMA(ByteString, l, r) return l == r;
            }
            throw "";
        }

        bool constExprNodesEqual(const ::HIR::ConstGenericUnevaluated& leftValue, const ::HIR::ExprNode& left, const ::HIR::ConstGenericUnevaluated& right_value, const ::HIR::ExprNode& right) {
            if (const auto* l = cast<const ::HIR::ExprNodeConstParam>(&left)) {
                const auto* r = cast<const ::HIR::ExprNodeConstParam>(&right);
                if (!r) {
                    return false;
                }
                const auto* lParam = getUnevaluatedParam(leftValue, l->mBinding);
                const auto* r_param = getUnevaluatedParam(right_value, r->mBinding);
                return lParam && r_param ? *lParam == *r_param : l->mBinding == r->mBinding;
            }
            if (const auto* l = cast<const ::HIR::ExprNodeLiteral>(&left)) {
                const auto* r = cast<const ::HIR::ExprNodeLiteral>(&right);
                return r && constExprLiteralsEqual(*l, *r);
            }
            if (const auto* l = cast<const ::HIR::ExprNodeBinOp>(&left)) {
                const auto* r = cast<const ::HIR::ExprNodeBinOp>(&right);
                return r && l->op == r->op && constExprNodesEqual(leftValue, *l->left, right_value, *r->left) && constExprNodesEqual(leftValue, *l->right, right_value, *r->right);
            }
            if (const auto* l = cast<const ::HIR::ExprNodeUniOp>(&left)) {
                const auto* r = cast<const ::HIR::ExprNodeUniOp>(&right);
                return r && l->op == r->op && constExprNodesEqual(leftValue, *l->mValue, right_value, *r->mValue);
            }
            if (const auto* l = cast<const ::HIR::ExprNodeCast>(&left)) {
                const auto* r = cast<const ::HIR::ExprNodeCast>(&right);
                return r && l->dstType == r->dstType && constExprNodesEqual(leftValue, *l->mValue, right_value, *r->mValue);
            }
            if (const auto* l = cast<const ::HIR::ExprNodeConstBlock>(&left)) {
                const auto* r = cast<const ::HIR::ExprNodeConstBlock>(&right);
                return r && constExprNodesEqual(leftValue, *l->inner, right_value, *r->inner);
            }
            if (const auto* l = cast<const ::HIR::ExprNodeCallPath>(&left)) {
                const auto* r = cast<const ::HIR::ExprNodeCallPath>(&right);
                if (!r || l->mPath != r->mPath || l->mArgs.size() != r->mArgs.size()) {
                    return false;
                }
                for (unsigned int i = 0; i < l->mArgs.size(); i++) {
                    if (!constExprNodesEqual(leftValue, *l->mArgs[i], right_value, *r->mArgs[i])) {
                        return false;
                    }
                }
                return true;
            }
            if (const auto* l = cast<const ::HIR::ExprNodeBlock>(&left)) {
                const auto* r = cast<const ::HIR::ExprNodeBlock>(&right);
                if (!r || l->nodes.size() != r->nodes.size() || static_cast<bool>(l->valueNode) != static_cast<bool>(r->valueNode)) {
                    return false;
                }
                for (unsigned int i = 0; i < l->nodes.size(); i++) {
                    if (!constExprNodesEqual(leftValue, *l->nodes[i], right_value, *r->nodes[i])) {
                        return false;
                    }
                }
                return !l->valueNode || constExprNodesEqual(leftValue, *l->valueNode, right_value, *r->valueNode);
            }
            return false;
        }
    }

    bool ConstGenericUnevaluated::equivalent(const ConstGenericUnevaluated& x) const {
        return constExprNodesEqual(*this, **this->expr, x, **x.expr);
    }

    Ordering ConstGenericUnevaluated::ord(const ConstGenericUnevaluated& x) const {
        if (this->expr.get() != x.expr.get()) {
            // If only one has populated MIR, they can't be equal (sort populated MIR after)
            if (!this->expr->mir != !x.expr->mir) {
                return (this->expr->mir ? OrdGreater : OrdLess);
            }

            // HACK: If the inner is a const param on both, sort based on that.
            // - Very similar to the ordering of TypeRef::Generic
            const auto* tn = cast<const HIR::ExprNodeConstParam>(&**this->expr);
            const auto* xn = cast<const HIR::ExprNodeConstParam>(&**x.expr);
            if (tn && xn) {
                // Is this valid? What if they're from different scopes?
                return ::ord(tn->mBinding, xn->mBinding);
            }

            // EVIL OPTION: Just compare the string representations
            // - The fmt() routine prints MIR blocks (when populated) or the source expression
            //   (when not) in a deterministic, pointer-free form, so this gives a stable order.
            auto v_t = FMT(*this);
            auto v_x = FMT(x);
            return ::ord(v_t, v_x);
        }
        if (auto cmp = this->params_impl.ord(x.params_impl)) {
            return cmp;
        }
        if (auto cmp = this->params_item.ord(x.params_item)) {
            return cmp;
        }
        return OrdEqual;
    }

    void ConstGenericUnevaluated::fmt(::std::ostream& os) const {
        os << "{";
        os << "0=" << this->params_impl;
        os << "1=" << this->params_item;
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

    ::std::ostream& operator<<(::std::ostream& os, const Struct::Repr& x) {
        os << "repr(";
        switch (x) {
            case Struct::Repr::Rust:
                os << "Rust";
                break;
            case Struct::Repr::C:
                os << "C";
                break;
            case Struct::Repr::Simd:
                os << "simd";
                break;
            case Struct::Repr::Transparent:
                os << "transparent";
                break;
        }
        os << ")";
        return os;
    }
}

HIR::ConstGeneric HIR::ConstGeneric::clone() const {
    TU_MATCH_HDRA( (*this), {)
    TU_ARMA(Infer, e) return e;
        TU_ARMA(Unevaluated, e) return ::std::make_unique<ConstGenericUnevaluated>(e->clone());
        TU_ARMA(Generic, e) return e;
        TU_ARMA(Evaluated, e) return EncodedLiteralPtr(e->clone());
    }
    throw "";
}

::std::shared_ptr<::HIR::SimplePath> HIR::Publicity::none_path = ::std::make_shared<HIR::SimplePath>(::HIR::SimplePath{"#", {}});

bool HIR::Publicity::isVisible(const ::HIR::SimplePath& p) const {
    DEBUG(*this << " " << p);
    // No path = global public
    if (!vis_path) {
        return true;
    }
    // Empty simple path = full private
    if (*vis_path == *none_path) {
        return false;
    }
    // `p` must be a child of vis_path (i.e. starts with it)
    return p.starts_with(*vis_path);
}

::HIR::TypeRef HIR::Function::makePtrTy(const Span& sp, const Monomorphiser& ms) const {
    ::HIR::TypeDataFunctionPointer ft;
    ft.is_unsafe = this->unsafe;
    ft.is_variadic = this->variadic;
    ft.mAbi = this->mAbi;
    ft.mRettype = ms.monomorph_type(sp, this->returnType);
    ft.argTypes.reserve(this->mArgs.size());
    for (const auto& arg : this->mArgs) {
        ft.argTypes.push_back(ms.monomorph_type(sp, arg.second));
    }
    return ms.type_interner().function(std::move(ft));
}

::HIR::TypeRef HIR::fnPtrTupleConstructor(const Span& sp, const Monomorphiser& ms, HIR::TypeRef ret_ty, const t_tuple_fields& fields) {
    ::HIR::TypeDataFunctionPointer ft;
    ft.is_unsafe = false;
    ft.is_variadic = false;
    ft.mAbi = RcString::new_interned(ABI_RUST);
    ft.mRettype = std::move(ret_ty);
    ft.argTypes.reserve(fields.size());
    for (const auto& fld : fields) {
        ft.argTypes.push_back(ms.monomorph_type(sp, fld.ent));
    }
    return ms.type_interner().function(std::move(ft));
}

size_t HIR::Enum::findVariant(const RcString& name) const {
    if (mData.is_Value()) {
        const auto& e = mData.as_Value();
        auto it = ::std::find_if(e.variants.begin(), e.variants.end(), [&](const auto& x) {
            return x.name == name;
        });
        if (it == e.variants.end()) {
            return SIZE_MAX;
        }
        return it - e.variants.begin();
    } else {
        const auto& e = mData.as_Data();

        auto it = ::std::find_if(e.begin(), e.end(), [&](const auto& x) {
            return x.name == name;
        });
        if (it == e.end()) {
            return SIZE_MAX;
        }
        return it - e.begin();
    }
}

bool HIR::Enum::isValue() const {
    return this->mData.is_Value();
}

U128 HIR::Enum::getValue(size_t idx) const {
    if (mData.is_Value()) {
        const auto& e = mData.as_Value();
        assert(idx < e.variants.size());

        return e.variants[idx].val;
    } else {
        assert(!"TODO: Enum::get_value on non-value enum?");
        throw "";
    }
}

/*static*/ ::HIR::CoreType HIR::Enum::getReprType(Repr r) {
    switch (r) {
        case ::HIR::Enum::Repr::Auto:
            return ::HIR::CoreType::Isize;
            break;
        case ::HIR::Enum::Repr::Usize:
            return ::HIR::CoreType::Usize;
            break;
        case ::HIR::Enum::Repr::U8:
            return ::HIR::CoreType::U8;
            break;
        case ::HIR::Enum::Repr::U16:
            return ::HIR::CoreType::U16;
            break;
        case ::HIR::Enum::Repr::U32:
            return ::HIR::CoreType::U32;
            break;
        case ::HIR::Enum::Repr::U64:
            return ::HIR::CoreType::U64;
            break;
        case ::HIR::Enum::Repr::U128:
            return ::HIR::CoreType::U128;
            break;
        case ::HIR::Enum::Repr::Isize:
            return ::HIR::CoreType::Isize;
            break;
        case ::HIR::Enum::Repr::I8:
            return ::HIR::CoreType::I8;
            break;
        case ::HIR::Enum::Repr::I16:
            return ::HIR::CoreType::I16;
            break;
        case ::HIR::Enum::Repr::I32:
            return ::HIR::CoreType::I32;
            break;
        case ::HIR::Enum::Repr::I64:
            return ::HIR::CoreType::I64;
            break;
        case ::HIR::Enum::Repr::I128:
            return ::HIR::CoreType::I128;
            break;
    }
    throw "";
}

const ::HIR::SimplePath& ::HIR::Crate::getLangItemPath(const Span& sp, const char* name) const {
    auto it = this->mLangItems.find(name);
    if (it == this->mLangItems.end()) {
        ERROR(sp, E0000, "Undefined language item '" << name << "' required");
    }
    return it->second;
}

const ::HIR::SimplePath& ::HIR::Crate::getLangItemPathOpt(const char* name) const {
    static ::HIR::SimplePath emptyPath;
    auto it = this->mLangItems.find(name);
    if (it == this->mLangItems.end()) {
        return emptyPath;
    }
    return it->second;
}

namespace {
    const ::HIR::Module& getContainingModule(const ::HIR::Crate& crate, const Span& sp, const ::HIR::SimplePath& path, bool ignoreCrateName, bool ignoreLastNode) {
        ASSERT_BUG(sp, path.components().size() > 0u, "Invalid path (no nodes) - " << path);
        ASSERT_BUG(sp, path.components().size() > (ignoreLastNode ? 1u : 0u), "Invalid path (only one node with `ignore_last_node` - " << path);

        const ::HIR::Module* mod;
        if (!ignoreCrateName && path.crate_name() != crate.crateName) {
            ASSERT_BUG(sp, crate.extCrates.count(path.crate_name()) > 0, "Crate '" << path.crate_name() << "' not loaded for " << path);
            mod = &crate.extCrates.at(path.crate_name()).mData->rootModule;
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

const ::HIR::MacroItem& ::HIR::Crate::getMacroitemByPath(const Span& sp, const ::HIR::SimplePath& path, bool ignoreCrateName, bool ignoreLastNode) const {
    const auto& mod = getContainingModule(*this, sp, path, ignoreCrateName, ignoreLastNode);

    auto it = mod.macroItems.find(ignoreLastNode ? path.components()[path.components().size() - 2] : path.components().back());
    if (it == mod.macroItems.end()) {
        BUG(sp, "Could not find macro name in " << path);
    }

    return it->second->ent;
}

const ::HIR::TypeItem& ::HIR::Crate::getTypeitemByPath(const Span& sp, const ::HIR::SimplePath& path, bool ignoreCrateName, bool ignoreLastNode) const {
    const auto& mod = getContainingModule(*this, sp, path, ignoreCrateName, ignoreLastNode);

    auto it = mod.modItems.find(ignoreLastNode ? path.components()[path.components().size() - 2] : path.components().back());
    if (it == mod.modItems.end()) {
        BUG(sp, "Could not find type " << path);
    }

    return it->second->ent;
}

const ::HIR::Module& ::HIR::Crate::getModByPath(const Span& sp, const ::HIR::SimplePath& path, bool ignoreLastNode /*=false*/, bool ignoreCrateName /*=false*/) const {
    if (ignoreLastNode) {
        ASSERT_BUG(sp, path.components().size() > 0, "get_mod_by_path received invalid path with ignore_last_node=true - " << path);
    }
    // Special handling for empty paths with `ignore_last_node`
    if (path.components().size() == (ignoreLastNode ? 1 : 0)) {
        if (!ignoreCrateName && path.crate_name() != crateName) {
            ASSERT_BUG(sp, extCrates.count(path.crate_name()) > 0, "Crate '" << path.crate_name() << "' not loaded");
            return extCrates.at(path.crate_name()).mData->rootModule;
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

const ::HIR::Trait& ::HIR::Crate::getTraitByPath(const Span& sp, const ::HIR::SimplePath& path) const {
    const auto& ti = this->getTypeitemByPath(sp, path);
    TU_IFLET(::HIR::TypeItem, ti, Trait, e, return e;)
    else {
        BUG(sp, "Trait path " << path << " didn't point to a trait (" << ti.tag_str() << ")");
    }
}

::std::optional<size_t> HIR::Crate::findMostSpecificTrait(
    const Span& sp,
    const ::std::vector<::HIR::SimplePath>& candidates
) const {
    ::std::optional<size_t> selected;
    for (size_t candidateIndex = 0; candidateIndex < candidates.size(); candidateIndex++) {
        const auto& candidate = candidates[candidateIndex];
        const auto& trait = this->getTraitByPath(sp, candidate);
        bool isSubtraitOfAll = true;

        for (const auto& other : candidates) {
            if (candidate == other) {
                continue;
            }
            const bool hasSupertrait = ::std::any_of(
                trait.allParentTraits.begin(),
                trait.allParentTraits.end(),
                [&](const auto& parent) {
                    return parent.mPath.mPath == other;
                }
            );
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

const ::HIR::Struct& ::HIR::Crate::getStructByPath(const Span& sp, const ::HIR::SimplePath& path) const {
    const auto& ti = this->getTypeitemByPath(sp, path);
    TU_IFLET(::HIR::TypeItem, ti, Struct, e, return e;)
    else {
        BUG(sp, "Struct path " << path << " didn't point to a struct (" << ti.tag_str() << ")");
    }
}

const ::HIR::Union& ::HIR::Crate::getUnionByPath(const Span& sp, const ::HIR::SimplePath& path) const {
    const auto& ti = this->getTypeitemByPath(sp, path);
    TU_IFLET(::HIR::TypeItem, ti, Union, e, return e;)
    else {
        BUG(sp, "Path " << path << " didn't point to a union (" << ti.tag_str() << ")");
    }
}

const ::HIR::Enum& ::HIR::Crate::getEnumByPath(const Span& sp, const ::HIR::SimplePath& path, bool ignoreCrateName, bool ignoreLastNode) const {
    const auto& ti = this->getTypeitemByPath(sp, path, ignoreCrateName, ignoreLastNode);
    TU_IFLET(::HIR::TypeItem, ti, Enum, e, return e;)
    else {
        BUG(sp, "Enum path " << path << " didn't point to an enum (" << ti.tag_str() << ")");
    }
}

const ::HIR::ValueItem& ::HIR::Crate::getValitemByPath(const Span& sp, const ::HIR::SimplePath& path, bool ignoreCrateName) const {
    if (path.crate_name() == "#intrinsics") {
        ASSERT_BUG(sp, path.components().size() == 1, "");
        if (path.components().back() == "offset_of") {
            if (!intrinsicOffsetof.as_Function().variadic) {
                auto& v = intrinsicOffsetof.as_Function();
                v.variadic = true;
                v.mParams.types.push_back(HIR::TypeParamDef{RcString::new_interned("T"), types.infer(), false});
            }
            return intrinsicOffsetof;
        }
        TODO(sp, "Get intrinsic " << path.components().back());
    }
    if (path.crate_name() == this->crateName && path.components().size() == 1) {
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

    return it->second->ent;
}

const ::HIR::Function& ::HIR::Crate::getFunctionByPath(const Span& sp, const ::HIR::SimplePath& path) const {
    const auto& ti = this->getValitemByPath(sp, path);
    TU_IFLET(::HIR::ValueItem, ti, Function, e, return e;)
    else {
        BUG(sp, "Function path " << path << " didn't point to an function (" << ti.tag_str() << ")");
    }
}

const ::HIR::Static& ::HIR::Crate::getStaticByPath(const Span& sp, const ::HIR::SimplePath& path) const {
    const auto& m = this->getModByPath(sp, path, /*ignore_last*/ true);
    auto it = m.valueItems.find(path.components().back());
    if (it != m.valueItems.end()) {
        ASSERT_BUG(sp, it->second->ent.is_Static(), "`static` path " << path << " didn't point to a static - " << it->second->ent.tag_str());
        return it->second->ent.as_Static();
    }
    for (const auto& e : m.inlineStatics) {
        if (e.first == path.components().back()) {
            return *e.second;
        }
    }
    if (path.crate_name() == this->crateName && path.components().size() == 1) {
        auto i = std::find_if(newValues.begin(), newValues.end(), [&](const auto& v) {
            return v.first == path.components().back();
        });
        if (i != newValues.end()) {
            return i->second->ent.as_Static();
        }
    }
    BUG(sp, "`static` path " << path << " can't be found");
}


void HIR::Crate::post_load_update(const RcString& name) {
    // TODO: Do a pass across m_hir that
    // 1. Updates all absolute paths with the crate name
    // 2. Sets binding pointers where required
    // 3. Updates macros with the crate name
}


namespace {
    bool isUnboundedInfer(const ::HIR::TypeData* type) {
        if (const auto* e = type->opt_Infer()) {
            return e->ty_class == ::HIR::InferClass::None;
        } else {
            return false;
        }
    }

    class ImplMatcher: public ::HIR::MatchGenerics {
        std::vector<std::optional<HIR::TypeRef>> implTypes;

    public:
        ImplMatcher(const ::HIR::GenericParams& impl_generics)
            : implTypes(impl_generics.types.size())
        {
        }

        ::HIR::Compare matchTy(const ::HIR::GenericRef& g, const ::HIR::TypeData* ty, ::HIR::t_cb_resolve_type resolve_cb) override {
            assert(g.binding < implTypes.size());
            if (implTypes[g.binding]) {
                return (*implTypes[g.binding])->compareWithPlaceholders(Span(), ty, resolve_cb);
            }
            implTypes[g.binding] = ty;
            return ::HIR::Compare::Equal;
        }

        ::HIR::Compare matchVal(const ::HIR::GenericRef& g, const ::HIR::ConstGeneric& sz) override {
            // TODO
            //assert( g.binding < impl_params.m_values.size() );
            //impl_params.m_values[g.binding] = sz.clone();
            return ::HIR::Compare::Equal;
        }
    };

    bool matchesTypeRoot(const ::HIR::GenericParams& params, const ::HIR::TypeData* implTy, const ::HIR::TypeData* matchType, ::HIR::t_cb_resolve_type ty_res) {
        // A nominal path deserialises without its pointer-valued binding
        // metadata. Its SimplePath is nevertheless complete and is exactly
        // what the impl index and matcher use. Only an unresolved UFCS path is
        // still too early to select an inherent impl.
        const auto* matchPath = matchType->opt_Path();
        if (isUnboundedInfer(matchType)
            || (matchPath && matchPath->binding.is_Unbound()
                && !matchPath->path.mData.is_Generic())) {
            return false;
        }
#if 1
        ImplMatcher m{params};
        auto cmp = implTy->matchTestGenericsFuzz(Span(), matchType, ty_res, m);
        return cmp != HIR::Compare::Unequal;
#else
        return matchesTypeInt(implTy, matchType, ty_res, true);
#endif
    }
}

bool ::HIR::TraitImpl::matchesType(const ::HIR::TypeData* type, ::HIR::t_cb_resolve_type ty_res) const {
    // NOTE: Don't return any impls when the type is an unbouned ivar. Wouldn't be able to pick anything anyway
    // TODO: For `Unbound`, it could be valid, if the target is a generic.
    // - Pure infer could also be useful (for knowing if there's any other potential impls)

    // HACK: Assume an unbounded matches
    if (isUnboundedInfer(type)) {
        return true;
    }
    return matchesTypeRoot(mParams, mType, type, ty_res);
}

bool ::HIR::TypeImpl::matchesType(const ::HIR::TypeData* type, ::HIR::t_cb_resolve_type ty_res) const {
    return matchesTypeRoot(mParams, mType, type, ty_res);
}

bool ::HIR::MarkerImpl::matchesType(const ::HIR::TypeData* type, ::HIR::t_cb_resolve_type ty_res) const {
    return matchesTypeRoot(mParams, mType, type, ty_res);
}

namespace {

    struct TypeOrdSpecificMixedOrdering {};

    ::Ordering typelist_ord_specific(const Span& sp, const ThinVector<::HIR::TypeRef>& left, const ThinVector<::HIR::TypeRef>& right);
    ::Ordering typelist_ord_specific(const Span& sp, const ::std::vector<::HIR::TypeRef>& left, const ::std::vector<::HIR::TypeRef>& right);

    ::Ordering arraySizeOrdSpecific(
        const Span& sp,
        const ::HIR::ArraySize& left,
        const ::HIR::ArraySize& right
    ) {
        if (left == right) {
            return ::OrdEqual;
        }
        const bool leftOpen = left.is_Unevaluated();
        const bool right_open = right.is_Unevaluated();
        if (leftOpen != right_open) {
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

    ::Ordering type_ord_specific(const Span& sp, const ::HIR::TypeData* left, const ::HIR::TypeData* right) {
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

        TU_MATCH_HDRA( ((*left)), {)
        TU_ARMA(Generic, le)
            throw "";
            TU_ARMA(Infer, le) {
                BUG(sp, "Hit infer");
            }
            TU_ARMA(Diverge, le) {
                BUG(sp, "Hit diverge");
            }
            TU_ARMA(NodeType, le) {
                BUG(sp, "Hit " << left);
            }
            TU_ARMA(Primitive, le) {
                if (const auto* re = right->opt_Primitive()) {
                    if (le != *re) {
                        BUG(sp, "Mismatched types - " << left << " and " << right);
                    }
                    return ::OrdEqual;
                } else {
                    BUG(sp, "Mismatched types - " << left << " and " << right);
                }
            }
            TU_ARMA(Path, le) {
                if (!right->is_Path() || le.path.mData.tag() != right->as_Path().path.mData.tag()) {
                    BUG(sp, "Mismatched types - " << left << " and " << right);
                }
                TU_MATCHA((le.path.mData, right->as_Path().path.mData), (lpe, rpe), (Generic, if (lpe.mPath != rpe.mPath) BUG(sp, "Mismatched types - " << left << " and " << right); return typelist_ord_specific(sp, lpe.mParams.types, rpe.mParams.types);), (UfcsUnknown, ), (UfcsKnown, ), (UfcsInherent, ))
                TODO(sp, "Path - " << le.path << " and " << right);
            }
            TU_ARMA(TraitObject, le) {
                ASSERT_BUG(sp, right->is_TraitObject(), "Mismatched types - " << left << " vs " << right);
                const auto& re = right->as_TraitObject();
                ASSERT_BUG(sp, le.mTrait.mPath.mPath == re.mTrait.mPath.mPath, "Mismatched types - " << left << " vs " << right);
                ASSERT_BUG(sp, le.markers.size() == re.markers.size(), "Mismatched types - " << left << " vs " << right);

                auto ord = typelist_ord_specific(sp, le.mTrait.mPath.mParams.types, re.mTrait.mPath.mParams.types);
                if (ord != ::OrdEqual) {
                    return ord;
                }
                for (size_t i = 0; i < le.markers.size(); i++) {
                    ASSERT_BUG(sp, le.markers[i].mPath == re.markers[i].mPath, "Mismatched types - " << left << " vs " << right);
                    ord = typelist_ord_specific(sp, le.markers[i].mParams.types, re.markers[i].mParams.types);
                    if (ord != ::OrdEqual) {
                        return ord;
                    }
                }
                return ::OrdEqual;
            }
            TU_ARMA(ErasedType, le) {
                TODO(sp, "ErasedType - " << left);
            }
            TU_ARMA(NamedFunction, le) {
                BUG(sp, "Hit function type");
            }
            TU_ARMA(Function, le) {
                if (/*const auto* re =*/right->opt_Function()) {
                    if (left == right) {
                        return ::OrdEqual;
                    }
                    TODO(sp, "Function - " << left << " vs " << right);
                    //return typelist_ord_specific(sp, le.arg_types, re->arg_types);
                } else {
                    BUG(sp, "Mismatched types - " << left << " and " << right);
                }
            }
            TU_ARMA(Tuple, le) {
                if (const auto* re = right->opt_Tuple()) {
                    return typelist_ord_specific(sp, le, *re);
                } else {
                    BUG(sp, "Mismatched types - " << left << " and " << right);
                }
            }
            TU_ARMA(Slice, le) {
                if (const auto* re = right->opt_Slice()) {
                    return type_ord_specific(sp, le.inner, re->inner);
                } else {
                    BUG(sp, "Mismatched types - " << left << " and " << right);
                }
            }
            TU_ARMA(Array, le) {
                if (const auto* re = right->opt_Array()) {
                    return combineSpecificity(
                        type_ord_specific(sp, le.inner, re->inner),
                        arraySizeOrdSpecific(sp, le.size, re->size)
                    );
                } else {
                    BUG(sp, "Mismatched types - " << left << " and " << right);
                }
            }
            TU_ARMA(Pointer, le) {
                if (const auto* re = right->opt_Pointer()) {
                    if (le.type != re->type) {
                        BUG(sp, "Mismatched types - " << left << " and " << right);
                    }
                    return type_ord_specific(sp, le.inner, re->inner);
                } else {
                    BUG(sp, "Mismatched types - " << left << " and " << right);
                }
            }
            TU_ARMA(Borrow, le) {
                if (const auto* re = right->opt_Borrow()) {
                    if (le.type != re->type) {
                        BUG(sp, "Mismatched types - " << left << " and " << right);
                    }
                    return type_ord_specific(sp, le.inner, re->inner);
                } else {
                    BUG(sp, "Mismatched types - " << left << " and " << right);
                }
            }
        }
        throw "Fell off end of type_ord_specific";
    }

    ::Ordering typelist_ord_specific(const Span& sp, const ThinVector<::HIR::TypeRef>& le, const ThinVector<::HIR::TypeRef>& re) {
        auto rv = ::OrdEqual;
        assert(le.size() == re.size());
        for (unsigned int i = 0; i < le.size(); i++) {
            auto a = type_ord_specific(sp, le[i], re[i]);
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

    ::Ordering typelist_ord_specific(const Span& sp, const ::std::vector<::HIR::TypeRef>& le, const ::std::vector<::HIR::TypeRef>& re) {
        auto rv = ::OrdEqual;
        assert(le.size() == re.size());
        for (unsigned int i = 0; i < le.size(); i++) {
            auto a = type_ord_specific(sp, le[i], re[i]);
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
    void addBoundFromTrait(HIR::TypeInterner& types, ::std::vector<::HIR::GenericBound>& rv, const std::unique_ptr<HIR::GenericParams>& hrtbs, const ::HIR::TypeData* type, const ::HIR::TraitPath& curTrait) {
        static Span sp;
        assert(curTrait.traitPtr);
        const auto& tr = *curTrait.traitPtr;
        auto monomorph_cb = MonomorphStatePtr(types, type, &curTrait.mPath.mParams, nullptr);

        for (const auto& trait_path_raw : tr.allParentTraits) {
            // 1. Monomorph
            auto trait_path_mono = monomorph_cb.monomorph_traitpath(sp, trait_path_raw, false, false);
            // 2. Add
            rv.push_back(::HIR::GenericBound::make_TraitBound({hrtbs ? box$(hrtbs->clone()) : nullptr, type, mv$(trait_path_mono)}));
        }

        // TODO: Add traits from `Self: Foo` bounds?
        // TODO: Move associated types to the source trait.
    }

    ::std::vector<::HIR::GenericBound> flattenBounds(HIR::TypeInterner& types, const ::std::vector<::HIR::GenericBound>& bounds) {
        ::std::vector<::HIR::GenericBound> rv;
        for (const auto& b : bounds) {
            rv.push_back(b.clone());
            if (const auto* be = b.opt_TraitBound()) {
                addBoundFromTrait(types, rv, be->hrtbs, be->type, be->trait);
            }
        }
        ::std::sort(rv.begin(), rv.end(), [](const auto& a, const auto& b) {
            return ::ord(a, b) == OrdLess;
        });
        return rv;
    }
}

bool ::HIR::TraitImpl::more_specific_than(HIR::TypeInterner& types, const ::HIR::TraitImpl& other) const {
    static const Span _sp;
    const Span& sp = _sp;
    TRACE_FUNCTION;
    //DEBUG("this  = " << *this);
    //DEBUG("other = " << other);

    // >> https://github.com/rust-lang/rfcs/blob/master/text/1210-impl-specialization.md#defining-the-precedence-rules
    // 1. If this->m_type is less specific than other.m_type: return false
    try {
        // If any in te.impl->m_params is less specific than oe.impl->m_params: return false
        auto ord = typelist_ord_specific(sp, this->traitArgs.types, other.traitArgs.types);
        if (ord != ::OrdEqual) {
            DEBUG("- Trait arguments " << (ord == ::OrdLess ? "less" : "more") << " specific");
            return ord == ::OrdGreater;
        }

        ord = type_ord_specific(sp, this->mType, other.mType);
        // If `*this` < `other` : false
        if (ord != ::OrdEqual) {
            DEBUG("- Type " << this->mType << " " << (ord == ::OrdLess ? "less" : "more") << " specific than " << other.mType);
            return ord == ::OrdGreater;
        }
    } catch (const TypeOrdSpecificMixedOrdering& e) {
        BUG(sp, "Mixed ordering in more_specific_than");
    }

    //if( other.m_params.m_bounds.size() == 0 ) {
    //    DEBUG("- Params (none in other, some in this)");
    //    return m_params.m_bounds.size() > 0;
    //}
    // 3. Compare bound set, if there is a rule in oe that is missing from te; return false
    // TODO: Cache these lists (calculate after outer typecheck?)
    auto boundsT = flattenBounds(types, mParams.bounds);
    auto boundsO = flattenBounds(types, other.mParams.bounds);

    DEBUG("bounds_t = " << boundsT);
    DEBUG("bounds_o = " << boundsO);

    // If there are less bounds in this impl, it can't be more specific.
    if (boundsT.size() < boundsO.size()) {
        DEBUG("Bound count");
        return false;
    }

    auto itT = boundsT.begin();
    auto itO = boundsO.begin();
    bool is_equal = true;
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
            if (bT.type == bO.type && bT.trait.mPath.mPath == bO.trait.mPath.mPath) {
                const auto& params_t = bT.trait.mPath.mParams;
                const auto& params_o = bO.trait.mPath.mParams;
                switch (typelist_ord_specific(sp, params_t.types, params_o.types)) {
                    case ::OrdLess:
                        return false;
                    case ::OrdGreater:
                        return true;
                    case ::OrdEqual:
                        break;
                }
                // TODO: Find cases where there's `T: Foo<T>` and `T: Foo<U>`
                for (unsigned int i = 0; i < params_t.types.size(); i++) {
                    if (params_t.types[i] != params_o.types[i] && params_t.types[i] == bT.type) {
                        return true;
                    }
                }
                TODO(sp, *itT << " ?= " << *itO);
            }
        }

        if (cmp == OrdLess) {
            is_equal = false;
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
        return !is_equal;
    }
}

namespace {

    struct ImplTyMatcher: public ::HIR::MatchGenerics, public Monomorphiser {
        ::std::vector<::std::optional<::HIR::TypeRef>> implTys;
        ::std::vector<::std::optional<::HIR::ConstGeneric>> implVals;
        ::std::vector<::std::optional<::HIR::LifetimeRef>> implLfts;

        explicit ImplTyMatcher(HIR::TypeInterner& types)
            : Monomorphiser(types)
        {
        }

        ::HIR::Compare matchTy(const ::HIR::GenericRef& g, const ::HIR::TypeData* ty, ::HIR::t_cb_resolve_type _resolve_cb) override {
            assert(g.binding < implTys.size());
            if (implTys.at(g.binding)) {
                DEBUG("Compare " << ty << " and " << *implTys.at(g.binding));
                return (ty == *implTys.at(g.binding) ? ::HIR::Compare::Equal : ::HIR::Compare::Unequal);
            } else {
                implTys.at(g.binding) = ty;
                return ::HIR::Compare::Equal;
            }
        }

        ::HIR::Compare matchVal(const ::HIR::GenericRef& g, const ::HIR::ConstGeneric& sz) override {
            assert(g.binding < implVals.size());
            if (implVals.at(g.binding)) {
                DEBUG("Compare " << sz << " and " << *implVals.at(g.binding));
                return (sz == *implVals.at(g.binding) ? ::HIR::Compare::Equal : ::HIR::Compare::Unequal);
            } else {
                implVals.at(g.binding) = sz.clone();
                return ::HIR::Compare::Equal;
            }
        }

        ::HIR::Compare matchLft(const ::HIR::GenericRef& g, const ::HIR::LifetimeRef& lft) override {
            assert(g.binding < implLfts.size());
            if (implLfts.at(g.binding)) {
                DEBUG("Compare " << lft << " and " << *implLfts.at(g.binding));
                return (lft == *implLfts.at(g.binding) ? ::HIR::Compare::Equal : ::HIR::Compare::Unequal);
            } else {
                implLfts.at(g.binding) = lft;
                return HIR::Compare::Equal;
            }
        }

        ::HIR::TypeRef getType(const Span& sp, const ::HIR::GenericRef& g) const override {
            ASSERT_BUG(sp, g.group() == 0, "");
            ASSERT_BUG(sp, g.idx() < implTys.size(), "");
            if (!implTys[g.idx()]) {
                DEBUG("get_type - not populated, " << g);
                return types.generic(RcString(FMT("placeholder_" << &implTys << "_" << g.idx())), HIR::GenericRef(RcString(), HIR::GENERICPlaceholder, g.idx()).binding);
            }
            return *implTys[g.idx()];
        }

        ::HIR::ConstGeneric getValue(const Span& sp, const ::HIR::GenericRef& g) const override {
            ASSERT_BUG(sp, g.group() == 0, "");
            ASSERT_BUG(sp, g.idx() < implVals.size(), "");
            ASSERT_BUG(sp, implVals[g.idx()], "");
            return implVals[g.idx()]->clone();
        }

        ::HIR::LifetimeRef getLifetime(const Span& sp, const ::HIR::GenericRef& g) const override {
            ASSERT_BUG(sp, g.group() == 0, "");
            ASSERT_BUG(sp, g.idx() < implLfts.size(), "");
            if (!implLfts[g.idx()]) {
                DEBUG("WARNING: Assuming an empty lifetime");
                return HIR::LifetimeRef();
            }
            return *implLfts[g.idx()];
        }

        void reinit(const HIR::GenericParams& params) {
            this->implTys.clear();
            this->implVals.clear();
            this->implLfts.clear();
            this->implTys.resize(params.types.size());
            this->implVals.resize(params.values.size());
            this->implLfts.resize(params.mLifetimes.size());
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
            for (const auto& p : this->implLfts) {
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
bool ::HIR::TraitImpl::overlaps_with(const Crate& crate, const ::HIR::TraitImpl& other) const {
    // TODO: Pre-calculate impl trees (with pointers to parent impls)
    struct H {
        static bool types_overlap(const ::HIR::PathParams& a, const ::HIR::PathParams& b) {
            for (unsigned int i = 0; i < ::std::min(a.types.size(), b.types.size()); i++) {
                if (!H::types_overlap(a.types[i], b.types[i])) {
                    return false;
                }
            }
            return true;
        }

        static bool types_overlap_path(const ::HIR::Path& a, const ::HIR::Path& b) {
            if (a.mData.tag() != b.mData.tag()) {
                return false;
            }
            TU_MATCHA((a.mData, b.mData), (ape, bpe), (Generic, if (ape.mPath != bpe.mPath) return false; return H::types_overlap(ape.mParams, bpe.mParams);), (UfcsUnknown, ), (UfcsKnown, ), (UfcsInherent, ))
            DEBUG("TODO: Path - " << a << " and " << b);
            return false;
        }

        static bool types_overlap(const ::HIR::TypeData* a, const ::HIR::TypeData* b) {
            static Span sp;
            if (a == b) {
                return true;
            }
            //DEBUG("(" << a << "," << b << ")");
            if (a->is_Generic() || b->is_Generic()) {
                return true;
            }
            // TODO: Unbound/Opaque paths?
            if (a->tag() != b->tag()) {
                return false;
            }
            TU_MATCH_HDRA( ((*a), (*b)), {)
            TU_ARMA(Generic, ae, be) {
                }
                TU_ARMA(Infer, ae, be) {
                }
                TU_ARMA(Diverge, ae, be) {
                }
                TU_ARMA(NodeType, ae, be) {
                    BUG(sp, "Hit node-magic type (closure/generator/async) - " << a << " " << b);
                }
                TU_ARMA(Primitive, ae, be) {
                    if (ae != be) {
                        return false;
                    }
                }
                TU_ARMA(Path, ae, be) {
                    return types_overlap_path(ae.path, be.path);
                    //TODO(sp, "Path - " << ae.path << " and " << be.path);
                }
                TU_ARMA(TraitObject, ae, be) {
                    if (ae.mTrait.mPath.mPath != be.mTrait.mPath.mPath) {
                        return false;
                    }
                    if (!H::types_overlap(ae.mTrait.mPath.mParams, be.mTrait.mPath.mParams)) {
                        return false;
                    }
                    // Marker traits only overlap if the lists are the same (with overlap)
                    if (ae.markers.size() != be.markers.size()) {
                        return false;
                    }
                    for (size_t i = 0; i < ae.markers.size(); i++) {
                        if (ae.markers[i].mPath != be.markers[i].mPath) {
                            return false;
                        }
                        if (!H::types_overlap(ae.markers[i].mParams, be.markers[i].mParams)) {
                            return false;
                        }
                    }
                    return true;
                }
                TU_ARMA(ErasedType, ae, be) {
                    TODO(sp, "ErasedType - " << a);
                }
                TU_ARMA(NamedFunction, ae, be) {
                    return types_overlap_path(ae.path, be.path);
                }
                TU_ARMA(Function, ae, be) {
                    if (ae.is_unsafe != be.is_unsafe) {
                        return false;
                    }
                    if (ae.mAbi != be.mAbi) {
                        return false;
                    }
                    if (ae.argTypes.size() != be.argTypes.size()) {
                        return false;
                    }
                    for (unsigned int i = 0; i < ae.argTypes.size(); i++) {
                        if (!H::types_overlap(ae.argTypes[i], be.argTypes[i])) {
                            return false;
                        }
                    }
                    if (!H::types_overlap(ae.mRettype, be.mRettype)) {
                        return false;
                    }
                }
                TU_ARMA(Tuple, ae, be) {
                    if (ae.size() != be.size()) {
                        return false;
                    }
                    for (unsigned int i = 0; i < ae.size(); i++) {
                        if (!H::types_overlap(ae[i], be[i])) {
                            return false;
                        }
                    }
                }
                TU_ARMA(Slice, ae, be) {
                    return H::types_overlap(ae.inner, be.inner);
                }
                TU_ARMA(Array, ae, be) {
                    if (ae.size != be.size) {
                        return false;
                    }
                    return H::types_overlap(ae.inner, be.inner);
                }
                TU_ARMA(Pointer, ae, be) {
                    if (ae.type != be.type) {
                        return false;
                    }
                    return H::types_overlap(ae.inner, be.inner);
                }
                TU_ARMA(Borrow, ae, be) {
                    if (ae.type != be.type) {
                        return false;
                    }
                    return H::types_overlap(ae.inner, be.inner);
                }
            }
            return true;
        }
    };

    // Quick Check: If the types are equal, they do overlap
    if (this->mType == other.mType && this->traitArgs == other.traitArgs) {
        return true;
    }

    // 1. Are the impl types of the same form (or is one generic)
    if (!H::types_overlap(this->mType, other.mType)) {
        return false;
    }
    if (!H::types_overlap(this->traitArgs, other.traitArgs)) {
        return false;
    }

    DEBUG("TODO: Handle potential overlap (when not exactly equal)");
    //return this->m_type == other.m_type && this->m_trait_args == other.m_trait_args;
    Span sp;

    // TODO: Use `type_ord_specific` but treat any case of mixed ordering as this returning `false`
    try {
        type_ord_specific(sp, this->mType, other.mType);
        typelist_ord_specific(sp, this->traitArgs.types, other.traitArgs.types);
    } catch (const TypeOrdSpecificMixedOrdering& /*e*/) {
        return false;
    }

    // TODO: Detect `impl<T> Foo<T> for Bar<T>` vs `impl<T> Foo<&T> for Bar<T>`
    // > Create values for impl params from the type, then check if the trait params are compatible
    // > Requires two lists, and telling which one to use by the end
    auto cbIdent = ResolvePlaceholdersNop();
    bool isReversed = false;
    ImplTyMatcher matcher(crate.types);
    matcher.reinit(this->mParams);
    if (!this->mType->matchTestGenerics(sp, other.mType, cbIdent, matcher)) {
        DEBUG("- Type mismatch, try other ordering");
        isReversed = true;
        matcher.reinit(other.mParams);
        if (!other.mType->matchTestGenerics(sp, this->mType, cbIdent, matcher)) {
            DEBUG("- Type mismatch in both orderings");
            return false;
        }
        if (other.traitArgs.matchTestGenericsFuzz(sp, this->traitArgs, cbIdent, matcher) != ::HIR::Compare::Equal) {
            DEBUG("- Params mismatch");
            return false;
        }
        // Matched with second ordering
    } else if (this->traitArgs.matchTestGenericsFuzz(sp, other.traitArgs, cbIdent, matcher) != ::HIR::Compare::Equal) {
        DEBUG("- Param mismatch, try other ordering");
        isReversed = true;
        matcher.reinit(other.mParams);
        if (!other.mType->matchTestGenerics(sp, this->mType, cbIdent, matcher)) {
            DEBUG("- Type mismatch in alt ordering");
            return false;
        }
        if (other.traitArgs.matchTestGenericsFuzz(sp, this->traitArgs, cbIdent, matcher) != ::HIR::Compare::Equal) {
            DEBUG("- Params mismatch in alt ordering");
            return false;
        }
        // Matched with second ordering
    } else {
        // Matched with first ordering
    }

    struct H2 {
        static const ::HIR::TypeData* monomorph(const Span& sp, const ::HIR::TypeData* inTy, const Monomorphiser& ms, ::HIR::TypeRef& tmp) {
            if (!monomorphise_type_needed(inTy)) {
                return inTy;
            } else {
                tmp = ms.monomorph_type(sp, inTy);
                // TODO: EAT?
                return tmp;
            }
        }

        static const ::HIR::TraitPath& monomorph(const Span& sp, const ::HIR::TraitPath& in, const Monomorphiser& ms, ::HIR::TraitPath& tmp) {
            if (!monomorphise_traitpath_needed(in)) {
                return in;
            } else {
                tmp = ms.monomorph_traitpath(sp, in, true, false);
                // TODO: EAT?
                return tmp;
            }
        }

        static bool checkBounds(const ::HIR::Crate& crate, const ::HIR::TraitImpl& id, const Monomorphiser& ms, const ::HIR::TraitImpl& gSrc) {
            TRACE_FUNCTION;
            static Span sp;
            for (const auto& tb : id.mParams.bounds) {
                DEBUG(tb);
                if (tb.is_TraitBound()) {
                    ::HIR::TypeRef tmp_ty;
                    ::HIR::TraitPath tmp_tp;
                    const auto& ty = H2::monomorph(sp, tb.as_TraitBound().type, ms, tmp_ty);
                    const auto& trait = H2::monomorph(sp, tb.as_TraitBound().trait, ms, tmp_tp);
                    ;

                    // Determine if `ty` would be bounded (it's an ATY or generic)
                    if (ty->is_Generic()) {
                        bool found = false;
                        for (const auto& bound : gSrc.mParams.bounds) {
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
                            DEBUG("No matching bound for " << ty << " : " << trait << " in source bounds - " << gSrc.mParams.fmtBounds());
                            return false;
                        }
                    } else if (TU_TEST1((*ty), Path, .binding.is_Opaque())) {
                        TODO(sp, "Check bound " << ty << " : " << trait << " in source bounds or trait bounds");
                    } else {
                        // Search the crate for an impl
                        auto cbIdent = ResolvePlaceholdersNop();
                        bool rv = crate.findTraitImpls(trait.mPath.mPath, ty, cbIdent, [&](const ::HIR::TraitImpl& ti) -> bool {
                            DEBUG("impl" << ti.mParams.fmtArgs() << " " << trait.mPath.mPath << ti.traitArgs << " for " << ti.mType << ti.mParams.fmtBounds());

                            ImplTyMatcher matcher(crate.types);
                            matcher.reinit(ti.mParams);
                            // 1. Triple-check the type matches (and get generics)
                            if (!ti.mType->matchTestGenerics(sp, ty, cbIdent, matcher)) {
                                return false;
                            }
                            // 2. Check trait params
                            assert(trait.mPath.mParams.types.size() == ti.traitArgs.types.size());
                            for (size_t i = 0; i < trait.mPath.mParams.types.size(); i++) {
                                if (!ti.traitArgs.types[i]->matchTestGenerics(sp, trait.mPath.mParams.types[i], cbIdent, matcher)) {
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
    template <typename ImplType>
    bool findImplsList(const typename ::HIR::Crate::ImplGroup<::std::unique_ptr<ImplType>>::listT& implList, const ::HIR::TypeData* type, ::HIR::t_cb_resolve_type ty_res, ::std::function<bool(const ImplType&)> callback) {
        for (const auto& impl : implList) {
            if (impl->matchesType(type, ty_res)) {
                if (callback(*impl)) {
                    return true;
                }
            }
        }
        return false;
    }

    template <typename ImplType>
    bool findImplsList(const typename ::HIR::Crate::ImplGroup<const ImplType*>::listT& implList, const ::HIR::TypeData* type, ::HIR::t_cb_resolve_type ty_res, ::std::function<bool(const ImplType&)> callback) {
        for (const auto& impl : implList) {
            if (impl->matchesType(type, ty_res)) {
                if (callback(*impl)) {
                    return true;
                }
            }
        }
        return false;
    }
}

namespace {
    bool findTraitImplsInt(const ::HIR::Crate& crate, const ::HIR::SimplePath& trait, const ::HIR::TypeData* type, ::HIR::t_cb_resolve_type ty_res, ::std::function<bool(const ::HIR::TraitImpl&)> callback) {
        auto it = crate.traitImpls.find(trait);
        if (it != crate.traitImpls.end()) {
            // 1. Find named impls (associated with named types)
            if (const auto* implList = it->second.getListForType(type)) {
                if (findImplsList(*implList, type, ty_res, callback)) {
                    return true;
                }
            }
            // - If the type is an ivar, search all types
            if (type->is_Infer() && !type->as_Infer().isLit()) {
                DEBUG("Search all lists");
                for (const auto& list : it->second.named) {
                    if (findImplsList(list.second, type, ty_res, callback)) {
                        return true;
                    }
                }
            }

            // 2. Search fully generic list.
            if (findImplsList(it->second.generic, type, ty_res, callback)) {
                return true;
            }
        }

        return false;
    }

}

bool ::HIR::Crate::findTraitImpls(const ::HIR::SimplePath& trait, const ::HIR::TypeData* type, t_cb_resolve_type ty_res, ::std::function<bool(const ::HIR::TraitImpl&)> callback) const {
    if (this->allTraitImpls.size() > 0) {
        auto it = this->allTraitImpls.find(trait);
        if (it != this->allTraitImpls.end()) {
            // 1. Find named impls (associated with named types)
            if (const auto* implList = it->second.getListForType(type)) {
                if (findImplsList(*implList, type, ty_res, callback)) {
                    return true;
                }
            }
            // - If the type is an ivar, search all types
            if (type->is_Infer() && !type->as_Infer().isLit()) {
                DEBUG("Search all lists");
                for (const auto& list : it->second.named) {
                    if (findImplsList(list.second, type, ty_res, callback)) {
                        return true;
                    }
                }
            }

            // 2. Search fully generic list.
            if (findImplsList(it->second.generic, type, ty_res, callback)) {
                return true;
            }
        }

        return false;
    }

    // TODO: Determine the source crates for this type and trait (coherence) and only search those
    if (findTraitImplsInt(*this, trait, type, ty_res, callback)) {
        return true;
    }
    for (const auto& ec : this->extCrates) {
        if (findTraitImplsInt(*ec.second.mData, trait, type, ty_res, callback)) {
            return true;
        }
    }
    return false;
}

namespace {
    bool findAutoTraitImplsInt(const ::HIR::Crate& crate, const ::HIR::SimplePath& trait, const ::HIR::TypeData* type, ::HIR::t_cb_resolve_type ty_res, ::std::function<bool(const ::HIR::MarkerImpl&)> callback) {
        auto it = crate.markerImpls.find(trait);
        if (it != crate.markerImpls.end()) {
            // 1. Find named impls (associated with named types)
            if (const auto* implList = it->second.getListForType(type)) {
                if (findImplsList(*implList, type, ty_res, callback)) {
                    return true;
                }
            }

            // 2. Search fully generic list.
            if (findImplsList(it->second.generic, type, ty_res, callback)) {
                return true;
            }
        }

        return false;
    }
}

bool ::HIR::Crate::findAutoTraitImpls(const ::HIR::SimplePath& trait, const ::HIR::TypeData* type, t_cb_resolve_type ty_res, ::std::function<bool(const ::HIR::MarkerImpl&)> callback) const {
    if (this->allMarkerImpls.size() > 0) {
        auto it = this->allMarkerImpls.find(trait);
        if (it != this->allMarkerImpls.end()) {
            // 1. Find named impls (associated with named types)
            if (const auto* implList = it->second.getListForType(type)) {
                if (findImplsList(*implList, type, ty_res, callback)) {
                    return true;
                }
            }

            // 2. Search fully generic list.
            if (findImplsList(it->second.generic, type, ty_res, callback)) {
                return true;
            }
        }

        return false;
    }

    if (findAutoTraitImplsInt(*this, trait, type, ty_res, callback)) {
        return true;
    }
    for (const auto& ec : this->extCrates) {
        if (findAutoTraitImplsInt(*ec.second.mData, trait, type, ty_res, callback)) {
            return true;
        }
    }
    return false;
}

namespace {
    bool findTypeImplsInt(const ::HIR::Crate& crate, const ::HIR::TypeData* type, ::HIR::t_cb_resolve_type ty_res, ::std::function<bool(const ::HIR::TypeImpl&)> callback) {
        // 1. Find named impls (associated with named types)
        if (const auto* implList = crate.typeImpls.getListForType(type)) {
            if (findImplsList(*implList, type, ty_res, callback)) {
                return true;
            }
        }

        // 2. Search fully generic list?
        if (findImplsList(crate.typeImpls.generic, type, ty_res, callback)) {
            return true;
        }

        return false;
    }
}

bool ::HIR::Crate::findTypeImpls(const ::HIR::TypeData* type, t_cb_resolve_type ty_res, ::std::function<bool(const ::HIR::TypeImpl&)> callback) const {
    if (allTraitImpls.size() > 0) {
        // 1. Find named impls (associated with named types)
        if (const auto* implList = this->allTypeImpls.getListForType(type)) {
            if (findImplsList(*implList, type, ty_res, callback)) {
                return true;
            }
        }

        // 2. Search fully generic list?
        if (findImplsList(this->allTypeImpls.generic, type, ty_res, callback)) {
            return true;
        }

        return false;
    }
    // TODO: Determine the source crate for this type (coherence) and only search that

    // > Current crate
    if (findTypeImplsInt(*this, type, ty_res, callback)) {
        return true;
    }
    for (const auto& ec : this->extCrates) {
        //DEBUG("- " << ec.first);
        if (findTypeImplsInt(*ec.second.mData, type, ty_res, callback)) {
            return true;
        }
    }
    return false;
}

const ::MIR::Function* HIR::Crate::getOrGenMir(const ::HIR::ItemPath& ip, const ::HIR::ExprPtr& ep, const ::HIR::Function::argsT& args, ::HIR::TypeRef& ret_ty) const {
    if (!ep) {
        // No HIR, so has to just have MIR - from a extern crate most likely
        ASSERT_BUG(Span(), ep.mir, "No HIR (!ep) and no MIR (!ep.m_mir) for " << ip);
        return &*ep.mir;
    } else {
        if (!ep.mir) {
            TRACE_FUNCTION_F(ip);
            ASSERT_BUG(Span(), ep.state, "No ExprState for " << ip);

            auto& epMut = const_cast<::HIR::ExprPtr&>(ep);

            ::HIR::GenericPath current_trait;
            if (ep.state->currentTraitImpl) {
                current_trait.mPath = ep.state->currentTraitPath;
                current_trait.mParams = ep.state->currentTraitImpl->traitArgs.clone();
                // Lazy processing can be requested from Resolve UFCS Outer,
                // before the whole-crate Self-expansion pass has run.  Give
                // this body and its signature the same owner substitution.
                ConvertHIRExpandAliasesSelfExpr(
                    *this,
                    ep.state->currentTraitImpl->mType,
                    const_cast<::HIR::Function::argsT&>(args),
                    ret_ty,
                    epMut
                    );
            }

            // TODO: Ensure that all referenced items have constants evaluated
            if (ep.state->stage < ::HIR::ExprState::Stage::ConstEval) {
                if (ep.state->stage == ::HIR::ExprState::Stage::ConstEvalRequest) {
                    ERROR(Span(), E0000, "Loop in constant evaluation");
                }
                ep.state->stage = ::HIR::ExprState::Stage::ConstEvalRequest;
                ConvertHIRResolveUFCSExpr(*this, ip, epMut);
                ConvertHIRConstantEvaluateExpr(*this, ip, epMut);
                ep.state->stage = ::HIR::ExprState::Stage::ConstEval;
            }

            // Ensure typechecked
            if (ep.state->stage < ::HIR::ExprState::Stage::Typecheck) {
                if (ep.state->stage == ::HIR::ExprState::Stage::TypecheckRequest) {
                    ERROR(Span(), E0000, "Loop in constant evaluation");
                }
                ep.state->stage = ::HIR::ExprState::Stage::TypecheckRequest;

                // TODO: Set debug/timing stage
                //Debug_SetStagePre("HIR Typecheck");
                // - Can store that on the Expr, OR get it from the item path
                typeck::ModuleState ms{const_cast<::HIR::Crate&>(*this)};
                //ms.prepare_from_path( ip );   // <- Ideally would use this, but it's a lot of code for one usage
                ms.implGenerics = ep.state->implGenerics;
                ms.itemGenerics = ep.state->itemGenerics;
                ms.currentTrait = ep.state->currentTraitImpl ? &current_trait : nullptr;
                ms.currentTraitImpl = ep.state->currentTraitImpl;
                ms.traits = ep.state->traits;
                ms.modPaths.push_back(ep.state->modPath);
                TypecheckCode(ms, const_cast<::HIR::Function::argsT&>(args), ret_ty, epMut);
                // NOTE: This is already set by the above function
                ASSERT_BUG(Span(), ep.state->stage == ::HIR::ExprState::Stage::Typecheck, "Typecheck_Code didn't set stage");
            }
            if (ep.state->stage < ::HIR::ExprState::Stage::PostTypecheck) {
                //Debug_SetStagePre("Expand HIR Annotate");
                HIRExpandAnnotateUsageExpr(*this, ip, epMut);
                //Debug_SetStagePre("Expand HIR Statics Mark");
                HIRExpandStaticBorrowConstantsMarkExpr(*this, ip, epMut);
            }
            if (ep.state->stage < ::HIR::ExprState::Stage::Lifetimes) {
                //Debug_SetStagePre("Expand HIR Lifetimes");
                HIRExpandLifetimeInferExpr(*this, ip, args, ret_ty, epMut);
                ep.state->stage = ::HIR::ExprState::Stage::Lifetimes;
            }
            if (ep.state->stage < ::HIR::ExprState::Stage::Sbc) {
                if (ep.state->stage == ::HIR::ExprState::Stage::SbcRequest) {
                    ERROR(Span(), E0000, "Loop in constant evaluation");
                }
                ep.state->stage = ::HIR::ExprState::Stage::SbcRequest;
                //Debug_SetStagePre("Expand HIR Closures");
                HIRExpandClosuresExpr(*this, ret_ty, epMut);
                //Debug_SetStagePre("Expand HIR Statics");
                HIRExpandStaticBorrowConstantsExpr(*this, ip, epMut);
            }
            if (ep.state->stage < ::HIR::ExprState::Stage::Expand) {
                if (ep.state->stage == ::HIR::ExprState::Stage::ExpandRequest) {
                    ERROR(Span(), E0000, "Loop in constant evaluation");
                }
                ep.state->stage = ::HIR::ExprState::Stage::ExpandRequest;
                //Debug_SetStagePre("Expand HIR Calls");
                HIRExpandUfcsEverythingExpr(*this, epMut, ep.state->currentTraitImpl);
                //Debug_SetStagePre("Expand HIR Reborrows");
                HIRExpandReborrowsExpr(*this, epMut);
                //Debug_SetStagePre("Expand HIR ErasedType");
                //HIR_Expand_ErasedType(*this, ep_mut);    // - Maybe?
                //Typecheck_Expressions_Validate(*hir_crate);

                ep.state->stage = ::HIR::ExprState::Stage::Expand;
            }
            // Generate MIR
            if (ep.state->stage < ::HIR::ExprState::Stage::Mir) {
                if (ep.state->stage == ::HIR::ExprState::Stage::MirRequest) {
                    ERROR(Span(), E0000, "Loop in constant evaluation");
                }
                ep.state->stage = ::HIR::ExprState::Stage::MirRequest;
                //Debug_SetStage("Lower MIR");
                HIRGenerateMIRExpr(*this, ip, epMut, args, ret_ty);
                ep.state->stage = ::HIR::ExprState::Stage::Mir;
            }
            assert(ep.mir);
        }
        return &*ep.mir;
    }
}

::HIR::TypeRef HIR::Trait::getVtableType(const Span& sp, const ::HIR::Crate& crate, const ::HIR::TypeData::Data_TraitObject& te) const {
    assert(te.mTrait.traitPtr == this);

    const auto& vtable_ty_spath = this->vtablePath;
    const auto& vtable_ref = crate.getStructByPath(sp, vtable_ty_spath);
    HIR::PathParams pp_hrls;
    if (te.mTrait.hrtbs) {
        pp_hrls = te.mTrait.hrtbs->makeEmptyParams(true);
    }
    // Copy the param set from the trait in the trait object
    ::HIR::PathParams vtable_params = MonomorphHrlsOnly(crate.types, pp_hrls).monomorph_path_params(sp, te.mTrait.mPath.mParams, false);
    vtable_params.types.resize(te.mTrait.mPath.mParams.types.size() + this->typeIndexes.size());
    // - Include associated types on bound
    for (const auto& ty_b : te.mTrait.typeBounds) {
        if (this->typeIndexes.count(ty_b.first) == 0) {
            WARNING(sp, W0000, "Trait object path " << te.mTrait << " references a type with no vtable type index");
            continue;
        }
        auto idx = this->typeIndexes.at(ty_b.first);
        vtable_params.types.at(idx) = MonomorphHrlsOnly(crate.types, pp_hrls).monomorph_type(sp, ty_b.second.type);
    }
    return crate.types.path(::HIR::GenericPath(vtable_ty_spath, mv$(vtable_params)), &vtable_ref);
}

unsigned HIR::Trait::getVtableValueIndex(const HIR::GenericPath& trait_path, const RcString& name) const {
    auto its = this->valueIndexes.equal_range(name);
    for (auto it = its.first; it != its.second; ++it) {
        DEBUG(trait_path << " :: " << name << " - " << it->second.second);
        if (it->second.second.mPath == trait_path.mPath) {
            // TODO: Match generics using match_test_generics comparing to the trait args
            assert(it->second.first > 0);
            return it->second.first;
        }
    }
    return 0;
}

unsigned HIR::Trait::getVtableParentIndex(HIR::TypeInterner& types, const Span& sp, const HIR::PathParams& this_params, const HIR::GenericPath& trait_path) const {
    for (const auto& pt : this->allParentTraits) {
        if (pt.mPath.mPath == trait_path.mPath) {
            auto p = MonomorphStatePtr(types, nullptr, &this_params, nullptr).monomorph_genericpath(sp, pt.mPath);
            if (p == trait_path) {
                return vtableParentTraitsStart + (&pt - this->allParentTraits.data());
            }
        }
    }
    return 0;
}

::std::pair<const ::HIR::AssociatedType*, const ::HIR::PathParams*> HIR::Trait::getAtyDef(const RcString& name) const {
    auto it = types.find(name);
    if (it != types.end()) {
        return std::make_pair(&it->second, nullptr);
    }
    for (const auto& parent : allParentTraits) {
        it = parent.traitPtr->types.find(name);
        if (it != parent.traitPtr->types.end()) {
            return std::make_pair(&it->second, &parent.mPath.mParams);
        }
    }
    return std::make_pair(nullptr, nullptr);
}

/// Helper for getting the struct associated with a pattern path
const ::HIR::Struct& HIR::pattern_get_struct(const Span& sp, const ::HIR::Path& path, const ::HIR::Pattern::PathBinding& binding, bool isTuple) {
    const ::HIR::Struct* str_p = nullptr;
    TU_MATCH_HDRA( (binding), { )
    TU_ARMA(Unbound, be)
        BUG(sp, "Unexpected unbound named pattern - " << path);
        TU_ARMA(Struct, be) {
            str_p = be;
        }
        TU_ARMA(Union, be) {
            BUG(sp, "Tuple pattern used on union " << path);
        }
        TU_ARMA(Enum, be) {
            const auto& enm = *be.ptr;
            if (isTuple) {
                ASSERT_BUG(sp, enm.mData.is_Data(), "PathTuple pattern with non-data enum - " << path);
            } else {
                ASSERT_BUG(sp, enm.mData.is_Data(), "PathNamed pattern with non-data enum - " << path);
            }
            const auto& enmD = enm.mData.as_Data();
            ASSERT_BUG(sp, be.var_idx < enmD.size(), "Variant index " << be.var_idx << " out of range - " << path);
            if (isTuple) {
                ASSERT_BUG(sp, !enmD[be.var_idx].is_struct, "PathTuple pattern with brace enum variant - " << path);
            } else {
                ASSERT_BUG(sp, enmD[be.var_idx].is_struct, "PathNamed pattern with non-brace enum variant - " << path);
            }
            str_p = enmD[be.var_idx].type->as_Path().binding.as_Struct();
        }
    }
    const auto& str = *str_p;

    if(isTuple) {
        ASSERT_BUG(sp, str.mData.is_Tuple(), "PathTuple pattern with non-tuple struct - " << str.mData.tag_str());
    }
    else {
        ASSERT_BUG(sp, str.mData.is_Named(), "Struct pattern on non-brace struct");
    }
    return str;
}

const ::HIR::t_tuple_fields& HIR::pattern_get_tuple(const Span& sp, const ::HIR::Path& path, const ::HIR::Pattern::PathBinding& binding) {
    return pattern_get_struct(sp, path, binding, true).mData.as_Tuple();
}

const ::HIR::t_struct_fields& HIR::pattern_get_named(const Span& sp, const ::HIR::Path& path, const ::HIR::Pattern::PathBinding& binding) {
    if (binding.is_Union()) {
        return binding.as_Union()->mVariants;
    }
    return pattern_get_struct(sp, path, binding, false).mData.as_Named();
}

namespace HIR {
    EncodedLiteralPtr::EncodedLiteralPtr(EncodedLiteral el) {
        p = new EncodedLiteral(mv$(el));
    }

    EncodedLiteralPtr::~EncodedLiteralPtr() {
        if (p) {
            delete p;
            p = nullptr;
        }
    }
}

// ---
EncodedLiteral EncodedLiteral::makeUsize(uint64_t v) {
    EncodedLiteral rv;
    rv.bytes.resize(TargetGetPointerBits() / 8);
    rv.write_usize(0, v);
    return rv;
}

EncodedLiteral EncodedLiteral::clone() const {
    EncodedLiteral rv;
    rv.bytes = bytes;
    rv.relocations.reserve(relocations.size());
    for (const auto& r : relocations) {
        if (r.p) {
            rv.relocations.push_back(Reloc::new_named(r.ofs, r.len, r.p->clone()));
        } else {
            rv.relocations.push_back(Reloc::new_bytes(r.ofs, r.len, r.bytes));
        }
    }
    return rv;
}

void EncodedLiteral::write_uint(size_t ofs, size_t size, uint64_t v) {
    assert(ofs + size <= bytes.size());
    for (size_t i = 0; i < size; i++) {
        size_t bit = (TargetGetCurSpec().arch.bigEndian ? (size - 1 - i) * 8 : i * 8);
        if (bit < 64) {
            auto b = static_cast<uint8_t>(v >> bit);
            bytes[ofs + i] = b;
        }
    }
}

void EncodedLiteral::write_usize(size_t ofs, uint64_t v) {
    this->write_uint(ofs, TargetGetPointerBits() / 8, v);
}

uint64_t EncodedLiteral::read_usize(size_t ofs) const {
    return EncodedLiteralSlice(*this).slice(ofs).read_uint(TargetGetPointerBits() / 8).truncate_u64();
}

U128 EncodedLiteralSlice::read_uint(size_t size /*=0*/) const {
    if (size == 0) {
        size = mSize;
    }
    ASSERT_BUG(Span(), size <= mSize, "Over-large read (" << size << " > " << mSize << ")");
    U128 v(0);
    for (size_t i = 0; i < size; i++) {
        size_t bit = (TargetGetCurSpec().arch.bigEndian ? (size - 1 - i) * 8 : i * 8);
        if (bit < 128) {
            v |= U128(base.bytes[ofs + i]) << bit;
        }
    }
    DEBUG("(" << size << ") = " << v);
    return v;
}

S128 EncodedLiteralSlice::read_sint(size_t size /*=0*/) const {
    if (size == 0) {
        size = mSize;
    }
    auto v = read_uint(size);
    if (size < 128 / 8 && ((v >> (8 * size - 1)) != 0)) {
        // Sign extend
        v |= U128(UINT64_MAX, UINT64_MAX) << (8 * size);
    }
    DEBUG("(" << size << ") = " << v);
    return S128(v);
}

FloatValue EncodedLiteralSlice::read_float(size_t size /*=0*/) const {
    if (size == 0) {
        size = mSize;
    }
    assert(size <= mSize);
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
    if (mSize != x.mSize) {
        return false;
    }
    for (size_t i = 0; i < mSize; i++) {
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
    auto min_size = std::min(mSize, x.mSize);
    for (size_t i = 0; i < min_size; i++) {
        if (auto cmp = ::ord(base.bytes[ofs + i], x.base.bytes[x.ofs + i])) {
            return cmp;
        }
    }
    if (auto cmp = ::ord(mSize, x.mSize)) {
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
    for (size_t i = 0; i < x.mSize; i++) {
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
        if ((i + 1) % 8 == 0 && i + 1 < x.mSize) {
            os << " ";
        }
    }
    return os;
}

namespace HIR {

Publicity::Publicity(::std::shared_ptr<::HIR::SimplePath> p)
    : vis_path(p) {
}
Publicity Publicity::new_priv(::HIR::SimplePath p) {
    size_t n_comp = p.components().size();
    while (n_comp > 0 && p.components()[n_comp - 1].c_str()[0] == '#') {
        n_comp--;
    }
    auto s = std::span<const RcString>(p.components().data(), n_comp);
    return Publicity(::std::make_shared<HIR::SimplePath>(p.crate_name(), s));
}
Static::Static(Linkage linkage, bool is_mut, TypeRef type, ExprPtr value)
    : linkage(std::move(linkage))
    , isMut(is_mut)
    , mType(std::move(type))
    , mValue(std::move(value)) {
}
Constant::Constant() {
}
Constant::Constant(GenericParams params, TypeRef type, ExprPtr value)
    : mParams(::std::move(params))
    , mType(::std::move(type))
    , mValue(::std::move(value)) {
}
Function::Function() {
}
Function::Function(Receiver receiver, GenericParams params, argsT args, TypeRef ret_ty, ExprPtr code)
    : receiver(receiver)
    , mParams(std::move(params))
    , mArgs(std::move(args))
    , variadic(false)
    , returnType(std::move(ret_ty))
    , mCode(std::move(code)) {
}
Struct::FieldDefault::FieldDefault(size_t index, HIR::ExprPtr v)
    : index(index)
    , expr(std::move(v)) {
}
Struct::Struct(GenericParams params, Repr repr, Data data)
    : mParams(mv$(params))
    , repr(mv$(repr))
    , mData(mv$(data)) {
}
Struct::Struct(GenericParams params, Repr repr, Data data, unsigned align, TraitMarkings tm, StructMarkings sm)
    : mParams(mv$(params))
    , repr(mv$(repr))
    , mData(mv$(data))
    , forcedAlignment(align)
    , markings(mv$(tm))
    , structMarkings(mv$(sm)) {
}
AssociatedType::AssociatedType(
    ::HIR::GenericParams generics,
    bool is_sized,
    LifetimeRef lifetime_bound,
    ::std::vector<::HIR::TraitPath> trait_bounds,
    ::HIR::TypeRef defaultType
)
    : generics(::std::move(generics))
    , is_sized(is_sized)
    , lifetimeBound(lifetime_bound)
    , traitBounds(::std::move(trait_bounds))
    , hasDefault(defaultType && !defaultType->is_Infer())
    , defaultValue(defaultType) {
    assert(defaultType);
}
Trait::Trait(GenericParams gps, LifetimeRef lifetime, ::std::vector<::HIR::TraitPath> parents)
    : mParams(mv$(gps))
    , lifetime(mv$(lifetime))
    , parentTraits(mv$(parents))
    , isMarker(false)
    , isConst(false)
    , isCoinductive(false)
    , isFundamental(false)
    , vtableParentTraitsStart(0) {
}
Module::Module() {
}
Crate::Crate(stl::ObjPool* pool, TypeInterner& types)
    : pool(pool)
    , types(types)
    , intrinsicOffsetof(Function{Function::Receiver::Free, GenericParams{}, {}, types.primitive(CoreType::Usize), {}}) {
}
const ::HIR::Constant& Crate::getConstantByPath(const Span& sp, const ::HIR::SimplePath& path) const {
    const auto& ti = this->getValitemByPath(sp, path);
    TU_IFLET(::HIR::ValueItem, ti, Constant, e, return e;)
    else {
        BUG(sp, "`const` path " << path << " didn't point to an enum");
    }
}
const ::MIR::Function* Crate::getOrGenMir(const ::HIR::ItemPath& ip, const ::HIR::Function& fcn) const {
    auto ty = fcn.returnType;
    return getOrGenMir(ip, fcn.mCode, fcn.mArgs, ty);
}
const ::MIR::Function* Crate::getOrGenMir(const ::HIR::ItemPath& ip, const ::HIR::ExprPtr& ep, ::HIR::TypeRef& expTy) const {
    static ::HIR::Function::argsT s_args;
    return getOrGenMir(ip, ep, s_args, expTy);
}
}
