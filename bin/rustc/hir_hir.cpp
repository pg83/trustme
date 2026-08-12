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

    ConstGenericUnevaluated ConstGenericUnevaluated::monomorph(const Span& sp, const Monomorphiser& ms, bool allow_infer /*=true*/) const {
        ConstGenericUnevaluated rv;
        rv.params_impl = ms.monomorph_path_params(sp, params_impl, allow_infer);
        rv.params_item = ms.monomorph_path_params(sp, params_item, allow_infer);
        rv.expr = this->expr;
        return rv;
    }

    namespace {
        const ::HIR::ConstGeneric* get_unevaluated_param(const ::HIR::ConstGenericUnevaluated& value, unsigned int binding) {
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
            return index < params->m_values.size() ? &params->m_values[index] : nullptr;
        }

        bool const_expr_literals_equal(const ::HIR::ExprNodeLiteral& left, const ::HIR::ExprNodeLiteral& right) {
            if (left.m_data.tag() != right.m_data.tag()) {
                return false;
            }
            TU_MATCH_HDRA( (left.m_data, right.m_data), {)
            TU_ARMA(Integer, l, r) return l.m_type == r.m_type && l.m_value == r.m_value;
                TU_ARMA(Float, l, r) return l.m_type == r.m_type && l.m_value == r.m_value;
                TU_ARMA(Boolean, l, r) return l == r;
                TU_ARMA(String, l, r) return l == r;
                TU_ARMA(CString, l, r) return l.v == r.v;
                TU_ARMA(ByteString, l, r) return l == r;
            }
            throw "";
        }

        bool const_expr_nodes_equal(const ::HIR::ConstGenericUnevaluated& left_value, const ::HIR::ExprNode& left, const ::HIR::ConstGenericUnevaluated& right_value, const ::HIR::ExprNode& right) {
            if (const auto* l = cast<const ::HIR::ExprNodeConstParam>(&left)) {
                const auto* r = cast<const ::HIR::ExprNodeConstParam>(&right);
                if (!r) {
                    return false;
                }
                const auto* l_param = get_unevaluated_param(left_value, l->m_binding);
                const auto* r_param = get_unevaluated_param(right_value, r->m_binding);
                return l_param && r_param ? *l_param == *r_param : l->m_binding == r->m_binding;
            }
            if (const auto* l = cast<const ::HIR::ExprNodeLiteral>(&left)) {
                const auto* r = cast<const ::HIR::ExprNodeLiteral>(&right);
                return r && const_expr_literals_equal(*l, *r);
            }
            if (const auto* l = cast<const ::HIR::ExprNodeBinOp>(&left)) {
                const auto* r = cast<const ::HIR::ExprNodeBinOp>(&right);
                return r && l->m_op == r->m_op && const_expr_nodes_equal(left_value, *l->m_left, right_value, *r->m_left) && const_expr_nodes_equal(left_value, *l->m_right, right_value, *r->m_right);
            }
            if (const auto* l = cast<const ::HIR::ExprNodeUniOp>(&left)) {
                const auto* r = cast<const ::HIR::ExprNodeUniOp>(&right);
                return r && l->m_op == r->m_op && const_expr_nodes_equal(left_value, *l->m_value, right_value, *r->m_value);
            }
            if (const auto* l = cast<const ::HIR::ExprNodeCast>(&left)) {
                const auto* r = cast<const ::HIR::ExprNodeCast>(&right);
                return r && l->m_dst_type == r->m_dst_type && const_expr_nodes_equal(left_value, *l->m_value, right_value, *r->m_value);
            }
            if (const auto* l = cast<const ::HIR::ExprNodeConstBlock>(&left)) {
                const auto* r = cast<const ::HIR::ExprNodeConstBlock>(&right);
                return r && const_expr_nodes_equal(left_value, *l->m_inner, right_value, *r->m_inner);
            }
            if (const auto* l = cast<const ::HIR::ExprNodeCallPath>(&left)) {
                const auto* r = cast<const ::HIR::ExprNodeCallPath>(&right);
                if (!r || l->m_path != r->m_path || l->m_args.size() != r->m_args.size()) {
                    return false;
                }
                for (unsigned int i = 0; i < l->m_args.size(); i++) {
                    if (!const_expr_nodes_equal(left_value, *l->m_args[i], right_value, *r->m_args[i])) {
                        return false;
                    }
                }
                return true;
            }
            if (const auto* l = cast<const ::HIR::ExprNodeBlock>(&left)) {
                const auto* r = cast<const ::HIR::ExprNodeBlock>(&right);
                if (!r || l->m_nodes.size() != r->m_nodes.size() || static_cast<bool>(l->m_value_node) != static_cast<bool>(r->m_value_node)) {
                    return false;
                }
                for (unsigned int i = 0; i < l->m_nodes.size(); i++) {
                    if (!const_expr_nodes_equal(left_value, *l->m_nodes[i], right_value, *r->m_nodes[i])) {
                        return false;
                    }
                }
                return !l->m_value_node || const_expr_nodes_equal(left_value, *l->m_value_node, right_value, *r->m_value_node);
            }
            return false;
        }
    }

    bool ConstGenericUnevaluated::equivalent(const ConstGenericUnevaluated& x) const {
        return const_expr_nodes_equal(*this, **this->expr, x, **x.expr);
    }

    Ordering ConstGenericUnevaluated::ord(const ConstGenericUnevaluated& x) const {
        if (this->expr.get() != x.expr.get()) {
            // If only one has populated MIR, they can't be equal (sort populated MIR after)
            if (!this->expr->m_mir != !x.expr->m_mir) {
                return (this->expr->m_mir ? OrdGreater : OrdLess);
            }

            // HACK: If the inner is a const param on both, sort based on that.
            // - Very similar to the ordering of TypeRef::Generic
            const auto* tn = cast<const HIR::ExprNodeConstParam>(&**this->expr);
            const auto* xn = cast<const HIR::ExprNodeConstParam>(&**x.expr);
            if (tn && xn) {
                // Is this valid? What if they're from different scopes?
                return ::ord(tn->m_binding, xn->m_binding);
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
        if (expr->m_mir) {
            for (const auto& b : expr->m_mir->blocks) {
                os << "bb" << (&b - expr->m_mir->blocks.data()) << ":{ ";
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
            } inner_os(os);

            HIRDumpExpr(inner_os, *expr);
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

bool HIR::Publicity::is_visible(const ::HIR::SimplePath& p) const {
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

::HIR::TypeRef HIR::Function::make_ptr_ty(const Span& sp, const Monomorphiser& ms) const {
    ::HIR::TypeDataFunctionPointer ft;
    ft.is_unsafe = this->m_unsafe;
    ft.is_variadic = this->m_variadic;
    ft.m_abi = this->m_abi;
    ft.m_rettype = ms.monomorph_type(sp, this->m_return);
    ft.m_arg_types.reserve(this->m_args.size());
    for (const auto& arg : this->m_args) {
        ft.m_arg_types.push_back(ms.monomorph_type(sp, arg.second));
    }
    return ms.type_interner().function(std::move(ft));
}

::HIR::TypeRef HIR::fn_ptr_tuple_constructor(const Span& sp, const Monomorphiser& ms, HIR::TypeRef ret_ty, const t_tuple_fields& fields) {
    ::HIR::TypeDataFunctionPointer ft;
    ft.is_unsafe = false;
    ft.is_variadic = false;
    ft.m_abi = RcString::new_interned(ABI_RUST);
    ft.m_rettype = std::move(ret_ty);
    ft.m_arg_types.reserve(fields.size());
    for (const auto& fld : fields) {
        ft.m_arg_types.push_back(ms.monomorph_type(sp, fld.ent));
    }
    return ms.type_interner().function(std::move(ft));
}

size_t HIR::Enum::find_variant(const RcString& name) const {
    if (m_data.is_Value()) {
        const auto& e = m_data.as_Value();
        auto it = ::std::find_if(e.variants.begin(), e.variants.end(), [&](const auto& x) {
            return x.name == name;
        });
        if (it == e.variants.end()) {
            return SIZE_MAX;
        }
        return it - e.variants.begin();
    } else {
        const auto& e = m_data.as_Data();

        auto it = ::std::find_if(e.begin(), e.end(), [&](const auto& x) {
            return x.name == name;
        });
        if (it == e.end()) {
            return SIZE_MAX;
        }
        return it - e.begin();
    }
}

bool HIR::Enum::is_value() const {
    return this->m_data.is_Value();
}

U128 HIR::Enum::get_value(size_t idx) const {
    if (m_data.is_Value()) {
        const auto& e = m_data.as_Value();
        assert(idx < e.variants.size());

        return e.variants[idx].val;
    } else {
        assert(!"TODO: Enum::get_value on non-value enum?");
        throw "";
    }
}

/*static*/ ::HIR::CoreType HIR::Enum::get_repr_type(Repr r) {
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

const ::HIR::SimplePath& ::HIR::Crate::get_lang_item_path(const Span& sp, const char* name) const {
    auto it = this->m_lang_items.find(name);
    if (it == this->m_lang_items.end()) {
        ERROR(sp, E0000, "Undefined language item '" << name << "' required");
    }
    return it->second;
}

const ::HIR::SimplePath& ::HIR::Crate::get_lang_item_path_opt(const char* name) const {
    static ::HIR::SimplePath empty_path;
    auto it = this->m_lang_items.find(name);
    if (it == this->m_lang_items.end()) {
        return empty_path;
    }
    return it->second;
}

namespace {
    const ::HIR::Module& get_containing_module(const ::HIR::Crate& crate, const Span& sp, const ::HIR::SimplePath& path, bool ignore_crate_name, bool ignore_last_node) {
        ASSERT_BUG(sp, path.components().size() > 0u, "Invalid path (no nodes) - " << path);
        ASSERT_BUG(sp, path.components().size() > (ignore_last_node ? 1u : 0u), "Invalid path (only one node with `ignore_last_node` - " << path);

        const ::HIR::Module* mod;
        if (!ignore_crate_name && path.crate_name() != crate.m_crate_name) {
            ASSERT_BUG(sp, crate.m_ext_crates.count(path.crate_name()) > 0, "Crate '" << path.crate_name() << "' not loaded for " << path);
            mod = &crate.m_ext_crates.at(path.crate_name()).m_data->m_root_module;
        } else {
            mod = &crate.m_root_module;
        }
        for (unsigned int i = 0; i < path.components().size() - (ignore_last_node ? 2 : 1); i++) {
            const auto& pc = path.components()[i];
            auto it = mod->m_mod_items.find(pc);
            if (it == mod->m_mod_items.end()) {
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

const ::HIR::MacroItem& ::HIR::Crate::get_macroitem_by_path(const Span& sp, const ::HIR::SimplePath& path, bool ignore_crate_name, bool ignore_last_node) const {
    const auto& mod = get_containing_module(*this, sp, path, ignore_crate_name, ignore_last_node);

    auto it = mod.m_macro_items.find(ignore_last_node ? path.components()[path.components().size() - 2] : path.components().back());
    if (it == mod.m_macro_items.end()) {
        BUG(sp, "Could not find macro name in " << path);
    }

    return it->second->ent;
}

const ::HIR::TypeItem& ::HIR::Crate::get_typeitem_by_path(const Span& sp, const ::HIR::SimplePath& path, bool ignore_crate_name, bool ignore_last_node) const {
    const auto& mod = get_containing_module(*this, sp, path, ignore_crate_name, ignore_last_node);

    auto it = mod.m_mod_items.find(ignore_last_node ? path.components()[path.components().size() - 2] : path.components().back());
    if (it == mod.m_mod_items.end()) {
        BUG(sp, "Could not find type " << path);
    }

    return it->second->ent;
}

const ::HIR::Module& ::HIR::Crate::get_mod_by_path(const Span& sp, const ::HIR::SimplePath& path, bool ignore_last_node /*=false*/, bool ignore_crate_name /*=false*/) const {
    if (ignore_last_node) {
        ASSERT_BUG(sp, path.components().size() > 0, "get_mod_by_path received invalid path with ignore_last_node=true - " << path);
    }
    // Special handling for empty paths with `ignore_last_node`
    if (path.components().size() == (ignore_last_node ? 1 : 0)) {
        if (!ignore_crate_name && path.crate_name() != m_crate_name) {
            ASSERT_BUG(sp, m_ext_crates.count(path.crate_name()) > 0, "Crate '" << path.crate_name() << "' not loaded");
            return m_ext_crates.at(path.crate_name()).m_data->m_root_module;
        } else {
            return this->m_root_module;
        }
    } else {
        const auto& ti = this->get_typeitem_by_path(sp, path, ignore_crate_name, ignore_last_node);
        if (auto* e = ti.opt_Module()) {
            return *e;
        } else {
            if (ignore_last_node) {
                BUG(sp, "Parent path of " << path << " didn't point to a module");
            } else {
                BUG(sp, "Module path " << path << " didn't point to a module");
            }
        }
    }
}

const ::HIR::Trait& ::HIR::Crate::get_trait_by_path(const Span& sp, const ::HIR::SimplePath& path) const {
    const auto& ti = this->get_typeitem_by_path(sp, path);
    TU_IFLET(::HIR::TypeItem, ti, Trait, e, return e;)
    else {
        BUG(sp, "Trait path " << path << " didn't point to a trait (" << ti.tag_str() << ")");
    }
}

::std::optional<size_t> HIR::Crate::find_most_specific_trait(
    const Span& sp,
    const ::std::vector<::HIR::SimplePath>& candidates
) const {
    ::std::optional<size_t> selected;
    for (size_t candidate_index = 0; candidate_index < candidates.size(); candidate_index++) {
        const auto& candidate = candidates[candidate_index];
        const auto& trait = this->get_trait_by_path(sp, candidate);
        bool is_subtrait_of_all = true;

        for (const auto& other : candidates) {
            if (candidate == other) {
                continue;
            }
            const bool has_supertrait = ::std::any_of(
                trait.m_all_parent_traits.begin(),
                trait.m_all_parent_traits.end(),
                [&](const auto& parent) {
                    return parent.m_path.m_path == other;
                }
            );
            if (!has_supertrait) {
                is_subtrait_of_all = false;
                break;
            }
        }

        if (!is_subtrait_of_all) {
            continue;
        }
        if (selected && candidates[*selected] != candidate) {
            return {};
        }
        if (!selected) {
            selected = candidate_index;
        }
    }
    return selected;
}

const ::HIR::Struct& ::HIR::Crate::get_struct_by_path(const Span& sp, const ::HIR::SimplePath& path) const {
    const auto& ti = this->get_typeitem_by_path(sp, path);
    TU_IFLET(::HIR::TypeItem, ti, Struct, e, return e;)
    else {
        BUG(sp, "Struct path " << path << " didn't point to a struct (" << ti.tag_str() << ")");
    }
}

const ::HIR::Union& ::HIR::Crate::get_union_by_path(const Span& sp, const ::HIR::SimplePath& path) const {
    const auto& ti = this->get_typeitem_by_path(sp, path);
    TU_IFLET(::HIR::TypeItem, ti, Union, e, return e;)
    else {
        BUG(sp, "Path " << path << " didn't point to a union (" << ti.tag_str() << ")");
    }
}

const ::HIR::Enum& ::HIR::Crate::get_enum_by_path(const Span& sp, const ::HIR::SimplePath& path, bool ignore_crate_name, bool ignore_last_node) const {
    const auto& ti = this->get_typeitem_by_path(sp, path, ignore_crate_name, ignore_last_node);
    TU_IFLET(::HIR::TypeItem, ti, Enum, e, return e;)
    else {
        BUG(sp, "Enum path " << path << " didn't point to an enum (" << ti.tag_str() << ")");
    }
}

const ::HIR::ValueItem& ::HIR::Crate::get_valitem_by_path(const Span& sp, const ::HIR::SimplePath& path, bool ignore_crate_name) const {
    if (path.crate_name() == "#intrinsics") {
        ASSERT_BUG(sp, path.components().size() == 1, "");
        if (path.components().back() == "offset_of") {
            if (!m_intrinsic_offsetof.as_Function().m_variadic) {
                auto& v = m_intrinsic_offsetof.as_Function();
                v.m_variadic = true;
                v.m_params.m_types.push_back(HIR::TypeParamDef{RcString::new_interned("T"), m_types.infer(), false});
            }
            return m_intrinsic_offsetof;
        }
        TODO(sp, "Get intrinsic " << path.components().back());
    }
    if (path.crate_name() == this->m_crate_name && path.components().size() == 1) {
        auto i = std::find_if(m_new_values.begin(), m_new_values.end(), [&](const auto& v) {
            return v.first == path.components().back();
        });
        if (i != m_new_values.end()) {
            return i->second->ent;
        }
    }
    const auto& mod = get_containing_module(*this, sp, path, ignore_crate_name, /*ignore_last_node=*/false);

    auto it = mod.m_value_items.find(path.components().back());
    if (it == mod.m_value_items.end()) {
        BUG(sp, "Could not find value name " << path);
    }

    return it->second->ent;
}

const ::HIR::Function& ::HIR::Crate::get_function_by_path(const Span& sp, const ::HIR::SimplePath& path) const {
    const auto& ti = this->get_valitem_by_path(sp, path);
    TU_IFLET(::HIR::ValueItem, ti, Function, e, return e;)
    else {
        BUG(sp, "Function path " << path << " didn't point to an function (" << ti.tag_str() << ")");
    }
}

const ::HIR::Static& ::HIR::Crate::get_static_by_path(const Span& sp, const ::HIR::SimplePath& path) const {
    const auto& m = this->get_mod_by_path(sp, path, /*ignore_last*/ true);
    auto it = m.m_value_items.find(path.components().back());
    if (it != m.m_value_items.end()) {
        ASSERT_BUG(sp, it->second->ent.is_Static(), "`static` path " << path << " didn't point to a static - " << it->second->ent.tag_str());
        return it->second->ent.as_Static();
    }
    for (const auto& e : m.m_inline_statics) {
        if (e.first == path.components().back()) {
            return *e.second;
        }
    }
    if (path.crate_name() == this->m_crate_name && path.components().size() == 1) {
        auto i = std::find_if(m_new_values.begin(), m_new_values.end(), [&](const auto& v) {
            return v.first == path.components().back();
        });
        if (i != m_new_values.end()) {
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
    bool is_unbounded_infer(const ::HIR::TypeData* type) {
        if (const auto* e = type->opt_Infer()) {
            return e->ty_class == ::HIR::InferClass::None;
        } else {
            return false;
        }
    }

    class ImplMatcher: public ::HIR::MatchGenerics {
        std::vector<std::optional<HIR::TypeRef>> impl_types;

    public:
        ImplMatcher(const ::HIR::GenericParams& impl_generics)
            : impl_types(impl_generics.m_types.size())
        {
        }

        ::HIR::Compare match_ty(const ::HIR::GenericRef& g, const ::HIR::TypeData* ty, ::HIR::t_cb_resolve_type resolve_cb) override {
            assert(g.binding < impl_types.size());
            if (impl_types[g.binding]) {
                return (*impl_types[g.binding])->compare_with_placeholders(Span(), ty, resolve_cb);
            }
            impl_types[g.binding] = ty;
            return ::HIR::Compare::Equal;
        }

        ::HIR::Compare match_val(const ::HIR::GenericRef& g, const ::HIR::ConstGeneric& sz) override {
            // TODO
            //assert( g.binding < impl_params.m_values.size() );
            //impl_params.m_values[g.binding] = sz.clone();
            return ::HIR::Compare::Equal;
        }
    };

    bool matches_type_root(const ::HIR::GenericParams& params, const ::HIR::TypeData* impl_ty, const ::HIR::TypeData* match_type, ::HIR::t_cb_resolve_type ty_res) {
        // A nominal path deserialises without its pointer-valued binding
        // metadata. Its SimplePath is nevertheless complete and is exactly
        // what the impl index and matcher use. Only an unresolved UFCS path is
        // still too early to select an inherent impl.
        const auto* match_path = match_type->opt_Path();
        if (is_unbounded_infer(match_type)
            || (match_path && match_path->binding.is_Unbound()
                && !match_path->path.m_data.is_Generic())) {
            return false;
        }
#if 1
        ImplMatcher m{params};
        auto cmp = impl_ty->match_test_generics_fuzz(Span(), match_type, ty_res, m);
        return cmp != HIR::Compare::Unequal;
#else
        return matches_type_int(impl_ty, match_type, ty_res, true);
#endif
    }
}

bool ::HIR::TraitImpl::matches_type(const ::HIR::TypeData* type, ::HIR::t_cb_resolve_type ty_res) const {
    // NOTE: Don't return any impls when the type is an unbouned ivar. Wouldn't be able to pick anything anyway
    // TODO: For `Unbound`, it could be valid, if the target is a generic.
    // - Pure infer could also be useful (for knowing if there's any other potential impls)

    // HACK: Assume an unbounded matches
    if (is_unbounded_infer(type)) {
        return true;
    }
    return matches_type_root(m_params, m_type, type, ty_res);
}

bool ::HIR::TypeImpl::matches_type(const ::HIR::TypeData* type, ::HIR::t_cb_resolve_type ty_res) const {
    return matches_type_root(m_params, m_type, type, ty_res);
}

bool ::HIR::MarkerImpl::matches_type(const ::HIR::TypeData* type, ::HIR::t_cb_resolve_type ty_res) const {
    return matches_type_root(m_params, m_type, type, ty_res);
}

namespace {

    struct TypeOrdSpecificMixedOrdering {};

    ::Ordering typelist_ord_specific(const Span& sp, const ThinVector<::HIR::TypeRef>& left, const ThinVector<::HIR::TypeRef>& right);
    ::Ordering typelist_ord_specific(const Span& sp, const ::std::vector<::HIR::TypeRef>& left, const ::std::vector<::HIR::TypeRef>& right);

    ::Ordering array_size_ord_specific(
        const Span& sp,
        const ::HIR::ArraySize& left,
        const ::HIR::ArraySize& right
    ) {
        if (left == right) {
            return ::OrdEqual;
        }
        const bool left_open = left.is_Unevaluated();
        const bool right_open = right.is_Unevaluated();
        if (left_open != right_open) {
            return left_open ? ::OrdLess : ::OrdGreater;
        }
        if (left_open) {
            // Two independently named const parameters are equally general.
            // Relations between them are accounted for by the surrounding
            // impl matcher; neither is more specific on syntax alone.
            return ::OrdEqual;
        }
        BUG(sp, "Mismatched const values - " << left << " and " << right);
    }

    ::Ordering combine_specificity(::Ordering left, ::Ordering right) {
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
                if (!right->is_Path() || le.path.m_data.tag() != right->as_Path().path.m_data.tag()) {
                    BUG(sp, "Mismatched types - " << left << " and " << right);
                }
                TU_MATCHA((le.path.m_data, right->as_Path().path.m_data), (lpe, rpe), (Generic, if (lpe.m_path != rpe.m_path) BUG(sp, "Mismatched types - " << left << " and " << right); return typelist_ord_specific(sp, lpe.m_params.m_types, rpe.m_params.m_types);), (UfcsUnknown, ), (UfcsKnown, ), (UfcsInherent, ))
                TODO(sp, "Path - " << le.path << " and " << right);
            }
            TU_ARMA(TraitObject, le) {
                ASSERT_BUG(sp, right->is_TraitObject(), "Mismatched types - " << left << " vs " << right);
                const auto& re = right->as_TraitObject();
                ASSERT_BUG(sp, le.m_trait.m_path.m_path == re.m_trait.m_path.m_path, "Mismatched types - " << left << " vs " << right);
                ASSERT_BUG(sp, le.m_markers.size() == re.m_markers.size(), "Mismatched types - " << left << " vs " << right);

                auto ord = typelist_ord_specific(sp, le.m_trait.m_path.m_params.m_types, re.m_trait.m_path.m_params.m_types);
                if (ord != ::OrdEqual) {
                    return ord;
                }
                for (size_t i = 0; i < le.m_markers.size(); i++) {
                    ASSERT_BUG(sp, le.m_markers[i].m_path == re.m_markers[i].m_path, "Mismatched types - " << left << " vs " << right);
                    ord = typelist_ord_specific(sp, le.m_markers[i].m_params.m_types, re.m_markers[i].m_params.m_types);
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
                    return combine_specificity(
                        type_ord_specific(sp, le.inner, re->inner),
                        array_size_ord_specific(sp, le.size, re->size)
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
    void add_bound_from_trait(HIR::TypeInterner& types, ::std::vector<::HIR::GenericBound>& rv, const std::unique_ptr<HIR::GenericParams>& hrtbs, const ::HIR::TypeData* type, const ::HIR::TraitPath& cur_trait) {
        static Span sp;
        assert(cur_trait.m_trait_ptr);
        const auto& tr = *cur_trait.m_trait_ptr;
        auto monomorph_cb = MonomorphStatePtr(types, type, &cur_trait.m_path.m_params, nullptr);

        for (const auto& trait_path_raw : tr.m_all_parent_traits) {
            // 1. Monomorph
            auto trait_path_mono = monomorph_cb.monomorph_traitpath(sp, trait_path_raw, false, false);
            // 2. Add
            rv.push_back(::HIR::GenericBound::make_TraitBound({hrtbs ? box$(hrtbs->clone()) : nullptr, type, mv$(trait_path_mono)}));
        }

        // TODO: Add traits from `Self: Foo` bounds?
        // TODO: Move associated types to the source trait.
    }

    ::std::vector<::HIR::GenericBound> flatten_bounds(HIR::TypeInterner& types, const ::std::vector<::HIR::GenericBound>& bounds) {
        ::std::vector<::HIR::GenericBound> rv;
        for (const auto& b : bounds) {
            rv.push_back(b.clone());
            if (const auto* be = b.opt_TraitBound()) {
                add_bound_from_trait(types, rv, be->hrtbs, be->type, be->trait);
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
        auto ord = typelist_ord_specific(sp, this->m_trait_args.m_types, other.m_trait_args.m_types);
        if (ord != ::OrdEqual) {
            DEBUG("- Trait arguments " << (ord == ::OrdLess ? "less" : "more") << " specific");
            return ord == ::OrdGreater;
        }

        ord = type_ord_specific(sp, this->m_type, other.m_type);
        // If `*this` < `other` : false
        if (ord != ::OrdEqual) {
            DEBUG("- Type " << this->m_type << " " << (ord == ::OrdLess ? "less" : "more") << " specific than " << other.m_type);
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
    auto bounds_t = flatten_bounds(types, m_params.m_bounds);
    auto bounds_o = flatten_bounds(types, other.m_params.m_bounds);

    DEBUG("bounds_t = " << bounds_t);
    DEBUG("bounds_o = " << bounds_o);

    // If there are less bounds in this impl, it can't be more specific.
    if (bounds_t.size() < bounds_o.size()) {
        DEBUG("Bound count");
        return false;
    }

    auto it_t = bounds_t.begin();
    auto it_o = bounds_o.begin();
    bool is_equal = true;
    while (it_t != bounds_t.end() && it_o != bounds_o.end()) {
        auto cmp = ::ord(*it_t, *it_o);
        // Equal bounds? advance both
        if (cmp == OrdEqual) {
            ++it_t;
            ++it_o;
            continue;
        }

        // If the two bounds are similar
        if (it_t->tag() == it_o->tag() && it_t->is_TraitBound()) {
            const auto& b_t = it_t->as_TraitBound();
            const auto& b_o = it_o->as_TraitBound();
            // Check if the type is equal
            if (b_t.type == b_o.type && b_t.trait.m_path.m_path == b_o.trait.m_path.m_path) {
                const auto& params_t = b_t.trait.m_path.m_params;
                const auto& params_o = b_o.trait.m_path.m_params;
                switch (typelist_ord_specific(sp, params_t.m_types, params_o.m_types)) {
                    case ::OrdLess:
                        return false;
                    case ::OrdGreater:
                        return true;
                    case ::OrdEqual:
                        break;
                }
                // TODO: Find cases where there's `T: Foo<T>` and `T: Foo<U>`
                for (unsigned int i = 0; i < params_t.m_types.size(); i++) {
                    if (params_t.m_types[i] != params_o.m_types[i] && params_t.m_types[i] == b_t.type) {
                        return true;
                    }
                }
                TODO(sp, *it_t << " ?= " << *it_o);
            }
        }

        if (cmp == OrdLess) {
            is_equal = false;
            ++it_t;
        } else {
            //++ it_o;
            DEBUG(*it_t << " ?= " << *it_o << " : " << cmp);
            return false;
        }
    }
    if (it_t != bounds_t.end()) {
        DEBUG("Remaining local bounds - " << *it_t);
        return true;
    } else {
        DEBUG("Out of local bounds, equal or less specific");
        return !is_equal;
    }
}

namespace {

    struct ImplTyMatcher: public ::HIR::MatchGenerics, public Monomorphiser {
        ::std::vector<::std::optional<::HIR::TypeRef>> impl_tys;
        ::std::vector<::std::optional<::HIR::ConstGeneric>> impl_vals;
        ::std::vector<::std::optional<::HIR::LifetimeRef>> impl_lfts;

        explicit ImplTyMatcher(HIR::TypeInterner& types)
            : Monomorphiser(types)
        {
        }

        ::HIR::Compare match_ty(const ::HIR::GenericRef& g, const ::HIR::TypeData* ty, ::HIR::t_cb_resolve_type _resolve_cb) override {
            assert(g.binding < impl_tys.size());
            if (impl_tys.at(g.binding)) {
                DEBUG("Compare " << ty << " and " << *impl_tys.at(g.binding));
                return (ty == *impl_tys.at(g.binding) ? ::HIR::Compare::Equal : ::HIR::Compare::Unequal);
            } else {
                impl_tys.at(g.binding) = ty;
                return ::HIR::Compare::Equal;
            }
        }

        ::HIR::Compare match_val(const ::HIR::GenericRef& g, const ::HIR::ConstGeneric& sz) override {
            assert(g.binding < impl_vals.size());
            if (impl_vals.at(g.binding)) {
                DEBUG("Compare " << sz << " and " << *impl_vals.at(g.binding));
                return (sz == *impl_vals.at(g.binding) ? ::HIR::Compare::Equal : ::HIR::Compare::Unequal);
            } else {
                impl_vals.at(g.binding) = sz.clone();
                return ::HIR::Compare::Equal;
            }
        }

        ::HIR::Compare match_lft(const ::HIR::GenericRef& g, const ::HIR::LifetimeRef& lft) override {
            assert(g.binding < impl_lfts.size());
            if (impl_lfts.at(g.binding)) {
                DEBUG("Compare " << lft << " and " << *impl_lfts.at(g.binding));
                return (lft == *impl_lfts.at(g.binding) ? ::HIR::Compare::Equal : ::HIR::Compare::Unequal);
            } else {
                impl_lfts.at(g.binding) = lft;
                return HIR::Compare::Equal;
            }
        }

        ::HIR::TypeRef get_type(const Span& sp, const ::HIR::GenericRef& g) const override {
            ASSERT_BUG(sp, g.group() == 0, "");
            ASSERT_BUG(sp, g.idx() < impl_tys.size(), "");
            if (!impl_tys[g.idx()]) {
                DEBUG("get_type - not populated, " << g);
                return m_types.generic(RcString(FMT("placeholder_" << &impl_tys << "_" << g.idx())), HIR::GenericRef(RcString(), HIR::GENERICPlaceholder, g.idx()).binding);
            }
            return *impl_tys[g.idx()];
        }

        ::HIR::ConstGeneric get_value(const Span& sp, const ::HIR::GenericRef& g) const override {
            ASSERT_BUG(sp, g.group() == 0, "");
            ASSERT_BUG(sp, g.idx() < impl_vals.size(), "");
            ASSERT_BUG(sp, impl_vals[g.idx()], "");
            return impl_vals[g.idx()]->clone();
        }

        ::HIR::LifetimeRef get_lifetime(const Span& sp, const ::HIR::GenericRef& g) const override {
            ASSERT_BUG(sp, g.group() == 0, "");
            ASSERT_BUG(sp, g.idx() < impl_lfts.size(), "");
            if (!impl_lfts[g.idx()]) {
                DEBUG("WARNING: Assuming an empty lifetime");
                return HIR::LifetimeRef();
            }
            return *impl_lfts[g.idx()];
        }

        void reinit(const HIR::GenericParams& params) {
            this->impl_tys.clear();
            this->impl_vals.clear();
            this->impl_lfts.clear();
            this->impl_tys.resize(params.m_types.size());
            this->impl_vals.resize(params.m_values.size());
            this->impl_lfts.resize(params.m_lifetimes.size());
        }

        void fmt(::std::ostream& os) const {
            for (const auto& p : this->impl_tys) {
                if (p) {
                    os << *p;
                } else {
                    os << "?";
                }
                os << ",";
            }
            for (const auto& p : this->impl_vals) {
                if (p) {
                    os << *p;
                } else {
                    os << "?";
                }
                os << ",";
            }
            for (const auto& p : this->impl_lfts) {
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
            for (unsigned int i = 0; i < ::std::min(a.m_types.size(), b.m_types.size()); i++) {
                if (!H::types_overlap(a.m_types[i], b.m_types[i])) {
                    return false;
                }
            }
            return true;
        }

        static bool types_overlap_path(const ::HIR::Path& a, const ::HIR::Path& b) {
            if (a.m_data.tag() != b.m_data.tag()) {
                return false;
            }
            TU_MATCHA((a.m_data, b.m_data), (ape, bpe), (Generic, if (ape.m_path != bpe.m_path) return false; return H::types_overlap(ape.m_params, bpe.m_params);), (UfcsUnknown, ), (UfcsKnown, ), (UfcsInherent, ))
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
                    if (ae.m_trait.m_path.m_path != be.m_trait.m_path.m_path) {
                        return false;
                    }
                    if (!H::types_overlap(ae.m_trait.m_path.m_params, be.m_trait.m_path.m_params)) {
                        return false;
                    }
                    // Marker traits only overlap if the lists are the same (with overlap)
                    if (ae.m_markers.size() != be.m_markers.size()) {
                        return false;
                    }
                    for (size_t i = 0; i < ae.m_markers.size(); i++) {
                        if (ae.m_markers[i].m_path != be.m_markers[i].m_path) {
                            return false;
                        }
                        if (!H::types_overlap(ae.m_markers[i].m_params, be.m_markers[i].m_params)) {
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
                    if (ae.m_abi != be.m_abi) {
                        return false;
                    }
                    if (ae.m_arg_types.size() != be.m_arg_types.size()) {
                        return false;
                    }
                    for (unsigned int i = 0; i < ae.m_arg_types.size(); i++) {
                        if (!H::types_overlap(ae.m_arg_types[i], be.m_arg_types[i])) {
                            return false;
                        }
                    }
                    if (!H::types_overlap(ae.m_rettype, be.m_rettype)) {
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
    if (this->m_type == other.m_type && this->m_trait_args == other.m_trait_args) {
        return true;
    }

    // 1. Are the impl types of the same form (or is one generic)
    if (!H::types_overlap(this->m_type, other.m_type)) {
        return false;
    }
    if (!H::types_overlap(this->m_trait_args, other.m_trait_args)) {
        return false;
    }

    DEBUG("TODO: Handle potential overlap (when not exactly equal)");
    //return this->m_type == other.m_type && this->m_trait_args == other.m_trait_args;
    Span sp;

    // TODO: Use `type_ord_specific` but treat any case of mixed ordering as this returning `false`
    try {
        type_ord_specific(sp, this->m_type, other.m_type);
        typelist_ord_specific(sp, this->m_trait_args.m_types, other.m_trait_args.m_types);
    } catch (const TypeOrdSpecificMixedOrdering& /*e*/) {
        return false;
    }

    // TODO: Detect `impl<T> Foo<T> for Bar<T>` vs `impl<T> Foo<&T> for Bar<T>`
    // > Create values for impl params from the type, then check if the trait params are compatible
    // > Requires two lists, and telling which one to use by the end
    auto cb_ident = ResolvePlaceholdersNop();
    bool is_reversed = false;
    ImplTyMatcher matcher(crate.m_types);
    matcher.reinit(this->m_params);
    if (!this->m_type->match_test_generics(sp, other.m_type, cb_ident, matcher)) {
        DEBUG("- Type mismatch, try other ordering");
        is_reversed = true;
        matcher.reinit(other.m_params);
        if (!other.m_type->match_test_generics(sp, this->m_type, cb_ident, matcher)) {
            DEBUG("- Type mismatch in both orderings");
            return false;
        }
        if (other.m_trait_args.match_test_generics_fuzz(sp, this->m_trait_args, cb_ident, matcher) != ::HIR::Compare::Equal) {
            DEBUG("- Params mismatch");
            return false;
        }
        // Matched with second ordering
    } else if (this->m_trait_args.match_test_generics_fuzz(sp, other.m_trait_args, cb_ident, matcher) != ::HIR::Compare::Equal) {
        DEBUG("- Param mismatch, try other ordering");
        is_reversed = true;
        matcher.reinit(other.m_params);
        if (!other.m_type->match_test_generics(sp, this->m_type, cb_ident, matcher)) {
            DEBUG("- Type mismatch in alt ordering");
            return false;
        }
        if (other.m_trait_args.match_test_generics_fuzz(sp, this->m_trait_args, cb_ident, matcher) != ::HIR::Compare::Equal) {
            DEBUG("- Params mismatch in alt ordering");
            return false;
        }
        // Matched with second ordering
    } else {
        // Matched with first ordering
    }

    struct H2 {
        static const ::HIR::TypeData* monomorph(const Span& sp, const ::HIR::TypeData* in_ty, const Monomorphiser& ms, ::HIR::TypeRef& tmp) {
            if (!monomorphise_type_needed(in_ty)) {
                return in_ty;
            } else {
                tmp = ms.monomorph_type(sp, in_ty);
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

        static bool check_bounds(const ::HIR::Crate& crate, const ::HIR::TraitImpl& id, const Monomorphiser& ms, const ::HIR::TraitImpl& g_src) {
            TRACE_FUNCTION;
            static Span sp;
            for (const auto& tb : id.m_params.m_bounds) {
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
                        for (const auto& bound : g_src.m_params.m_bounds) {
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
                            DEBUG("No matching bound for " << ty << " : " << trait << " in source bounds - " << g_src.m_params.fmt_bounds());
                            return false;
                        }
                    } else if (TU_TEST1((*ty), Path, .binding.is_Opaque())) {
                        TODO(sp, "Check bound " << ty << " : " << trait << " in source bounds or trait bounds");
                    } else {
                        // Search the crate for an impl
                        auto cb_ident = ResolvePlaceholdersNop();
                        bool rv = crate.find_trait_impls(trait.m_path.m_path, ty, cb_ident, [&](const ::HIR::TraitImpl& ti) -> bool {
                            DEBUG("impl" << ti.m_params.fmt_args() << " " << trait.m_path.m_path << ti.m_trait_args << " for " << ti.m_type << ti.m_params.fmt_bounds());

                            ImplTyMatcher matcher(crate.m_types);
                            matcher.reinit(ti.m_params);
                            // 1. Triple-check the type matches (and get generics)
                            if (!ti.m_type->match_test_generics(sp, ty, cb_ident, matcher)) {
                                return false;
                            }
                            // 2. Check trait params
                            assert(trait.m_path.m_params.m_types.size() == ti.m_trait_args.m_types.size());
                            for (size_t i = 0; i < trait.m_path.m_params.m_types.size(); i++) {
                                if (!ti.m_trait_args.m_types[i]->match_test_generics(sp, trait.m_path.m_params.m_types[i], cb_ident, matcher)) {
                                    return false;
                                }
                            }
                            // 3. Check bounds on the impl
                            if (!H2::check_bounds(crate, ti, matcher, g_src)) {
                                return false;
                            }
                            // 4. Check ATY bounds on the trait path
                            for (const auto& atyb : trait.m_type_bounds) {
                                if (ti.m_types.count(atyb.first) == 0) {
                                    DEBUG("Associated type '" << atyb.first << "' not in trait impl, assuming good");
                                } else {
                                    const auto& aty = ti.m_types.at(atyb.first);
                                    if (!aty.data->match_test_generics(sp, atyb.second.type, cb_ident, matcher)) {
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
    if (is_reversed) {
        DEBUG("(reversed) impl params " << FMT_CB(os, matcher.fmt(os);));
        // Check bounds on `other` using these params
        // TODO: Take a callback that does the checks. Or somehow return a "maybe overlaps" result?
        return H2::check_bounds(crate, other, matcher, *this);
    } else {
        DEBUG("impl params " << FMT_CB(os, matcher.fmt(os);));
        // Check bounds on `*this`
        return H2::check_bounds(crate, *this, matcher, other);
    }
}

namespace {
    template <typename ImplType>
    bool find_impls_list(const typename ::HIR::Crate::ImplGroup<::std::unique_ptr<ImplType>>::list_t& impl_list, const ::HIR::TypeData* type, ::HIR::t_cb_resolve_type ty_res, ::std::function<bool(const ImplType&)> callback) {
        for (const auto& impl : impl_list) {
            if (impl->matches_type(type, ty_res)) {
                if (callback(*impl)) {
                    return true;
                }
            }
        }
        return false;
    }

    template <typename ImplType>
    bool find_impls_list(const typename ::HIR::Crate::ImplGroup<const ImplType*>::list_t& impl_list, const ::HIR::TypeData* type, ::HIR::t_cb_resolve_type ty_res, ::std::function<bool(const ImplType&)> callback) {
        for (const auto& impl : impl_list) {
            if (impl->matches_type(type, ty_res)) {
                if (callback(*impl)) {
                    return true;
                }
            }
        }
        return false;
    }
}

namespace {
    bool find_trait_impls_int(const ::HIR::Crate& crate, const ::HIR::SimplePath& trait, const ::HIR::TypeData* type, ::HIR::t_cb_resolve_type ty_res, ::std::function<bool(const ::HIR::TraitImpl&)> callback) {
        auto it = crate.m_trait_impls.find(trait);
        if (it != crate.m_trait_impls.end()) {
            // 1. Find named impls (associated with named types)
            if (const auto* impl_list = it->second.get_list_for_type(type)) {
                if (find_impls_list(*impl_list, type, ty_res, callback)) {
                    return true;
                }
            }
            // - If the type is an ivar, search all types
            if (type->is_Infer() && !type->as_Infer().is_lit()) {
                DEBUG("Search all lists");
                for (const auto& list : it->second.named) {
                    if (find_impls_list(list.second, type, ty_res, callback)) {
                        return true;
                    }
                }
            }

            // 2. Search fully generic list.
            if (find_impls_list(it->second.generic, type, ty_res, callback)) {
                return true;
            }
        }

        return false;
    }

}

bool ::HIR::Crate::find_trait_impls(const ::HIR::SimplePath& trait, const ::HIR::TypeData* type, t_cb_resolve_type ty_res, ::std::function<bool(const ::HIR::TraitImpl&)> callback) const {
    if (this->m_all_trait_impls.size() > 0) {
        auto it = this->m_all_trait_impls.find(trait);
        if (it != this->m_all_trait_impls.end()) {
            // 1. Find named impls (associated with named types)
            if (const auto* impl_list = it->second.get_list_for_type(type)) {
                if (find_impls_list(*impl_list, type, ty_res, callback)) {
                    return true;
                }
            }
            // - If the type is an ivar, search all types
            if (type->is_Infer() && !type->as_Infer().is_lit()) {
                DEBUG("Search all lists");
                for (const auto& list : it->second.named) {
                    if (find_impls_list(list.second, type, ty_res, callback)) {
                        return true;
                    }
                }
            }

            // 2. Search fully generic list.
            if (find_impls_list(it->second.generic, type, ty_res, callback)) {
                return true;
            }
        }

        return false;
    }

    // TODO: Determine the source crates for this type and trait (coherence) and only search those
    if (find_trait_impls_int(*this, trait, type, ty_res, callback)) {
        return true;
    }
    for (const auto& ec : this->m_ext_crates) {
        if (find_trait_impls_int(*ec.second.m_data, trait, type, ty_res, callback)) {
            return true;
        }
    }
    return false;
}

namespace {
    bool find_auto_trait_impls_int(const ::HIR::Crate& crate, const ::HIR::SimplePath& trait, const ::HIR::TypeData* type, ::HIR::t_cb_resolve_type ty_res, ::std::function<bool(const ::HIR::MarkerImpl&)> callback) {
        auto it = crate.m_marker_impls.find(trait);
        if (it != crate.m_marker_impls.end()) {
            // 1. Find named impls (associated with named types)
            if (const auto* impl_list = it->second.get_list_for_type(type)) {
                if (find_impls_list(*impl_list, type, ty_res, callback)) {
                    return true;
                }
            }

            // 2. Search fully generic list.
            if (find_impls_list(it->second.generic, type, ty_res, callback)) {
                return true;
            }
        }

        return false;
    }
}

bool ::HIR::Crate::find_auto_trait_impls(const ::HIR::SimplePath& trait, const ::HIR::TypeData* type, t_cb_resolve_type ty_res, ::std::function<bool(const ::HIR::MarkerImpl&)> callback) const {
    if (this->m_all_marker_impls.size() > 0) {
        auto it = this->m_all_marker_impls.find(trait);
        if (it != this->m_all_marker_impls.end()) {
            // 1. Find named impls (associated with named types)
            if (const auto* impl_list = it->second.get_list_for_type(type)) {
                if (find_impls_list(*impl_list, type, ty_res, callback)) {
                    return true;
                }
            }

            // 2. Search fully generic list.
            if (find_impls_list(it->second.generic, type, ty_res, callback)) {
                return true;
            }
        }

        return false;
    }

    if (find_auto_trait_impls_int(*this, trait, type, ty_res, callback)) {
        return true;
    }
    for (const auto& ec : this->m_ext_crates) {
        if (find_auto_trait_impls_int(*ec.second.m_data, trait, type, ty_res, callback)) {
            return true;
        }
    }
    return false;
}

namespace {
    bool find_type_impls_int(const ::HIR::Crate& crate, const ::HIR::TypeData* type, ::HIR::t_cb_resolve_type ty_res, ::std::function<bool(const ::HIR::TypeImpl&)> callback) {
        // 1. Find named impls (associated with named types)
        if (const auto* impl_list = crate.m_type_impls.get_list_for_type(type)) {
            if (find_impls_list(*impl_list, type, ty_res, callback)) {
                return true;
            }
        }

        // 2. Search fully generic list?
        if (find_impls_list(crate.m_type_impls.generic, type, ty_res, callback)) {
            return true;
        }

        return false;
    }
}

bool ::HIR::Crate::find_type_impls(const ::HIR::TypeData* type, t_cb_resolve_type ty_res, ::std::function<bool(const ::HIR::TypeImpl&)> callback) const {
    if (m_all_trait_impls.size() > 0) {
        // 1. Find named impls (associated with named types)
        if (const auto* impl_list = this->m_all_type_impls.get_list_for_type(type)) {
            if (find_impls_list(*impl_list, type, ty_res, callback)) {
                return true;
            }
        }

        // 2. Search fully generic list?
        if (find_impls_list(this->m_all_type_impls.generic, type, ty_res, callback)) {
            return true;
        }

        return false;
    }
    // TODO: Determine the source crate for this type (coherence) and only search that

    // > Current crate
    if (find_type_impls_int(*this, type, ty_res, callback)) {
        return true;
    }
    for (const auto& ec : this->m_ext_crates) {
        //DEBUG("- " << ec.first);
        if (find_type_impls_int(*ec.second.m_data, type, ty_res, callback)) {
            return true;
        }
    }
    return false;
}

const ::MIR::Function* HIR::Crate::get_or_gen_mir(const ::HIR::ItemPath& ip, const ::HIR::ExprPtr& ep, const ::HIR::Function::args_t& args, ::HIR::TypeRef& ret_ty) const {
    if (!ep) {
        // No HIR, so has to just have MIR - from a extern crate most likely
        ASSERT_BUG(Span(), ep.m_mir, "No HIR (!ep) and no MIR (!ep.m_mir) for " << ip);
        return &*ep.m_mir;
    } else {
        if (!ep.m_mir) {
            TRACE_FUNCTION_F(ip);
            ASSERT_BUG(Span(), ep.m_state, "No ExprState for " << ip);

            auto& ep_mut = const_cast<::HIR::ExprPtr&>(ep);

            ::HIR::GenericPath current_trait;
            if (ep.m_state->m_current_trait_impl) {
                current_trait.m_path = ep.m_state->m_current_trait_path;
                current_trait.m_params = ep.m_state->m_current_trait_impl->m_trait_args.clone();
                // Lazy processing can be requested from Resolve UFCS Outer,
                // before the whole-crate Self-expansion pass has run.  Give
                // this body and its signature the same owner substitution.
                ConvertHIRExpandAliasesSelfExpr(
                    *this,
                    ep.m_state->m_current_trait_impl->m_type,
                    const_cast<::HIR::Function::args_t&>(args),
                    ret_ty,
                    ep_mut
                    );
            }

            // TODO: Ensure that all referenced items have constants evaluated
            if (ep.m_state->stage < ::HIR::ExprState::Stage::ConstEval) {
                if (ep.m_state->stage == ::HIR::ExprState::Stage::ConstEvalRequest) {
                    ERROR(Span(), E0000, "Loop in constant evaluation");
                }
                ep.m_state->stage = ::HIR::ExprState::Stage::ConstEvalRequest;
                ConvertHIRResolveUFCSExpr(*this, ip, ep_mut);
                ConvertHIRConstantEvaluateExpr(*this, ip, ep_mut);
                ep.m_state->stage = ::HIR::ExprState::Stage::ConstEval;
            }

            // Ensure typechecked
            if (ep.m_state->stage < ::HIR::ExprState::Stage::Typecheck) {
                if (ep.m_state->stage == ::HIR::ExprState::Stage::TypecheckRequest) {
                    ERROR(Span(), E0000, "Loop in constant evaluation");
                }
                ep.m_state->stage = ::HIR::ExprState::Stage::TypecheckRequest;

                // TODO: Set debug/timing stage
                //Debug_SetStagePre("HIR Typecheck");
                // - Can store that on the Expr, OR get it from the item path
                typeck::ModuleState ms{const_cast<::HIR::Crate&>(*this)};
                //ms.prepare_from_path( ip );   // <- Ideally would use this, but it's a lot of code for one usage
                ms.m_impl_generics = ep.m_state->m_impl_generics;
                ms.m_item_generics = ep.m_state->m_item_generics;
                ms.m_current_trait = ep.m_state->m_current_trait_impl ? &current_trait : nullptr;
                ms.m_current_trait_impl = ep.m_state->m_current_trait_impl;
                ms.m_traits = ep.m_state->m_traits;
                ms.m_mod_paths.push_back(ep.m_state->m_mod_path);
                TypecheckCode(ms, const_cast<::HIR::Function::args_t&>(args), ret_ty, ep_mut);
                // NOTE: This is already set by the above function
                ASSERT_BUG(Span(), ep.m_state->stage == ::HIR::ExprState::Stage::Typecheck, "Typecheck_Code didn't set stage");
            }
            if (ep.m_state->stage < ::HIR::ExprState::Stage::PostTypecheck) {
                //Debug_SetStagePre("Expand HIR Annotate");
                HIRExpandAnnotateUsageExpr(*this, ip, ep_mut);
                //Debug_SetStagePre("Expand HIR Statics Mark");
                HIRExpandStaticBorrowConstantsMarkExpr(*this, ip, ep_mut);
            }
            if (ep.m_state->stage < ::HIR::ExprState::Stage::Lifetimes) {
                //Debug_SetStagePre("Expand HIR Lifetimes");
                HIRExpandLifetimeInferExpr(*this, ip, args, ret_ty, ep_mut);
                ep.m_state->stage = ::HIR::ExprState::Stage::Lifetimes;
            }
            if (ep.m_state->stage < ::HIR::ExprState::Stage::Sbc) {
                if (ep.m_state->stage == ::HIR::ExprState::Stage::SbcRequest) {
                    ERROR(Span(), E0000, "Loop in constant evaluation");
                }
                ep.m_state->stage = ::HIR::ExprState::Stage::SbcRequest;
                //Debug_SetStagePre("Expand HIR Closures");
                HIRExpandClosuresExpr(*this, ret_ty, ep_mut);
                //Debug_SetStagePre("Expand HIR Statics");
                HIRExpandStaticBorrowConstantsExpr(*this, ip, ep_mut);
            }
            if (ep.m_state->stage < ::HIR::ExprState::Stage::Expand) {
                if (ep.m_state->stage == ::HIR::ExprState::Stage::ExpandRequest) {
                    ERROR(Span(), E0000, "Loop in constant evaluation");
                }
                ep.m_state->stage = ::HIR::ExprState::Stage::ExpandRequest;
                //Debug_SetStagePre("Expand HIR Calls");
                HIRExpandUfcsEverythingExpr(*this, ep_mut, ep.m_state->m_current_trait_impl);
                //Debug_SetStagePre("Expand HIR Reborrows");
                HIRExpandReborrowsExpr(*this, ep_mut);
                //Debug_SetStagePre("Expand HIR ErasedType");
                //HIR_Expand_ErasedType(*this, ep_mut);    // - Maybe?
                //Typecheck_Expressions_Validate(*hir_crate);

                ep.m_state->stage = ::HIR::ExprState::Stage::Expand;
            }
            // Generate MIR
            if (ep.m_state->stage < ::HIR::ExprState::Stage::Mir) {
                if (ep.m_state->stage == ::HIR::ExprState::Stage::MirRequest) {
                    ERROR(Span(), E0000, "Loop in constant evaluation");
                }
                ep.m_state->stage = ::HIR::ExprState::Stage::MirRequest;
                //Debug_SetStage("Lower MIR");
                HIRGenerateMIRExpr(*this, ip, ep_mut, args, ret_ty);
                ep.m_state->stage = ::HIR::ExprState::Stage::Mir;
            }
            assert(ep.m_mir);
        }
        return &*ep.m_mir;
    }
}

::HIR::TypeRef HIR::Trait::get_vtable_type(const Span& sp, const ::HIR::Crate& crate, const ::HIR::TypeData::Data_TraitObject& te) const {
    assert(te.m_trait.m_trait_ptr == this);

    const auto& vtable_ty_spath = this->m_vtable_path;
    const auto& vtable_ref = crate.get_struct_by_path(sp, vtable_ty_spath);
    HIR::PathParams pp_hrls;
    if (te.m_trait.m_hrtbs) {
        pp_hrls = te.m_trait.m_hrtbs->make_empty_params(true);
    }
    // Copy the param set from the trait in the trait object
    ::HIR::PathParams vtable_params = MonomorphHrlsOnly(crate.m_types, pp_hrls).monomorph_path_params(sp, te.m_trait.m_path.m_params, false);
    vtable_params.m_types.resize(te.m_trait.m_path.m_params.m_types.size() + this->m_type_indexes.size());
    // - Include associated types on bound
    for (const auto& ty_b : te.m_trait.m_type_bounds) {
        if (this->m_type_indexes.count(ty_b.first) == 0) {
            WARNING(sp, W0000, "Trait object path " << te.m_trait << " references a type with no vtable type index");
            continue;
        }
        auto idx = this->m_type_indexes.at(ty_b.first);
        vtable_params.m_types.at(idx) = MonomorphHrlsOnly(crate.m_types, pp_hrls).monomorph_type(sp, ty_b.second.type);
    }
    return crate.m_types.path(::HIR::GenericPath(vtable_ty_spath, mv$(vtable_params)), &vtable_ref);
}

unsigned HIR::Trait::get_vtable_value_index(const HIR::GenericPath& trait_path, const RcString& name) const {
    auto its = this->m_value_indexes.equal_range(name);
    for (auto it = its.first; it != its.second; ++it) {
        DEBUG(trait_path << " :: " << name << " - " << it->second.second);
        if (it->second.second.m_path == trait_path.m_path) {
            // TODO: Match generics using match_test_generics comparing to the trait args
            assert(it->second.first > 0);
            return it->second.first;
        }
    }
    return 0;
}

unsigned HIR::Trait::get_vtable_parent_index(HIR::TypeInterner& types, const Span& sp, const HIR::PathParams& this_params, const HIR::GenericPath& trait_path) const {
    for (const auto& pt : this->m_all_parent_traits) {
        if (pt.m_path.m_path == trait_path.m_path) {
            auto p = MonomorphStatePtr(types, nullptr, &this_params, nullptr).monomorph_genericpath(sp, pt.m_path);
            if (p == trait_path) {
                return m_vtable_parent_traits_start + (&pt - this->m_all_parent_traits.data());
            }
        }
    }
    return 0;
}

::std::pair<const ::HIR::AssociatedType*, const ::HIR::PathParams*> HIR::Trait::get_aty_def(const RcString& name) const {
    auto it = m_types.find(name);
    if (it != m_types.end()) {
        return std::make_pair(&it->second, nullptr);
    }
    for (const auto& parent : m_all_parent_traits) {
        it = parent.m_trait_ptr->m_types.find(name);
        if (it != parent.m_trait_ptr->m_types.end()) {
            return std::make_pair(&it->second, &parent.m_path.m_params);
        }
    }
    return std::make_pair(nullptr, nullptr);
}

/// Helper for getting the struct associated with a pattern path
const ::HIR::Struct& HIR::pattern_get_struct(const Span& sp, const ::HIR::Path& path, const ::HIR::Pattern::PathBinding& binding, bool is_tuple) {
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
            if (is_tuple) {
                ASSERT_BUG(sp, enm.m_data.is_Data(), "PathTuple pattern with non-data enum - " << path);
            } else {
                ASSERT_BUG(sp, enm.m_data.is_Data(), "PathNamed pattern with non-data enum - " << path);
            }
            const auto& enm_d = enm.m_data.as_Data();
            ASSERT_BUG(sp, be.var_idx < enm_d.size(), "Variant index " << be.var_idx << " out of range - " << path);
            if (is_tuple) {
                ASSERT_BUG(sp, !enm_d[be.var_idx].is_struct, "PathTuple pattern with brace enum variant - " << path);
            } else {
                ASSERT_BUG(sp, enm_d[be.var_idx].is_struct, "PathNamed pattern with non-brace enum variant - " << path);
            }
            str_p = enm_d[be.var_idx].type->as_Path().binding.as_Struct();
        }
    }
    const auto& str = *str_p;

    if(is_tuple) {
        ASSERT_BUG(sp, str.m_data.is_Tuple(), "PathTuple pattern with non-tuple struct - " << str.m_data.tag_str());
    }
    else {
        ASSERT_BUG(sp, str.m_data.is_Named(), "Struct pattern on non-brace struct");
    }
    return str;
}

const ::HIR::t_tuple_fields& HIR::pattern_get_tuple(const Span& sp, const ::HIR::Path& path, const ::HIR::Pattern::PathBinding& binding) {
    return pattern_get_struct(sp, path, binding, true).m_data.as_Tuple();
}

const ::HIR::t_struct_fields& HIR::pattern_get_named(const Span& sp, const ::HIR::Path& path, const ::HIR::Pattern::PathBinding& binding) {
    if (binding.is_Union()) {
        return binding.as_Union()->m_variants;
    }
    return pattern_get_struct(sp, path, binding, false).m_data.as_Named();
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
EncodedLiteral EncodedLiteral::make_usize(uint64_t v) {
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
        size_t bit = (TargetGetCurSpec().m_arch.m_big_endian ? (size - 1 - i) * 8 : i * 8);
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
        size = m_size;
    }
    ASSERT_BUG(Span(), size <= m_size, "Over-large read (" << size << " > " << m_size << ")");
    U128 v(0);
    for (size_t i = 0; i < size; i++) {
        size_t bit = (TargetGetCurSpec().m_arch.m_big_endian ? (size - 1 - i) * 8 : i * 8);
        if (bit < 128) {
            v |= U128(m_base.bytes[m_ofs + i]) << bit;
        }
    }
    DEBUG("(" << size << ") = " << v);
    return v;
}

S128 EncodedLiteralSlice::read_sint(size_t size /*=0*/) const {
    if (size == 0) {
        size = m_size;
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
        size = m_size;
    }
    assert(size <= m_size);
    switch (size) {
        case 2: {
            F16 v;
            memcpy(&v, &m_base.bytes[m_ofs], 2);
            return FloatValue(static_cast<float>(v));
        }
        case 4: {
            float v;
            memcpy(&v, &m_base.bytes[m_ofs], 4);
            return v;
        }
        case 8: {
            double v;
            memcpy(&v, &m_base.bytes[m_ofs], 8);
            return v;
        }
        case 16: {
            F128 v;
            memcpy(&v, &m_base.bytes[m_ofs], 16);
            return v;
        }
        default:
            BUG(Span(), "Unexpected float size");
    }
}

const Reloc* EncodedLiteralSlice::get_reloc() const {
    for (const auto& r : m_base.relocations) {
        if (r.ofs == m_ofs) {
            return &r;
        }
    }
    return nullptr;
}

bool EncodedLiteralSlice::operator==(const EncodedLiteralSlice& x) const {
    if (m_size != x.m_size) {
        return false;
    }
    for (size_t i = 0; i < m_size; i++) {
        if (m_base.bytes[m_ofs + i] != x.m_base.bytes[x.m_ofs + i]) {
            return false;
        }
    }
    auto it1 = std::find_if(m_base.relocations.begin(), m_base.relocations.end(), [&](const Reloc& r) {
        return r.ofs >= m_ofs;
    });
    auto it2 = std::find_if(x.m_base.relocations.begin(), x.m_base.relocations.end(), [&](const Reloc& r) {
        return r.ofs >= x.m_ofs;
    });
    for (; it1 != m_base.relocations.end() && it2 != x.m_base.relocations.end(); ++it1, ++it2) {
        if (it1->ofs - m_ofs != it2->ofs - x.m_ofs) {
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
    auto min_size = std::min(m_size, x.m_size);
    for (size_t i = 0; i < min_size; i++) {
        if (auto cmp = ::ord(m_base.bytes[m_ofs + i], x.m_base.bytes[x.m_ofs + i])) {
            return cmp;
        }
    }
    if (auto cmp = ::ord(m_size, x.m_size)) {
        return cmp;
    }

    auto it1 = std::find_if(m_base.relocations.begin(), m_base.relocations.end(), [&](const Reloc& r) {
        return r.ofs >= m_ofs;
    });
    auto it2 = std::find_if(x.m_base.relocations.begin(), x.m_base.relocations.end(), [&](const Reloc& r) {
        return r.ofs >= x.m_ofs;
    });

    for (; it1 != m_base.relocations.end() && it2 != x.m_base.relocations.end(); ++it1, ++it2) {
        if (auto cmp = ::ord(it1->ofs - m_ofs, it2->ofs - x.m_ofs)) {
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
    auto it = std::find_if(x.m_base.relocations.begin(), x.m_base.relocations.end(), [&](const Reloc& r) {
        return r.ofs >= x.m_ofs;
    });
    for (size_t i = 0; i < x.m_size; i++) {
        const char* HEX = "0123456789ABCDEF";
        auto o = x.m_ofs + i;
        auto b = x.m_base.bytes[o];
        if (it != x.m_base.relocations.end() && it->ofs == o) {
            auto& r = *it;
            if (r.p) {
                os << "{&" << *r.p << "}";
            } else {
                os << "{\"" << FmtEscaped(r.bytes) << "\"}";
            }
            ++it;
        }
        os << HEX[b >> 4] << HEX[b & 0xF];
        if ((i + 1) % 8 == 0 && i + 1 < x.m_size) {
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
    : m_linkage(std::move(linkage))
    , m_is_mut(is_mut)
    , m_type(std::move(type))
    , m_value(std::move(value)) {
}
Constant::Constant() {
}
Constant::Constant(GenericParams params, TypeRef type, ExprPtr value)
    : m_params(::std::move(params))
    , m_type(::std::move(type))
    , m_value(::std::move(value)) {
}
Function::Function() {
}
Function::Function(Receiver receiver, GenericParams params, args_t args, TypeRef ret_ty, ExprPtr code)
    : m_receiver(receiver)
    , m_params(std::move(params))
    , m_args(std::move(args))
    , m_variadic(false)
    , m_return(std::move(ret_ty))
    , m_code(std::move(code)) {
}
Struct::FieldDefault::FieldDefault(size_t index, HIR::ExprPtr v)
    : index(index)
    , expr(std::move(v)) {
}
Struct::Struct(GenericParams params, Repr repr, Data data)
    : m_params(mv$(params))
    , m_repr(mv$(repr))
    , m_data(mv$(data)) {
}
Struct::Struct(GenericParams params, Repr repr, Data data, unsigned align, TraitMarkings tm, StructMarkings sm)
    : m_params(mv$(params))
    , m_repr(mv$(repr))
    , m_data(mv$(data))
    , m_forced_alignment(align)
    , m_markings(mv$(tm))
    , m_struct_markings(mv$(sm)) {
}
AssociatedType::AssociatedType(
    ::HIR::GenericParams generics,
    bool is_sized,
    LifetimeRef lifetime_bound,
    ::std::vector<::HIR::TraitPath> trait_bounds,
    ::HIR::TypeRef default_type
)
    : m_generics(::std::move(generics))
    , is_sized(is_sized)
    , m_lifetime_bound(lifetime_bound)
    , m_trait_bounds(::std::move(trait_bounds))
    , m_has_default(default_type && !default_type->is_Infer())
    , m_default(default_type) {
    assert(default_type);
}
Trait::Trait(GenericParams gps, LifetimeRef lifetime, ::std::vector<::HIR::TraitPath> parents)
    : m_params(mv$(gps))
    , m_lifetime(mv$(lifetime))
    , m_parent_traits(mv$(parents))
    , m_is_marker(false)
    , m_is_const(false)
    , m_is_coinductive(false)
    , m_is_fundamental(false)
    , m_vtable_parent_traits_start(0) {
}
Module::Module() {
}
Crate::Crate(stl::ObjPool* pool, TypeInterner& types)
    : m_pool(pool)
    , m_types(types)
    , m_intrinsic_offsetof(Function{Function::Receiver::Free, GenericParams{}, {}, types.primitive(CoreType::Usize), {}}) {
}
const ::HIR::Constant& Crate::get_constant_by_path(const Span& sp, const ::HIR::SimplePath& path) const {
    const auto& ti = this->get_valitem_by_path(sp, path);
    TU_IFLET(::HIR::ValueItem, ti, Constant, e, return e;)
    else {
        BUG(sp, "`const` path " << path << " didn't point to an enum");
    }
}
const ::MIR::Function* Crate::get_or_gen_mir(const ::HIR::ItemPath& ip, const ::HIR::Function& fcn) const {
    auto ty = fcn.m_return;
    return get_or_gen_mir(ip, fcn.m_code, fcn.m_args, ty);
}
const ::MIR::Function* Crate::get_or_gen_mir(const ::HIR::ItemPath& ip, const ::HIR::ExprPtr& ep, ::HIR::TypeRef& exp_ty) const {
    static ::HIR::Function::args_t s_args;
    return get_or_gen_mir(ip, ep, s_args, exp_ty);
}
}
