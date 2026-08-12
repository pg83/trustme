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
        : isMarker(false)
        , isUnsafe(false)
    {
    }

    Trait::Trait(GenericParams params, ::std::vector<Spanned<TypeTraitPath>> supertraits, ::std::vector<Spanned<LifetimeRef>> lifetimes)
        : mParams(mv$(params))
        , mSupertraits(mv$(supertraits))
        , mLifetimes(mv$(lifetimes))
        , isMarker(false)
        , isUnsafe(false)
    {
    }

    Trait::~Trait() = default;
    Trait::Trait(Trait&&) = default;
    Trait& Trait::operator=(Trait&&) = default;

    Impl::Impl(ImplDef def)
        : mDef(mv$(def))
    {
    }

    Impl::~Impl() = default;
    Impl::Impl(Impl&&) = default;
    Impl& Impl::operator=(Impl&&) = default;

    namespace {
        ::std::vector<Attribute> cloneMivec(const ::std::vector<Attribute>& v) {
            ::std::vector<Attribute> ri;
            ri.reserve(v.size());
            for (const auto& i : v) {
                ri.push_back(i.clone());
            }
            return ri;
        }
    }

    AttributeList AttributeList::clone() const {
        return AttributeList(cloneMivec(mItems));
    }

    void AttributeList::push_back(Attribute i) {
        mItems.push_back(::std::move(i));
    }

    const Attribute* AttributeList::get(const char* name) const {
        for (auto& i : mItems) {
            if (i.name() == name) {
                //i.mark_used();
                return &i;
            }
        }
        return nullptr;
    }

    ::std::ostream& operator<<(::std::ostream& os, const AttributeList& x) {
        for (const auto& i : x.mItems) {
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
        : mSpan(x.mSpan)
        , mName(x.mName)
        , mData(x.mData.clone())
        , isInert(x.isInert)
    {
    }

    Attribute Attribute::clone() const {
        return Attribute(*this);
    }

    void Attribute::fmt(std::ostream& os) const {
        os << mName;
        os << mData;
    }

    std::string Attribute::parse_equals_string(const AST::Crate& crate, const AST::Module& mod) const {
        TTStream lex(this->mSpan, ParseState(), this->data());
        lex.getTokenCheck(TOK_EQUAL);
        auto n = ExpandParseAndExpandExprVal(crate, mod, lex);

        std::string rv;
        if (auto* v = cast<::AST::ExprNodeString>(&*n)) {
            rv = v->mValue;
        } else {
            throw ParseError::Unexpected(lex, Token(InterpolatedFragment(InterpolatedFragment::EXPR, n.release())), TOK_STRING);
        }
        lex.getTokenCheck(TOK_EOF);
        return rv;
    }

    std::string Attribute::parse_paren_string() const {
        TTStream lex(this->mSpan, ParseState(), this->data());
        lex.getTokenCheck(TOK_PAREN_OPEN);
        auto rv = lex.getTokenCheck(TOK_STRING).str();
        lex.getTokenCheck(TOK_PAREN_CLOSE);
        return rv;
    }

    void Attribute::parse_paren_ident_list(std::function<void(const Span& sp, RcString ident)> item_cb) const {
        TTStream lex(this->mSpan, ParseState(), this->data());
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
        rv.mTy = ty;
        rv.visPath = std::make_shared<AST::AbsolutePath>(std::move(p));
        return rv;
    }

    Visibility Visibility::make_restricted(AST::AbsolutePath p, AST::Path in_path) {
        Visibility rv;
        rv.mTy = Ty::PubIn;
        rv.visPath = std::make_shared<AST::AbsolutePath>(std::move(p));
        rv.inPath = std::make_shared<AST::Path>(std::move(in_path));
        return rv;
    }

    void Visibility::fmt(::std::ostream& os) const {
        switch (mTy) {
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
                if (inPath) {
                    os << *inPath;
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
        if (visPath) {
            if (visPath->crate != from_mod.crate) {
                return false;
            }
            if (visPath->nodes.size() > from_mod.nodes.size()) {
                return false;
            }
            for (size_t i = 0; i < visPath->nodes.size(); i++) {
                if (visPath->nodes[i] != from_mod.nodes[i]) {
                    return false;
                }
            }
            return true;
        } else {
            return true;
        }
    }

    bool Visibility::contains(const ::AST::Visibility& x) const {
        if (visPath) {
            return x.is_visible(*visPath);
        } else {
            return true;
        }
    }

    void Visibility::inplace_union(const Visibility& x) {
        if (this->contains(x)) {
        } else if (x.contains(*this)) {
            visPath = x.visPath;
        } else {
            TODO(Span(), "Union with incompatible visbility");
        }
    }

    // ---

    StructItem StructItem::clone() const {
        return StructItem(mAttrs.clone(), vis, mName, mType.clone(), defaultValue.clone());
    }

    TupleItem TupleItem::clone() const {
        return TupleItem(mAttrs.clone(), vis, mType.clone());
    }

    TypeAlias TypeAlias::clone() const {
        return TypeAlias(mParams.clone(), mType.clone());
    }

    Static Static::clone() const {
        return Static(cls, mType.clone(), mValue.is_valid() ? AST::Expr(mValue.node().clone()) : AST::Expr());
    }

    Function::Function(Span sp, ::std::string abi, Flags flags, GenericParams params, TypeRef ret_type, Arglist args, bool is_variadic)
        : mSpan(sp)
        , mParams(mv$(params))
        , mRettype(mv$(ret_type))
        , mArgs(mv$(args))
        , isVariadic(is_variadic)
        , mAbi(mv$(abi))
        , flags(flags)
    {
    }

    Function Function::clone() const {
        decltype(mArgs) new_args;
        for (const auto& arg : mArgs) {
            new_args.push_back(AST::Function::Arg(arg.pat.clone(), arg.ty.clone(), arg.attrs.clone()));
        }

        auto rv = Function(mSpan, mAbi, flags, mParams.clone(), mRettype.clone(), mv$(new_args), isVariadic);
        if (mCode.is_valid()) {
            rv.mCode = AST::Expr(mCode.node().clone());
        }
        return rv;
    }

    void Trait::addType(Span sp, RcString name, AttributeList attrs, TypeRef type) {
        mItems.push_back(Named<Item>(sp, mv$(attrs), AST::Visibility::make_global(), mv$(name), Item::make_Type({TypeAlias(GenericParams(), mv$(type))})));
    }

    void Trait::addFunction(Span sp, RcString name, AttributeList attrs, Function fcn) {
        mItems.push_back(Named<Item>(sp, mv$(attrs), AST::Visibility::make_global(), mv$(name), Item::make_Function({mv$(fcn)})));
    }

    void Trait::addStatic(Span sp, RcString name, AttributeList attrs, Static v) {
        mItems.push_back(Named<Item>(sp, mv$(attrs), AST::Visibility::make_global(), mv$(name), Item::make_Static({mv$(v)})));
    }

    void Trait::set_is_marker() {
        isMarker = true;
    }

    bool Trait::is_marker() const {
        return isMarker;
    }

    bool Trait::has_named_item(const RcString& name, bool& out_is_fcn) const {
        for (const auto& i : mItems) {
            if (i.name == name) {
                out_is_fcn = i.data.is_Function();
                return true;
            }
        }
        return false;
    }

    Trait Trait::clone() const {
        auto rv = Trait(mParams.clone(), mSupertraits, mLifetimes);
        for (const auto& item : mItems) {
            rv.mItems.push_back(Named<Item>{item.span, item.attrs.clone(), item.vis, item.name, item.data.clone()});
        }
        return rv;
    }

    Enum Enum::clone() const {
        decltype(mVariants) new_variants;
        for (const auto& var : mVariants) {
            TU_MATCHA((var.mData), (e), (Unit, new_variants.push_back(EnumVariant(var.mAttrs.clone(), var.mName));), (Tuple, decltype(e.mItems) new_st; for (const auto& f : e.mItems) new_st.push_back(f.clone()); new_variants.push_back(EnumVariant(var.mAttrs.clone(), var.mName, mv$(new_st)));), (Struct, decltype(e.fields) new_fields; for (const auto& f : e.fields) new_fields.push_back(f.clone()); new_variants.push_back(EnumVariant(var.mAttrs.clone(), var.mName, mv$(new_fields)));))
            new_variants.back().discriminantValue = var.discriminantValue.clone();
        }
        auto rv = Enum(mParams.clone(), mv$(new_variants));
        rv.markings = markings;
        return rv;
    }

    Struct Struct::clone() const {
        TU_MATCHA((mData), (e), (Unit, return Struct(mParams.clone());), (Tuple, decltype(e.ents) new_fields; for (const auto& f : e.ents) new_fields.push_back(f.clone()); return Struct(mParams.clone(), mv$(new_fields));), (Struct, decltype(e.ents) new_fields; for (const auto& f : e.ents) new_fields.push_back(f.clone()); return Struct(mParams.clone(), mv$(new_fields));))
        throw "";
    }

    Union Union::clone() const {
        decltype(mVariants) new_vars;
        for (const auto& f : mVariants) {
            new_vars.push_back(f.clone());
        }
        return Union(mParams.clone(), mv$(new_vars));
    }

    ::std::ostream& operator<<(::std::ostream& os, const ImplDef& impl) {
        return os << "impl " << (impl.isConst ? "const " : "") << "<" << impl.mParams << "> " << impl.mTrait.ent << " for " << impl.mType << "";
    }

    void Impl::addFunction(Span sp, AttributeList attrs, AST::Visibility vis, bool is_specialisable, RcString name, Function fcn) {
        mItems.push_back(ImplItem{sp, mv$(attrs), mv$(vis), is_specialisable, mv$(name), box$(Item::make_Function(mv$(fcn)))});
    }

    void Impl::addType(Span sp, AttributeList attrs, AST::Visibility vis, bool is_specialisable, RcString name, GenericParams params, TypeRef type) {
        mItems.push_back(ImplItem{sp, mv$(attrs), mv$(vis), is_specialisable, mv$(name), box$(Item::make_Type(TypeAlias(mv$(params), mv$(type))))});
    }

    void Impl::addStatic(Span sp, AttributeList attrs, AST::Visibility vis, bool is_specialisable, RcString name, Static v) {
        mItems.push_back(ImplItem{sp, mv$(attrs), mv$(vis), is_specialisable, mv$(name), box$(Item::make_Static(mv$(v)))});
    }

    void Impl::addMacroInvocation(MacroInvocation item) {
        mItems.push_back(ImplItem{item.span(), {}, AST::Visibility::make_global(), false, "", box$(Item::make_MacroInv(mv$(item)))});
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
        return os << impl.mDef;
    }

    ::std::ostream& operator<<(::std::ostream& os, const UseItem::Ent& x) {
        return os << x.name << "=" << x.path;
    }

    MacroInvocation MacroInvocation::clone() const {
        return MacroInvocation(mSpan, AST::Path(macroPath), ident, input.clone());
    }

    UseItem UseItem::clone() const {
        decltype(this->entries) entries;
        for (const auto& e : this->entries) {
            entries.push_back({e.sp, e.path, e.name});
        }
        return UseItem{this->sp, mv$(entries)};
    }

    ExternBlock::ExternBlock(::std::string abi)
        : mAbi(mv$(abi))
    {
    }

    ExternBlock::~ExternBlock() = default;
    ExternBlock::ExternBlock(ExternBlock&&) = default;
    ExternBlock& ExternBlock::operator=(ExternBlock&&) = default;

    void ExternBlock::addItem(Named<Item> named_item) {
        ASSERT_BUG(named_item.span, named_item.data.is_Function() || named_item.data.is_Static() || named_item.data.is_Type() || named_item.data.is_MacroInv(), "Incorrect item type for ExternBlock - " << named_item.data.tag_str());
        mItems.push_back(mv$(named_item));
    }

    ExternBlock ExternBlock::clone() const {
        TODO(Span(), "Clone an extern block");
    }

    Module::Module() = default;

    Module::Module(::AST::AbsolutePath path)
        : myPath(mv$(path))
    {
    }

    Module::~Module() = default;
    Module::Module(Module&&) = default;
    Module& Module::operator=(Module&&) = default;

    ::std::shared_ptr<AST::Module> Module::addAnon() {
        auto rv = ::std::shared_ptr<AST::Module>(new Module(myPath + RcString::new_interned(FMT("#" << anonModules.size()))));
        DEBUG("New anon " << rv->myPath);
        rv->fileInfo = fileInfo;

        anonModules.push_back(rv);

        return rv;
    }

    void Module::addItem(Named<Item> named_item) {
        mItems.push_back(box$(named_item));
        const auto& i = mItems.back();
        if (i->name == "") {
        } else {
            DEBUG(myPath << "::" << i->name << " = " << i->data.tag_str() << ", attrs = " << i->attrs);
        }
    }

    void Module::addItem(Span sp, Visibility vis, RcString name, Item it, AttributeList attrs) {
        addItem(Named<Item>(mv$(sp), mv$(attrs), mv$(vis), mv$(name), mv$(it)));
    }

    void Module::addExtCrate(Span sp, AST::Visibility vis, RcString ext_name, RcString imp_name, AttributeList attrs) {
        this->addItem(mv$(sp), mv$(vis), imp_name, Item::make_Crate({mv$(ext_name)}), mv$(attrs));
    }

    void Module::addMacroInvocation(MacroInvocation item) {
        this->addItem(item.span(), AST::Visibility::make_global(), "", Item(mv$(item)), ::AST::AttributeList{});
    }

    void Module::addMacro(bool is_exported, RcString name, MacroRulesPtr macro) {
        assert(macro);
        assert(macro->rules.size() > 0);
        mMacros.push_back(
            Named<MacroRulesPtr>(
                Span(),
                {},
                /*is_pub=*/is_exported ? AST::Visibility::make_global() : AST::Visibility::make_restricted(AST::Visibility::Ty::Private, myPath),
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
        os << tp.mName;
        os << " = ";
        os << tp.defaultValue;
        //os << ")";
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const LifetimeParam& p) {
        os << "'" << p.mName;
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const ValueParam& p) {
        os << "const " << p.mName << ": " << p.mType;
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const HigherRankedBounds& x) {
        if (!x.empty()) {
            os << "for<";
            for (const auto& l : x.mLifetimes) {
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
        return os << "<" << tps.mParams << "> where {" << tps.bounds << "}";
    }

} // namespace AST

AST::AttributeList::AttributeList() = default;

AST::AttributeList::AttributeList(::std::vector<AST::Attribute> items)
    : mItems(mv$(items))
{
}

AST::AttributeList::~AttributeList() = default;
AST::AttributeList::AttributeList(AttributeList&&) = default;
AST::AttributeList& AST::AttributeList::operator=(AttributeList&&) = default;
AST::AttributeList::AttributeList(const AttributeList&) = default;

namespace AST {

//StructItem() {}

StructItem::StructItem(::AST::AttributeList attrs, AST::Visibility vis, RcString name, TypeRef ty, Expr default_value)
    : mAttrs(mv$(attrs))
    , vis(mv$(vis))
    , mName(mv$(name))
    , mType(mv$(ty))
    , defaultValue(mv$(default_value)) {
}
//TupleItem() {}

TupleItem::TupleItem(::AST::AttributeList attrs, AST::Visibility vis, TypeRef ty)
    : mAttrs(mv$(attrs))
    , vis(mv$(vis))
    , mType(mv$(ty)) {
}
//TypeAlias() {}
TypeAlias::TypeAlias(GenericParams params, TypeRef type)
    : mParams(std::move(params))
    , mType(std::move(type)) {
}
TypeAlias TypeAlias::new_associated_type(GenericParams params, GenericParams type_bounds, TypeRef default_type) {
    TypeAlias rv{std::move(params), std::move(default_type)};
    rv.selfBounds = std::move(type_bounds);
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
    : cls(s_class)
    , mType(std::move(type))
    , mValue(std::move(value)) {
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
    : mAttrs(mv$(attrs))
    , mName(mv$(name))
    , mData(EnumVariantData::make_Unit({})) {
}
EnumVariant::EnumVariant(AttributeList attrs, RcString name, ::std::vector<TupleItem> sub_types)
    : mAttrs(mv$(attrs))
    , mName(::std::move(name))
    , mData(EnumVariantData::make_Tuple({std::move(sub_types)})) {
}
EnumVariant::EnumVariant(AttributeList attrs, RcString name, ::std::vector<StructItem> fields)
    : mAttrs(mv$(attrs))
    , mName(::std::move(name))
    , mData(EnumVariantData::make_Struct({std::move(fields)})) {
}
Enum::Enum() {
}
Enum::Enum(GenericParams params, ::std::vector<EnumVariant> variants)
    : mParams(::std::move(params))
    , mVariants(::std::move(variants)) {
}
Struct::Markings::Markings() {
}
Struct::Struct() {
}
Struct::Struct(GenericParams params)
    : mParams(::std::move(params))
    , mData(StructData::make_Unit({})) {
}
Struct::Struct(GenericParams params, ::std::vector<StructItem> fields)
    : mParams(::std::move(params))
    , mData(StructData::make_Struct({mv$(fields)})) {
}
Struct::Struct(GenericParams params, ::std::vector<TupleItem> fields)
    : mParams(::std::move(params))
    , mData(StructData::make_Tuple({mv$(fields)})) {
}
Union::Union(GenericParams params, ::std::vector<StructItem> fields)
    : mParams(::std::move(params))
    , mVariants(::std::move(fields)) {
}
ImplDef::ImplDef(GenericParams params, Spanned<Path> trait_type, TypeRef impl_type)
    : isUnsafe(false)
    , isConst(false)
    , mParams(mv$(params))
    , mTrait(mv$(trait_type))
    , mType(mv$(impl_type)) {
}
}

namespace AST {

::std::ostream& operator<<(::std::ostream& os, const EnumVariant& x) {
    os << "EnumVariant(" << x.mName;
    TU_MATCH(EnumVariantData, (x.mData), (e), (Unit, ), (Tuple, os << "(" << e.mItems << ")";), (Struct, os << " { " << e.fields << " }";))
    if (x.discriminantValue) {
        os << " = " << x.discriminantValue;
    }
    return os << ")";
}
}
