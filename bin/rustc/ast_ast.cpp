#include "ast_ast.h"
#include "ast_crate.h"
#include "ast_types.h"
#include "ast_expr.h"
#include "common.h"
#include <iostream>
#include "parse_parseerror.h"
#include <algorithm>

#include "parse_ttstream.h"
#include "parse_common.h"
#include "parse_interpolated_fragment.h"
#include "synext.h" // Expand_ParseAndExpand_ExprVal

namespace AST {

    Trait::Trait()
        : m_is_marker(false)
        , m_is_unsafe(false)
    {
    }

    Trait::Trait(GenericParams params, ::std::vector<Spanned<Type_TraitPath>> supertraits, ::std::vector<Spanned<LifetimeRef>> lifetimes)
        : m_params(mv$(params))
        , m_supertraits(mv$(supertraits))
        , m_lifetimes(mv$(lifetimes))
        , m_is_marker(false)
        , m_is_unsafe(false)
    {
    }

    Trait::~Trait() = default;
    Trait::Trait(Trait&&) = default;
    Trait& Trait::operator=(Trait&&) = default;

    Impl::Impl(ImplDef def)
        : m_def(mv$(def))
    {
    }

    Impl::~Impl() = default;
    Impl::Impl(Impl&&) = default;
    Impl& Impl::operator=(Impl&&) = default;

    namespace {
        ::std::vector<Attribute> clone_mivec(const ::std::vector<Attribute>& v) {
            ::std::vector<Attribute> ri;
            ri.reserve(v.size());
            for (const auto& i : v) {
                ri.push_back(i.clone());
            }
            return ri;
        }
    }

    AttributeList AttributeList::clone() const {
        return AttributeList(clone_mivec(m_items));
    }

    void AttributeList::push_back(Attribute i) {
        m_items.push_back(::std::move(i));
    }

    const Attribute* AttributeList::get(const char* name) const {
        for (auto& i : m_items) {
            if (i.name() == name) {
                //i.mark_used();
                return &i;
            }
        }
        return nullptr;
    }

    ::std::ostream& operator<<(::std::ostream& os, const AttributeList& x) {
        for (const auto& i : x.m_items) {
            os << "#[" << i << "]";
        }
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const AttributeName& x) {
        if (x.elems.empty()) {
            os << "<empty>";
        } else {
            for (const auto& i : x.elems) {
                if (&i != &x.elems.front()) {
                    os << "::";
                }
                os << i;
            }
        }
        return os;
    }

    Attribute::Attribute(const Attribute& x)
        : m_span(x.m_span)
        , m_name(x.m_name)
        , m_data(x.m_data.clone())
        , m_is_inert(x.m_is_inert)
    {
    }

    Attribute Attribute::clone() const {
        return Attribute(*this);
    }

    void Attribute::fmt(std::ostream& os) const {
        os << m_name;
        os << m_data;
    }

    std::string Attribute::parse_equals_string(const AST::Crate& crate, const AST::Module& mod) const {
        TTStream lex(this->m_span, ParseState(), this->data());
        lex.getTokenCheck(TOK_EQUAL);
        auto n = Expand_ParseAndExpand_ExprVal(crate, mod, lex);

        std::string rv;
        if (auto* v = cast<::AST::ExprNodeString>(&*n)) {
            rv = v->m_value;
        } else {
            throw ParseError::Unexpected(lex, Token(InterpolatedFragment(InterpolatedFragment::EXPR, n.release())), TOK_STRING);
        }
        lex.getTokenCheck(TOK_EOF);
        return rv;
    }

    std::string Attribute::parse_paren_string() const {
        TTStream lex(this->m_span, ParseState(), this->data());
        lex.getTokenCheck(TOK_PAREN_OPEN);
        auto rv = lex.getTokenCheck(TOK_STRING).str();
        lex.getTokenCheck(TOK_PAREN_CLOSE);
        return rv;
    }

    void Attribute::parse_paren_ident_list(std::function<void(const Span& sp, RcString ident)> item_cb) const {
        TTStream lex(this->m_span, ParseState(), this->data());
        lex.getTokenCheck(TOK_PAREN_OPEN);
        while (lex.lookahead(0) != TOK_PAREN_CLOSE) {
            item_cb(lex.point_span(), lex.getTokenCheck(TOK_IDENT).ident().name);
            if (lex.lookahead(0) != TOK_COMMA) {
                break;
            }
            lex.getTokenCheck(TOK_COMMA);
        }
        lex.getTokenCheck(TOK_PAREN_CLOSE);
    }

    // ---
    Visibility Visibility::make_global() {
        return Visibility();
    }

    Visibility Visibility::make_restricted(Ty ty, AST::AbsolutePath p) {
        Visibility rv;
        rv.m_ty = ty;
        rv.m_vis_path = std::make_shared<AST::AbsolutePath>(std::move(p));
        return rv;
    }

    Visibility Visibility::make_restricted(AST::AbsolutePath p, AST::Path in_path) {
        Visibility rv;
        rv.m_ty = Ty::PubIn;
        rv.m_vis_path = std::make_shared<AST::AbsolutePath>(std::move(p));
        rv.m_in_path = std::make_shared<AST::Path>(std::move(in_path));
        return rv;
    }

    void Visibility::fmt(::std::ostream& os) const {
        switch (m_ty) {
            case Ty::Private:
                break;
            case Ty::Pub:
                os << "pub ";
                break;
            case Ty::Crate:
                os << "crate ";
                break;
            case Ty::PubCrate:
                os << "pub(crate) ";
                break;
            case Ty::PubSuper:
                os << "pub(super) ";
                break;
            case Ty::PubSelf:
                os << "pub(self) ";
                break;
            case Ty::PubIn:
                os << "pub(in ";
                if (m_in_path) {
                    os << *m_in_path;
                } else {
                    os << "???";
                }
                os << ")";
                break;
        }
    }

    std::ostream& operator<<(::std::ostream& os, const Visibility& x) {
        x.fmt(os);
        return os;
    }

    bool Visibility::is_visible(const ::AST::AbsolutePath& from_mod) const {
        if (m_vis_path) {
            if (m_vis_path->crate != from_mod.crate) {
                return false;
            }
            if (m_vis_path->nodes.size() > from_mod.nodes.size()) {
                return false;
            }
            for (size_t i = 0; i < m_vis_path->nodes.size(); i++) {
                if (m_vis_path->nodes[i] != from_mod.nodes[i]) {
                    return false;
                }
            }
            return true;
        } else {
            return true;
        }
    }

    bool Visibility::contains(const ::AST::Visibility& x) const {
        if (m_vis_path) {
            return x.is_visible(*m_vis_path);
        } else {
            return true;
        }
    }

    void Visibility::inplace_union(const Visibility& x) {
        if (this->contains(x)) {
        } else if (x.contains(*this)) {
            m_vis_path = x.m_vis_path;
        } else {
            TODO(Span(), "Union with incompatible visbility");
        }
    }

    // ---

    StructItem StructItem::clone() const {
        return StructItem(m_attrs.clone(), m_vis, m_name, m_type.clone(), m_default.clone());
    }

    TupleItem TupleItem::clone() const {
        return TupleItem(m_attrs.clone(), m_vis, m_type.clone());
    }

    TypeAlias TypeAlias::clone() const {
        return TypeAlias(m_params.clone(), m_type.clone());
    }

    Static Static::clone() const {
        return Static(m_class, m_type.clone(), m_value.is_valid() ? AST::Expr(m_value.node().clone()) : AST::Expr());
    }

    Function::Function(Span sp, ::std::string abi, Flags flags, GenericParams params, TypeRef ret_type, Arglist args, bool is_variadic)
        : m_span(sp)
        , m_params(mv$(params))
        , m_rettype(mv$(ret_type))
        , m_args(mv$(args))
        , m_is_variadic(is_variadic)
        , m_abi(mv$(abi))
        , m_flags(flags)
    {
    }

    Function Function::clone() const {
        decltype(m_args) new_args;
        for (const auto& arg : m_args) {
            new_args.push_back(AST::Function::Arg(arg.pat.clone(), arg.ty.clone(), arg.attrs.clone()));
        }

        auto rv = Function(m_span, m_abi, m_flags, m_params.clone(), m_rettype.clone(), mv$(new_args), m_is_variadic);
        if (m_code.is_valid()) {
            rv.m_code = AST::Expr(m_code.node().clone());
        }
        return rv;
    }

    void Trait::add_type(Span sp, RcString name, AttributeList attrs, TypeRef type) {
        m_items.push_back(Named<Item>(sp, mv$(attrs), AST::Visibility::make_global(), mv$(name), Item::make_Type({TypeAlias(GenericParams(), mv$(type))})));
    }

    void Trait::add_function(Span sp, RcString name, AttributeList attrs, Function fcn) {
        m_items.push_back(Named<Item>(sp, mv$(attrs), AST::Visibility::make_global(), mv$(name), Item::make_Function({mv$(fcn)})));
    }

    void Trait::add_static(Span sp, RcString name, AttributeList attrs, Static v) {
        m_items.push_back(Named<Item>(sp, mv$(attrs), AST::Visibility::make_global(), mv$(name), Item::make_Static({mv$(v)})));
    }

    void Trait::set_is_marker() {
        m_is_marker = true;
    }

    bool Trait::is_marker() const {
        return m_is_marker;
    }

    bool Trait::has_named_item(const RcString& name, bool& out_is_fcn) const {
        for (const auto& i : m_items) {
            if (i.name == name) {
                out_is_fcn = i.data.is_Function();
                return true;
            }
        }
        return false;
    }

    Trait Trait::clone() const {
        auto rv = Trait(m_params.clone(), m_supertraits, m_lifetimes);
        for (const auto& item : m_items) {
            rv.m_items.push_back(Named<Item>{item.span, item.attrs.clone(), item.vis, item.name, item.data.clone()});
        }
        return rv;
    }

    Enum Enum::clone() const {
        decltype(m_variants) new_variants;
        for (const auto& var : m_variants) {
            TU_MATCHA((var.m_data), (e), (Unit, new_variants.push_back(EnumVariant(var.m_attrs.clone(), var.m_name));), (Tuple, decltype(e.m_items) new_st; for (const auto& f : e.m_items) new_st.push_back(f.clone()); new_variants.push_back(EnumVariant(var.m_attrs.clone(), var.m_name, mv$(new_st)));), (Struct, decltype(e.m_fields) new_fields; for (const auto& f : e.m_fields) new_fields.push_back(f.clone()); new_variants.push_back(EnumVariant(var.m_attrs.clone(), var.m_name, mv$(new_fields)));))
            new_variants.back().m_discriminant_value = var.m_discriminant_value.clone();
        }
        auto rv = Enum(m_params.clone(), mv$(new_variants));
        rv.m_markings = m_markings;
        return rv;
    }

    Struct Struct::clone() const {
        TU_MATCHA((m_data), (e), (Unit, return Struct(m_params.clone());), (Tuple, decltype(e.ents) new_fields; for (const auto& f : e.ents) new_fields.push_back(f.clone()); return Struct(m_params.clone(), mv$(new_fields));), (Struct, decltype(e.ents) new_fields; for (const auto& f : e.ents) new_fields.push_back(f.clone()); return Struct(m_params.clone(), mv$(new_fields));))
        throw "";
    }

    Union Union::clone() const {
        decltype(m_variants) new_vars;
        for (const auto& f : m_variants) {
            new_vars.push_back(f.clone());
        }
        return Union(m_params.clone(), mv$(new_vars));
    }

    ::std::ostream& operator<<(::std::ostream& os, const ImplDef& impl) {
        return os << "impl " << (impl.m_is_const ? "const " : "") << "<" << impl.m_params << "> " << impl.m_trait.ent << " for " << impl.m_type << "";
    }

    void Impl::add_function(Span sp, AttributeList attrs, AST::Visibility vis, bool is_specialisable, RcString name, Function fcn) {
        m_items.push_back(ImplItem{sp, mv$(attrs), mv$(vis), is_specialisable, mv$(name), box$(Item::make_Function(mv$(fcn)))});
    }

    void Impl::add_type(Span sp, AttributeList attrs, AST::Visibility vis, bool is_specialisable, RcString name, GenericParams params, TypeRef type) {
        m_items.push_back(ImplItem{sp, mv$(attrs), mv$(vis), is_specialisable, mv$(name), box$(Item::make_Type(TypeAlias(mv$(params), mv$(type))))});
    }

    void Impl::add_static(Span sp, AttributeList attrs, AST::Visibility vis, bool is_specialisable, RcString name, Static v) {
        m_items.push_back(ImplItem{sp, mv$(attrs), mv$(vis), is_specialisable, mv$(name), box$(Item::make_Static(mv$(v)))});
    }

    void Impl::add_macro_invocation(MacroInvocation item) {
        m_items.push_back(ImplItem{item.span(), {}, AST::Visibility::make_global(), false, "", box$(Item::make_MacroInv(mv$(item)))});
    }

    bool Impl::has_named_item(const RcString& name) const {
        for (const auto& it : this->items()) {
            if (it.name == name) {
                return true;
            }
        }
        return false;
    }

    ::std::ostream& operator<<(::std::ostream& os, const Impl& impl) {
        return os << impl.m_def;
    }

    ::std::ostream& operator<<(::std::ostream& os, const UseItem::Ent& x) {
        return os << x.name << "=" << x.path;
    }

    MacroInvocation MacroInvocation::clone() const {
        return MacroInvocation(m_span, AST::Path(m_macro_path), m_ident, m_input.clone());
    }

    UseItem UseItem::clone() const {
        decltype(this->entries) entries;
        for (const auto& e : this->entries) {
            entries.push_back({e.sp, e.path, e.name});
        }
        return UseItem{this->sp, mv$(entries)};
    }

    ExternBlock::ExternBlock(::std::string abi)
        : m_abi(mv$(abi))
    {
    }

    ExternBlock::~ExternBlock() = default;
    ExternBlock::ExternBlock(ExternBlock&&) = default;
    ExternBlock& ExternBlock::operator=(ExternBlock&&) = default;

    void ExternBlock::add_item(Named<Item> named_item) {
        ASSERT_BUG(named_item.span, named_item.data.is_Function() || named_item.data.is_Static() || named_item.data.is_Type() || named_item.data.is_MacroInv(), "Incorrect item type for ExternBlock - " << named_item.data.tag_str());
        m_items.push_back(mv$(named_item));
    }

    ExternBlock ExternBlock::clone() const {
        TODO(Span(), "Clone an extern block");
    }

    Module::Module() = default;

    Module::Module(::AST::AbsolutePath path)
        : m_my_path(mv$(path))
    {
    }

    Module::~Module() = default;
    Module::Module(Module&&) = default;
    Module& Module::operator=(Module&&) = default;

    ::std::shared_ptr<AST::Module> Module::add_anon() {
        auto rv = ::std::shared_ptr<AST::Module>(new Module(m_my_path + RcString::new_interned(FMT("#" << m_anon_modules.size()))));
        DEBUG("New anon " << rv->m_my_path);
        rv->m_file_info = m_file_info;

        m_anon_modules.push_back(rv);

        return rv;
    }

    void Module::add_item(Named<Item> named_item) {
        m_items.push_back(box$(named_item));
        const auto& i = m_items.back();
        if (i->name == "") {
        } else {
            DEBUG(m_my_path << "::" << i->name << " = " << i->data.tag_str() << ", attrs = " << i->attrs);
        }
    }

    void Module::add_item(Span sp, Visibility vis, RcString name, Item it, AttributeList attrs) {
        add_item(Named<Item>(mv$(sp), mv$(attrs), mv$(vis), mv$(name), mv$(it)));
    }

    void Module::add_ext_crate(Span sp, AST::Visibility vis, RcString ext_name, RcString imp_name, AttributeList attrs) {
        this->add_item(mv$(sp), mv$(vis), imp_name, Item::make_Crate({mv$(ext_name)}), mv$(attrs));
    }

    void Module::add_macro_invocation(MacroInvocation item) {
        this->add_item(item.span(), AST::Visibility::make_global(), "", Item(mv$(item)), ::AST::AttributeList{});
    }

    void Module::add_macro(bool is_exported, RcString name, MacroRulesPtr macro) {
        assert(macro);
        assert(macro->m_rules.size() > 0);
        m_macros.push_back(
            Named<MacroRulesPtr>(
                Span(),
                {},
                /*is_pub=*/is_exported ? AST::Visibility::make_global() : AST::Visibility::make_restricted(AST::Visibility::Ty::Private, m_my_path),
                mv$(name),
                mv$(macro)
            )
        );
    }

    Item Item::clone() const {
        TU_MATCHA(
            (*this),
            (e),
            (None, return Item(e);),
            (MacroInv, TODO(Span(), "Clone on Item::MacroInv");),
            (Macro, TODO(Span(), "Clone on Item::Macro");),
            (Use, return Item(e.clone());),
            (ExternBlock, TODO(Span(), "Clone on Item::" << this->tag_str());),
            (GlobalAsm, TODO(Span(), "Clone on Item::" << this->tag_str());),
            (Impl, TODO(Span(), "Clone on Item::" << this->tag_str());),
            (NegImpl, TODO(Span(), "Clone on Item::" << this->tag_str());),
            (Module, TODO(Span(), "Clone on Item::" << this->tag_str());),
            (Crate, return Item(e);),
            (Type, return AST::Item(e.clone());),
            (Struct, return AST::Item(e.clone());),
            (Enum, return AST::Item(e.clone());),
            (Union, return AST::Item(e.clone());),
            (Trait, return AST::Item(e.clone());),
            (TraitAlias, return AST::Item(e.clone());),

            (Function, return AST::Item(e.clone());),
            (Static, return AST::Item(e.clone());)
        )
        throw "";
    }

    ::std::ostream& operator<<(::std::ostream& os, const TypeParam& tp) {
        //os << "TypeParam(";
        os << tp.m_name;
        os << " = ";
        os << tp.m_default;
        //os << ")";
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const LifetimeParam& p) {
        os << "'" << p.m_name;
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const ValueParam& p) {
        os << "const " << p.m_name << ": " << p.m_type;
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const HigherRankedBounds& x) {
        if (!x.empty()) {
            os << "for<";
            for (const auto& l : x.m_lifetimes) {
                os << l << ",";
            }
            os << "> ";
        }
        return os;
    }

    GenericParam GenericParam::clone() const {
    TU_MATCH_HDRA( (*this), {)
    TU_ARMA(None, e)
        return e;
            TU_ARMA(Lifetime, e)
            return LifetimeParam(e);
            TU_ARMA(Type, e)
            return TypeParam(e);
            TU_ARMA(Value, e)
            return ValueParam(e);
    }
    throw "";
    }

    std::ostream& operator<<(std::ostream& os, const GenericParam& x) {
    TU_MATCH_HDRA( (x), {)
    TU_ARMA(None, e)
        os << "/*-*/";
            TU_ARMA(Lifetime, e)
            os << e;
            TU_ARMA(Type, e)
            os << e;
            TU_ARMA(Value, e)
            os << e;
    }
    return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const GenericBound& x) {
    TU_MATCH_HDRA( (x), {)
    TU_ARMA(None, ent) {
                os << "/*-*/";
            }
            TU_ARMA(Lifetime, ent) {
                os << ent.test << ": " << ent.bound;
            }
            TU_ARMA(TypeLifetime, ent) {
                os << ent.type << ": " << ent.bound;
            }
            TU_ARMA(IsTrait, ent) {
                os << ent.outer_hrbs << ent.type << ": ";
                if (ent.constness == BoundConstness::Always) {
                    os << "const ";
                } else if (ent.constness == BoundConstness::Maybe) {
                    os << "[const] ";
                }
                os << ent.inner_hrbs << ent.trait;
            }
            TU_ARMA(MaybeTrait, ent) {
                os << ent.type << ": ?" << ent.trait;
            }
            TU_ARMA(NotTrait, ent) {
                os << ent.type << ": !" << ent.trait;
            }
            TU_ARMA(Equality, ent) {
                os << ent.type << " = " << ent.replacement;
            }
    }
    return os;
    }

    //int GenericParams::find_name(const char* name) const
    //{
    //    for( unsigned int i = 0; i < m_type_params.size(); i ++ )
    //    {
    //        if( m_type_params[i].name() == name )
    //            return i;
    //    }
    //    DEBUG("Type param '" << name << "' not in list");
    //    return -1;
    //}

    ::std::ostream& operator<<(::std::ostream& os, const GenericParams& tps) {
        return os << "<" << tps.m_params << "> where {" << tps.m_bounds << "}";
    }

} // namespace AST

AST::AttributeList::AttributeList() = default;

AST::AttributeList::AttributeList(::std::vector<AST::Attribute> items)
    : m_items(mv$(items))
{
}

AST::AttributeList::~AttributeList() = default;
AST::AttributeList::AttributeList(AttributeList&&) = default;
AST::AttributeList& AST::AttributeList::operator=(AttributeList&&) = default;
AST::AttributeList::AttributeList(const AttributeList&) = default;

namespace AST {

//StructItem() {}

StructItem::StructItem(::AST::AttributeList attrs, AST::Visibility vis, RcString name, TypeRef ty, Expr default_value)
    : m_attrs(mv$(attrs))
    , m_vis(mv$(vis))
    , m_name(mv$(name))
    , m_type(mv$(ty))
    , m_default(mv$(default_value)) {
}
//TupleItem() {}

TupleItem::TupleItem(::AST::AttributeList attrs, AST::Visibility vis, TypeRef ty)
    : m_attrs(mv$(attrs))
    , m_vis(mv$(vis))
    , m_type(mv$(ty)) {
}
//TypeAlias() {}
TypeAlias::TypeAlias(GenericParams params, TypeRef type)
    : m_params(std::move(params))
    , m_type(std::move(type)) {
}
TypeAlias TypeAlias::new_associated_type(GenericParams params, GenericParams type_bounds, TypeRef default_type) {
    TypeAlias rv{std::move(params), std::move(default_type)};
    rv.m_self_bounds = std::move(type_bounds);
    return rv;
}
TraitAlias TraitAlias::clone() const {
    TraitAlias rv;
    for (const auto& p : this->traits) {
        rv.traits.push_back(p);
    }
    return rv;
}
Static::Static(Class s_class, TypeRef type, Expr value)
    : m_class(s_class)
    , m_type(std::move(type))
    , m_value(std::move(value)) {
}
Function::Arg::Arg(::AST::Pattern pat, TypeRef ty, ::AST::AttributeList attrs)
    : attrs(mv$(attrs))
    , pat(mv$(pat))
    , ty(mv$(ty)) {
}
Function::Flags::Flags()
    : is_const(false)
    , is_unsafe(false)
    , is_async(false) {
}
Function::Flags Function::Flags::set_unsafe() const {
    auto rv = *this;
    rv.is_unsafe = true;
    return rv;
}
Function::Flags Function::Flags::set_const() const {
    auto rv = *this;
    rv.is_const = true;
    return rv;
}
Function::Flags Function::Flags::set_async() const {
    auto rv = *this;
    rv.is_async = true;
    return rv;
}
// Helper for derive, defines an ABI_RUST function with no generics
Function::Function(Span sp, TypeRef ret_type, Arglist args)
    : Function(sp, ABI_RUST, Flags(), GenericParams(), std::move(ret_type), std::move(args), false) {
}
EnumVariant::EnumVariant() {
}
EnumVariant::EnumVariant(AttributeList attrs, RcString name)
    : m_attrs(mv$(attrs))
    , m_name(mv$(name))
    , m_data(EnumVariantData::make_Unit({})) {
}
EnumVariant::EnumVariant(AttributeList attrs, RcString name, ::std::vector<TupleItem> sub_types)
    : m_attrs(mv$(attrs))
    , m_name(::std::move(name))
    , m_data(EnumVariantData::make_Tuple({std::move(sub_types)})) {
}
EnumVariant::EnumVariant(AttributeList attrs, RcString name, ::std::vector<StructItem> fields)
    : m_attrs(mv$(attrs))
    , m_name(::std::move(name))
    , m_data(EnumVariantData::make_Struct({std::move(fields)})) {
}
Enum::Enum() {
}
Enum::Enum(GenericParams params, ::std::vector<EnumVariant> variants)
    : m_params(::std::move(params))
    , m_variants(::std::move(variants)) {
}
Struct::Markings::Markings() {
}
Struct::Struct() {
}
Struct::Struct(GenericParams params)
    : m_params(::std::move(params))
    , m_data(StructData::make_Unit({})) {
}
Struct::Struct(GenericParams params, ::std::vector<StructItem> fields)
    : m_params(::std::move(params))
    , m_data(StructData::make_Struct({mv$(fields)})) {
}
Struct::Struct(GenericParams params, ::std::vector<TupleItem> fields)
    : m_params(::std::move(params))
    , m_data(StructData::make_Tuple({mv$(fields)})) {
}
Union::Union(GenericParams params, ::std::vector<StructItem> fields)
    : m_params(::std::move(params))
    , m_variants(::std::move(fields)) {
}
ImplDef::ImplDef(GenericParams params, Spanned<Path> trait_type, TypeRef impl_type)
    : m_is_unsafe(false)
    , m_is_const(false)
    , m_params(mv$(params))
    , m_trait(mv$(trait_type))
    , m_type(mv$(impl_type)) {
}
}

namespace AST {

::std::ostream& operator<<(::std::ostream& os, const EnumVariant& x) {
    os << "EnumVariant(" << x.m_name;
    TU_MATCH(EnumVariantData, (x.m_data), (e), (Unit, ), (Tuple, os << "(" << e.m_items << ")";), (Struct, os << " { " << e.m_fields << " }";))
    if (x.m_discriminant_value) {
        os << " = " << x.m_discriminant_value;
    }
    return os << ")";
}
}
