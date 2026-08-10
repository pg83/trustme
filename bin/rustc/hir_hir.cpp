/*
 * MRustC - Rust Compiler
 * - By John Hodge (Mutabah/thePowersGang)
 *
 * hir/hir.cpp
 * - Processed module tree (High-level Intermediate Representation)
 *
 * HIR type helper code
 */
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

    ::std::ostream& operator<<(::std::ostream& os, const ConstGeneric_Unevaluated& x) {
        x.fmt(os);
        return os;
    }

    ConstGeneric_Unevaluated::ConstGeneric_Unevaluated(HIR::ExprPtr ep)
        : expr(std::make_shared<HIR::ExprPtr>(std::move(ep)))
    {
    }

    ConstGeneric_Unevaluated ConstGeneric_Unevaluated::clone() const {
        ConstGeneric_Unevaluated rv;
        rv.params_impl = params_impl.clone();
        rv.params_item = params_item.clone();
        rv.expr = expr;
        return rv;
    }

    ConstGeneric_Unevaluated ConstGeneric_Unevaluated::monomorph(const Span& sp, const Monomorphiser& ms, bool allow_infer /*=true*/) const {
        ConstGeneric_Unevaluated rv;
        rv.params_impl = ms.monomorph_path_params(sp, params_impl, allow_infer);
        rv.params_item = ms.monomorph_path_params(sp, params_item, allow_infer);
        rv.expr = this->expr;
        return rv;
    }

    namespace {
        const ::HIR::ConstGeneric* get_unevaluated_param(const ::HIR::ConstGeneric_Unevaluated& value, unsigned int binding) {
            const ::HIR::PathParams* params = nullptr;
            switch (binding >> 8) {
                case ::HIR::GENERIC_Impl:
                    params = &value.params_impl;
                    break;
                case ::HIR::GENERIC_Item:
                    params = &value.params_item;
                    break;
                default:
                    return nullptr;
            }
            const unsigned int index = binding & 0xFF;
            return index < params->m_values.size() ? &params->m_values[index] : nullptr;
        }

        bool const_expr_literals_equal(const ::HIR::ExprNode_Literal& left, const ::HIR::ExprNode_Literal& right) {
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

        bool const_expr_nodes_equal(const ::HIR::ConstGeneric_Unevaluated& left_value, const ::HIR::ExprNode& left, const ::HIR::ConstGeneric_Unevaluated& right_value, const ::HIR::ExprNode& right) {
            if (const auto* l = dynamic_cast<const ::HIR::ExprNode_ConstParam*>(&left)) {
                const auto* r = dynamic_cast<const ::HIR::ExprNode_ConstParam*>(&right);
                if (!r) {
                    return false;
                }
                const auto* l_param = get_unevaluated_param(left_value, l->m_binding);
                const auto* r_param = get_unevaluated_param(right_value, r->m_binding);
                return l_param && r_param ? *l_param == *r_param : l->m_binding == r->m_binding;
            }
            if (const auto* l = dynamic_cast<const ::HIR::ExprNode_Literal*>(&left)) {
                const auto* r = dynamic_cast<const ::HIR::ExprNode_Literal*>(&right);
                return r && const_expr_literals_equal(*l, *r);
            }
            if (const auto* l = dynamic_cast<const ::HIR::ExprNode_BinOp*>(&left)) {
                const auto* r = dynamic_cast<const ::HIR::ExprNode_BinOp*>(&right);
                return r && l->m_op == r->m_op && const_expr_nodes_equal(left_value, *l->m_left, right_value, *r->m_left) && const_expr_nodes_equal(left_value, *l->m_right, right_value, *r->m_right);
            }
            if (const auto* l = dynamic_cast<const ::HIR::ExprNode_UniOp*>(&left)) {
                const auto* r = dynamic_cast<const ::HIR::ExprNode_UniOp*>(&right);
                return r && l->m_op == r->m_op && const_expr_nodes_equal(left_value, *l->m_value, right_value, *r->m_value);
            }
            if (const auto* l = dynamic_cast<const ::HIR::ExprNode_Cast*>(&left)) {
                const auto* r = dynamic_cast<const ::HIR::ExprNode_Cast*>(&right);
                return r && l->m_dst_type == r->m_dst_type && const_expr_nodes_equal(left_value, *l->m_value, right_value, *r->m_value);
            }
            if (const auto* l = dynamic_cast<const ::HIR::ExprNode_ConstBlock*>(&left)) {
                const auto* r = dynamic_cast<const ::HIR::ExprNode_ConstBlock*>(&right);
                return r && const_expr_nodes_equal(left_value, *l->m_inner, right_value, *r->m_inner);
            }
            if (const auto* l = dynamic_cast<const ::HIR::ExprNode_Block*>(&left)) {
                const auto* r = dynamic_cast<const ::HIR::ExprNode_Block*>(&right);
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

    bool ConstGeneric_Unevaluated::equivalent(const ConstGeneric_Unevaluated& x) const {
        return const_expr_nodes_equal(*this, **this->expr, x, **x.expr);
    }

    Ordering ConstGeneric_Unevaluated::ord(const ConstGeneric_Unevaluated& x) const {
        if (this->expr.get() != x.expr.get()) {
            // If only one has populated MIR, they can't be equal (sort populated MIR after)
            if (!this->expr->m_mir != !x.expr->m_mir) {
                return (this->expr->m_mir ? OrdGreater : OrdLess);
            }

            // HACK: If the inner is a const param on both, sort based on that.
            // - Very similar to the ordering of TypeRef::Generic
            const auto* tn = dynamic_cast<const HIR::ExprNode_ConstParam*>(&**this->expr);
            const auto* xn = dynamic_cast<const HIR::ExprNode_ConstParam*>(&**x.expr);
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

    void ConstGeneric_Unevaluated::fmt(::std::ostream& os) const {
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

            HIR_DumpExpr(inner_os, *expr);
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
        TU_ARMA(Unevaluated, e) return ::std::make_unique<ConstGeneric_Unevaluated>(e->clone());
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
    ::HIR::TypeData_FunctionPointer ft;
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
    ::HIR::TypeData_FunctionPointer ft;
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
