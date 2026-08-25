#include "ast_ast.h"

#include "common.h"
#include "synext.h" // Expand_ParseAndExpand_ExprVal
#include "ast_expr.h"
#include "ast_crate.h"
#include "ast_types.h"
#include "parse_common.h"
#include "parse_ttstream.h"
#include "parse_parseerror.h"
#include "parse_interpolated_fragment.h"

#include <iostream>
#include <algorithm>

ASTTrait::ASTTrait()
    : isMarker_(false)
    , isUnsafe_(false)
{
}

ASTTrait::ASTTrait(ASTGenericParams params, ::std::vector<Spanned<TypeTraitPath>> supertraits, ::std::vector<Spanned<ASTLifetimeRef>> lifetimes)
    : params_(mv$(params))
    , supertraits_(mv$(supertraits))
    , lifetimes_(mv$(lifetimes))
    , isMarker_(false)
    , isUnsafe_(false)
{
}

ASTTrait::~ASTTrait() = default;
ASTTrait::ASTTrait(ASTTrait&&) = default;
ASTTrait& ASTTrait::operator=(ASTTrait&&) = default;

ASTImpl::ASTImpl(ASTImplDef def)
    : def_(mv$(def))
{
}

ASTImpl::~ASTImpl() = default;
ASTImpl::ASTImpl(ASTImpl&&) = default;
ASTImpl& ASTImpl::operator=(ASTImpl&&) = default;

namespace {
    ::std::vector<ASTAttribute> cloneMivec(const ::std::vector<ASTAttribute>& v) {
        ::std::vector<ASTAttribute> ri;
        ri.reserve(v.size());
        for (const auto& i : v) {
            ri.push_back(i.clone());
        }
        return ri;
    }
}

ASTAttributeList ASTAttributeList::clone() const {
    return ASTAttributeList(cloneMivec(items));
}

void ASTAttributeList::push_back(ASTAttribute i) {
    items.push_back(::std::move(i));
}

const ASTAttribute* ASTAttributeList::get(const char* name) const {
    for (auto& i : items) {
        if (i.name() == name) {
            return &i;
        }
    }
    return nullptr;
}

::std::ostream& operator<<(::std::ostream& os, const ASTAttributeList& x) {
    for (const auto& i : x.items) {
        os << "#[" << i << "]";
    }
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const ASTAttributeName& x) {
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

ASTAttribute::ASTAttribute(const ASTAttribute& x)
    : span_(x.span_)
    , name_(x.name_)
    , data_(x.data_.clone())
    , isInert_(x.isInert_)
{
}

ASTAttribute ASTAttribute::clone() const {
    return ASTAttribute(*this);
}

void ASTAttribute::fmt(std::ostream& os) const {
    os << name_;
    os << data_;
}

std::string ASTAttribute::parseEqualsString(const WireBoard& wb, const ASTCrate& crate, const ASTModule& mod) const {
    TTStream lex(this->span_, ParseState(), this->data());
    lex.parseState().wb = &wb;
    lex.getTokenCheck(TOK_EQUAL);
    auto n = ExpandParseAndExpandExprVal(crate, mod, lex);

    std::string rv;
    if (auto* v = cast<ASTExprNodeString>(&*n)) {
        rv = v->value;
    } else {
        parseErrorUnexpected(lex, Token(InterpolatedFragment(InterpolatedFragment::EXPR, n.release())), TOK_STRING);
    }
    lex.getTokenCheck(TOK_EOF);
    return rv;
}

std::string ASTAttribute::parseParenString() const {
    TTStream lex(this->span_, ParseState(), this->data());
    lex.getTokenCheck(TOK_PAREN_OPEN);
    auto rv = lex.getTokenCheck(TOK_STRING).str();
    lex.getTokenCheck(TOK_PAREN_CLOSE);
    return rv;
}

void ASTAttribute::parseParenIdentListCb(ASTAttributeIdentCallback& itemCb) const {
    TTStream lex(this->span_, ParseState(), this->data());
    lex.getTokenCheck(TOK_PAREN_OPEN);
    while (lex.lookahead(0) != TOK_PAREN_CLOSE) {
        itemCb.visit(lex.pointSpan(), lex.getTokenCheck(TOK_IDENT).ident().name);
        if (lex.lookahead(0) != TOK_COMMA) {
            break;
        }
        lex.getTokenCheck(TOK_COMMA);
    }
    lex.getTokenCheck(TOK_PAREN_CLOSE);
}

// ---
ASTVisibility ASTVisibility::makeGlobal() {
    return ASTVisibility();
}

ASTVisibility ASTVisibility::makeRestricted(Ty ty, ASTAbsolutePath p) {
    ASTVisibility rv;
    rv.ty_ = ty;
    rv.visPath_ = std::make_shared<ASTAbsolutePath>(std::move(p));
    return rv;
}

ASTVisibility ASTVisibility::makeRestricted(ASTAbsolutePath p, ASTPath inPath) {
    ASTVisibility rv;
    rv.ty_ = Ty::PubIn;
    rv.visPath_ = std::make_shared<ASTAbsolutePath>(std::move(p));
    rv.inPath_ = std::make_shared<ASTPath>(std::move(inPath));
    return rv;
}

void ASTVisibility::fmt(::std::ostream& os) const {
    switch (ty_) {
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
            if (inPath_) {
                os << *inPath_;
            } else {
                os << "???";
            }
            os << ")";
            break;
    }
}

std::ostream& operator<<(::std::ostream& os, const ASTVisibility& x) {
    x.fmt(os);
    return os;
}

bool ASTVisibility::isVisible(const ASTAbsolutePath& fromMod) const {
    if (visPath_) {
        if (visPath_->crate != fromMod.crate) {
            return false;
        }
        if (visPath_->nodes.size() > fromMod.nodes.size()) {
            return false;
        }
        for (size_t i = 0; i < visPath_->nodes.size(); i++) {
            if (visPath_->nodes[i] != fromMod.nodes[i]) {
                return false;
            }
        }
        return true;
    } else {
        return true;
    }
}

bool ASTVisibility::contains(const ASTVisibility& x) const {
    if (visPath_) {
        return x.isVisible(*visPath_);
    } else {
        return true;
    }
}

void ASTVisibility::inplaceUnion(const ASTVisibility& x) {
    if (this->contains(x)) {
    } else if (x.contains(*this)) {
        visPath_ = x.visPath_;
    } else {
        TODO(Span(), "Union with incompatible visbility");
    }
}

// ---

ASTStructItem ASTStructItem::clone() const {
    return ASTStructItem(attrs.clone(), vis, name, type->clone(), defaultValue.clone());
}

ASTTupleItem ASTTupleItem::clone() const {
    return ASTTupleItem(attrs.clone(), vis, type->clone());
}

ASTTypeAlias ASTTypeAlias::clone() const {
    return ASTTypeAlias(params_.clone(), type_->clone());
}

ASTStatic ASTStatic::clone() const {
    return ASTStatic(cls, type_->clone(), value_.isValid() ? ASTExpr(value_.node().clone()) : ASTExpr(), params_.clone());
}

ASTFunction::ASTFunction(Span sp, ::std::string abi, Flags flags, ASTGenericParams params, ASTType* retType, Arglist args, bool isVariadic, bool hasNamedVariadic)
    : span_(sp)
    , params_(mv$(params))
    , rettype_(mv$(retType))
    , args_(mv$(args))
    , isVariadic_(isVariadic)
    , hasNamedVariadic_(hasNamedVariadic)
    , abi_(mv$(abi))
    , flags(flags)
{
}

ASTFunction ASTFunction::clone() const {
    decltype(args_) newArgs;
    for (const auto& arg : args_) {
        newArgs.push_back(ASTFunction::Arg(arg.pat.clone(), arg.ty->clone(), arg.attrs.clone()));
    }

    auto rv = ASTFunction(span_, abi_, flags, params_.clone(), rettype_->clone(), mv$(newArgs), isVariadic_, hasNamedVariadic_);
    if (code_.isValid()) {
        rv.code_ = ASTExpr(code_.node().clone());
    }
    if (delegation_) {
        Delegation delegation;
        for (const auto& target : delegation_->targets) {
            delegation.targets.push_back({ASTPath(target.path), target.name});
        }
        if (delegation_->body.isValid()) {
            delegation.body = ASTExpr(delegation_->body.node().clone());
        }
        rv.setDelegation(mv$(delegation));
    }
    rv.markings = markings;
    return rv;
}

void ASTTrait::addType(Span sp, RcString name, ASTAttributeList attrs, ASTType* type) {
    items_.push_back(ASTNamed<ASTItem>(sp, mv$(attrs), ASTVisibility::makeGlobal(), mv$(name), ASTItem::make_Type({ASTTypeAlias(ASTGenericParams(), mv$(type))})));
}

void ASTTrait::addFunction(Span sp, RcString name, ASTAttributeList attrs, ASTFunction fcn) {
    items_.push_back(ASTNamed<ASTItem>(sp, mv$(attrs), ASTVisibility::makeGlobal(), mv$(name), ASTItem::make_Function({mv$(fcn)})));
}

void ASTTrait::addStatic(Span sp, RcString name, ASTAttributeList attrs, ASTStatic v) {
    items_.push_back(ASTNamed<ASTItem>(sp, mv$(attrs), ASTVisibility::makeGlobal(), mv$(name), ASTItem::make_Static({mv$(v)})));
}

void ASTTrait::setIsMarker() {
    isMarker_ = true;
}

bool ASTTrait::isMarker() const {
    return isMarker_;
}

bool ASTTrait::hasNamedItem(const RcString& name, bool& outIsFcn) const {
    for (const auto& i : items_) {
        if (i.name == name) {
            outIsFcn = i.data.is_Function();
            return true;
        }
    }
    return false;
}

ASTTrait ASTTrait::clone() const {
    auto rv = ASTTrait(params_.clone(), supertraits_, lifetimes_);
    for (const auto& item : items_) {
        rv.items_.push_back(ASTNamed<ASTItem>{item.span, item.attrs.clone(), item.vis, item.name, item.data.clone()});
    }
    return rv;
}

ASTEnum ASTEnum::clone() const {
    decltype(variants_) newVariants;
    for (const auto& var : variants_) {
        switch (var.data.tag()) {
            case ASTEnumVariantData::TAG_Unit: {
                newVariants.push_back(ASTEnumVariant(var.attrs.clone(), var.name));
                break;
            }
            case ASTEnumVariantData::TAG_Tuple: {
                auto& e = var.data.as_Tuple();
                decltype(e.items) newSt; for (const auto& f : e.items) newSt.push_back(f.clone()); newVariants.push_back(ASTEnumVariant(var.attrs.clone(), var.name, mv$(newSt)));
                break;
            }
            case ASTEnumVariantData::TAG_Struct: {
                auto& e = var.data.as_Struct();
                decltype(e.fields) newFields; for (const auto& f : e.fields) newFields.push_back(f.clone()); newVariants.push_back(ASTEnumVariant(var.attrs.clone(), var.name, mv$(newFields)));
                break;
            }
        }
        newVariants.back().discriminantValue = var.discriminantValue.clone();
    }
    auto rv = ASTEnum(params_.clone(), mv$(newVariants));
    rv.markings = markings;
    return rv;
}

ASTStruct ASTStruct::clone() const {
    switch (data.tag()) {
        case ASTStructData::TAG_Unit: {
            return ASTStruct(params_.clone());
        }
        case ASTStructData::TAG_Tuple: {
            auto& e = data.as_Tuple();
            decltype(e.ents) newFields; for (const auto& f : e.ents) newFields.push_back(f.clone()); return ASTStruct(params_.clone(), mv$(newFields));
            break;
        }
        case ASTStructData::TAG_Struct: {
            auto& e = data.as_Struct();
            decltype(e.ents) newFields; for (const auto& f : e.ents) newFields.push_back(f.clone()); return ASTStruct(params_.clone(), mv$(newFields));
            break;
        }
    }
    UNREACHABLE();
}

ASTUnion ASTUnion::clone() const {
    decltype(variants) newVars;
    for (const auto& f : variants) {
        newVars.push_back(f.clone());
    }
    return ASTUnion(params_.clone(), mv$(newVars));
}

::std::ostream& operator<<(::std::ostream& os, const ASTImplDef& impl) {
    return os << "impl " << (impl.isConst_ ? "const " : "") << "<" << impl.params_ << "> " << impl.trait_.ent << " for " << impl.type_ << "";
}

void ASTImpl::addFunction(Span sp, ASTAttributeList attrs, ASTVisibility vis, bool isSpecialisable, RcString name, ASTFunction fcn, RcString sourceName) {
    if (sourceName == "") {
        sourceName = name;
    }
    items_.push_back(ImplItem{sp, mv$(attrs), mv$(vis), isSpecialisable, mv$(name), mv$(sourceName), box$(ASTItem::make_Function(mv$(fcn)))});
}

void ASTImpl::addType(Span sp, ASTAttributeList attrs, ASTVisibility vis, bool isSpecialisable, RcString name, ASTGenericParams params, ASTType* type, RcString sourceName) {
    if (sourceName == "") {
        sourceName = name;
    }
    items_.push_back(ImplItem{sp, mv$(attrs), mv$(vis), isSpecialisable, mv$(name), mv$(sourceName), box$(ASTItem::make_Type(ASTTypeAlias(mv$(params), mv$(type))))});
}

void ASTImpl::addStatic(Span sp, ASTAttributeList attrs, ASTVisibility vis, bool isSpecialisable, RcString name, ASTStatic v, RcString sourceName) {
    if (sourceName == "") {
        sourceName = name;
    }
    items_.push_back(ImplItem{sp, mv$(attrs), mv$(vis), isSpecialisable, mv$(name), mv$(sourceName), box$(ASTItem::make_Static(mv$(v)))});
}

void ASTImpl::addMacroInvocation(ASTMacroInvocation item) {
    items_.push_back(ImplItem{item.span(), {}, ASTVisibility::makeGlobal(), false, "", "", box$(ASTItem::make_MacroInv(mv$(item)))});
}

bool ASTImpl::hasNamedItem(const RcString& name) const {
    for (const auto& it : this->items()) {
        if (it.name == name) {
            return true;
        }
    }
    return false;
}

::std::ostream& operator<<(::std::ostream& os, const ASTImpl& impl) {
    return os << impl.def_;
}

::std::ostream& operator<<(::std::ostream& os, const ASTUseItem::Ent& x) {
    return os << x.name << "=" << x.path;
}

ASTMacroInvocation ASTMacroInvocation::clone() const {
    return ASTMacroInvocation(span_, ASTPath(macroPath), ident, input.clone());
}

ASTUseItem ASTUseItem::clone() const {
    decltype(this->entries) entries;
    for (const auto& e : this->entries) {
        entries.push_back({e.sp, e.path, e.name, e.isSelf});
    }
    return ASTUseItem{this->sp, this->isPrelude, mv$(entries)};
}

ASTExternBlock::ASTExternBlock(::std::string abi)
    : abi_(mv$(abi))
{
}

ASTExternBlock::~ASTExternBlock() = default;
ASTExternBlock::ASTExternBlock(ASTExternBlock&&) = default;
ASTExternBlock& ASTExternBlock::operator=(ASTExternBlock&&) = default;

void ASTExternBlock::addItem(ASTNamed<ASTItem> namedItem) {
    ASSERT_BUG(namedItem.span, namedItem.data.is_Function() || namedItem.data.is_Static() || namedItem.data.is_Type() || namedItem.data.is_MacroInv(), "Incorrect item type for ExternBlock - " << namedItem.data.tagStr());
    items_.push_back(mv$(namedItem));
}

ASTExternBlock ASTExternBlock::clone() const {
    TODO(Span(), "Clone an extern block");
}

ASTGlobalAsm ASTGlobalAsm::clone() const {
    std::vector<Operand> clonedOperands;
    clonedOperands.reserve(operands.size());
    for (const auto& operand : operands) {
        switch (operand.tag()) {
            case ASTGlobalAsmOperand::TAG_Const: {
                auto& expr = operand.as_Const();
                clonedOperands.push_back(Operand::make_Const(expr->clone()));
                break;
            }
            case ASTGlobalAsmOperand::TAG_Sym: {
                auto& path = operand.as_Sym();
                clonedOperands.push_back(Operand::make_Sym(path));
                break;
            }
        }
    }
    return ASTGlobalAsm{lines, std::move(clonedOperands), options};
}

ASTModule::ASTModule() = default;

ASTModule::ASTModule(ASTAbsolutePath path)
    : myPath(mv$(path))
{
}

ASTModule::~ASTModule() = default;
ASTModule::ASTModule(ASTModule&&) = default;
ASTModule& ASTModule::operator=(ASTModule&&) = default;

::std::shared_ptr<ASTModule> ASTModule::addAnon() {
    auto rv = ::std::shared_ptr<ASTModule>(new ASTModule(myPath + RcString::newInterned(FMT("#" << anonModules.size()))));
    DEBUG("New anon " << rv->myPath);
    rv->fileInfo = fileInfo;

    anonModules.push_back(rv);

    return rv;
}

void ASTModule::addItem(ASTNamed<ASTItem> namedItem) {
    items.push_back(box$(namedItem));
    const auto& i = items.back();
    if (i->name == "") {
    } else {
        DEBUG(myPath << "::" << i->name << " = " << i->data.tagStr() << ", attrs = " << i->attrs);
    }
}

void ASTModule::addItem(Span sp, ASTVisibility vis, RcString name, ASTItem it, ASTAttributeList attrs) {
    addItem(ASTNamed<ASTItem>(mv$(sp), mv$(attrs), mv$(vis), mv$(name), mv$(it)));
}

void ASTModule::addExtCrate(Span sp, ASTVisibility vis, RcString extName, RcString impName, ASTAttributeList attrs) {
    this->addItem(mv$(sp), mv$(vis), impName, ASTItem::make_Crate({mv$(extName)}), mv$(attrs));
}

void ASTModule::addMacroInvocation(ASTMacroInvocation item) {
    this->addItem(item.span(), ASTVisibility::makeGlobal(), "", ASTItem(mv$(item)), ASTAttributeList{});
}

void ASTModule::addMacro(bool isExported, RcString name, MacroRulesPtr macro) {
    assert(macro);
    assert(macro->rules.size() > 0);
    macros_.push_back(
        ASTNamed<MacroRulesPtr>(
            Span(),
            {},
            /*is_pub=*/isExported ? ASTVisibility::makeGlobal() : ASTVisibility::makeRestricted(ASTVisibility::Ty::Private, myPath),
            mv$(name),
            mv$(macro)
        )
    );
}

ASTItem ASTItem::clone() const {
    switch ((*this).tag()) {
        case ASTItem::TAG_None: {
            auto& e = (*this).as_None();
            return ASTItem(e);
        }
        case ASTItem::TAG_MacroInv: {
            TODO(Span(), "Clone on Item::MacroInv");
            break;
        }
        case ASTItem::TAG_Macro: {
            TODO(Span(), "Clone on Item::Macro");
            break;
        }
        case ASTItem::TAG_Use: {
            auto& e = (*this).as_Use();
            return ASTItem(e.clone());
        }
        case ASTItem::TAG_ExternBlock: {
            TODO(Span(), "Clone on Item::" << this->tagStr());
            break;
        }
        case ASTItem::TAG_GlobalAsm: {
            auto& e = (*this).as_GlobalAsm();
            return ASTItem(e.clone());
        }
        case ASTItem::TAG_Impl: {
            TODO(Span(), "Clone on Item::" << this->tagStr());
            break;
        }
        case ASTItem::TAG_NegImpl: {
            TODO(Span(), "Clone on Item::" << this->tagStr());
            break;
        }
        case ASTItem::TAG_Module: {
            TODO(Span(), "Clone on Item::" << this->tagStr());
            break;
        }
        case ASTItem::TAG_Crate: {
            auto& e = (*this).as_Crate();
            return ASTItem(e);
        }
        case ASTItem::TAG_Type: {
            auto& e = (*this).as_Type();
            return ASTItem(e.clone());
        }
        case ASTItem::TAG_Struct: {
            auto& e = (*this).as_Struct();
            return ASTItem(e.clone());
        }
        case ASTItem::TAG_Enum: {
            auto& e = (*this).as_Enum();
            return ASTItem(e.clone());
        }
        case ASTItem::TAG_Union: {
            auto& e = (*this).as_Union();
            return ASTItem(e.clone());
        }
        case ASTItem::TAG_Trait: {
            auto& e = (*this).as_Trait();
            return ASTItem(e.clone());
        }
        case ASTItem::TAG_TraitAlias: {
            auto& e = (*this).as_TraitAlias();
            return ASTItem(e.clone());
        }
        case ASTItem::TAG_Function: {
            auto& e = (*this).as_Function();
            return ASTItem(e.clone());
        }
        case ASTItem::TAG_Static: {
            auto& e = (*this).as_Static();
            return ASTItem(e.clone());
        }
    }
    UNREACHABLE();
}

::std::ostream& operator<<(::std::ostream& os, const ASTTypeParam& tp) {
    os << tp.name_;
    os << " = ";
    os << tp.defaultValue_;
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const ASTLifetimeParam& p) {
    os << "'" << p.name_;
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const ASTValueParam& p) {
    os << "const " << p.name_ << ": " << p.type_;
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const ASTHigherRankedBounds& x) {
    if (!x.empty()) {
        os << "for<";
        for (const auto& l : x.lifetimes) {
            os << l << ",";
        }
        for (const auto& t : x.types) {
            os << t << ",";
        }
        os << "> ";
    }
    return os;
}

std::ostream& operator<<(std::ostream& os, const GenericParam& x) {
    switch (x.tag()) {
        case GenericParam::TAG_None: {
            os << "/*-*/";
            break;
        }
        case GenericParam::TAG_Lifetime: {
            auto& e = x.as_Lifetime();
            os << e;
            break;
        }
        case GenericParam::TAG_Type: {
            auto& e = x.as_Type();
            os << e;
            break;
        }
        case GenericParam::TAG_Value: {
            auto& e = x.as_Value();
            os << e;
            break;
        }
    }
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const ASTGenericBound& x) {
    switch (x.tag()) {
        case ASTGenericBound::TAG_None: {
            os << "/*-*/";
            break;
        }
        case ASTGenericBound::TAG_Lifetime: {
            auto& ent = x.as_Lifetime();
            os << ent.test << ": " << ent.bound;
            break;
        }
        case ASTGenericBound::TAG_TypeLifetime: {
            auto& ent = x.as_TypeLifetime();
            os << ent.type << ": " << ent.bound;
            break;
        }
        case ASTGenericBound::TAG_IsTrait: {
            auto& ent = x.as_IsTrait();
            os << ent.outerHrbs << ent.type << ": ";
            if (ent.constness == ASTBoundConstness::Always) {
                os << "const ";
            } else if (ent.constness == ASTBoundConstness::Maybe) {
                os << "[const] ";
            }
            os << ent.innerHrbs << ent.trait;
            break;
        }
        case ASTGenericBound::TAG_MaybeTrait: {
            auto& ent = x.as_MaybeTrait();
            os << ent.type << ": ?" << ent.trait;
            break;
        }
        case ASTGenericBound::TAG_NotTrait: {
            auto& ent = x.as_NotTrait();
            os << ent.type << ": !" << ent.trait;
            break;
        }
        case ASTGenericBound::TAG_Equality: {
            auto& ent = x.as_Equality();
            os << ent.type << " = " << ent.replacement;
            break;
        }
    }
    return os;
}

//int GenericParams::find_name(const char* name) const
//{
//    for( unsigned int i = 0; i < m_type_params.size(); i ++ )
//    {
//        if( m_type_params[i].name() == name )
//    }
//}

::std::ostream& operator<<(::std::ostream& os, const ASTGenericParams& tps) {
    return os << "<" << tps.params << "> where {" << tps.bounds << "}";
}

ASTAttributeList::ASTAttributeList() = default;

ASTAttributeList::ASTAttributeList(::std::vector<ASTAttribute> items)
    : items(mv$(items))
{
}

ASTAttributeList::~ASTAttributeList() = default;
ASTAttributeList::ASTAttributeList(ASTAttributeList&&) = default;
ASTAttributeList& ASTAttributeList::operator=(ASTAttributeList&&) = default;
ASTAttributeList::ASTAttributeList(const ASTAttributeList&) = default;

//StructItem() {}

ASTStructItem::ASTStructItem(ASTAttributeList attrs, ASTVisibility vis, RcString name, ASTType* ty, ASTExpr defaultValue)
    : attrs(mv$(attrs))
    , vis(mv$(vis))
    , name(mv$(name))
    , type(mv$(ty))
    , defaultValue(mv$(defaultValue))
{
}

//TupleItem() {}

ASTTupleItem::ASTTupleItem(ASTAttributeList attrs, ASTVisibility vis, ASTType* ty)
    : attrs(mv$(attrs))
    , vis(mv$(vis))
    , type(mv$(ty))
{
}

//TypeAlias() {}
ASTTypeAlias::ASTTypeAlias(ASTGenericParams params, ASTType* type)
    : params_(std::move(params))
    , type_(std::move(type))
{
}

ASTTypeAlias ASTTypeAlias::newAssociatedType(ASTGenericParams params, ASTGenericParams typeBounds, ASTType* defaultType) {
    ASTTypeAlias rv{std::move(params), std::move(defaultType)};
    rv.selfBounds = std::move(typeBounds);
    return rv;
}

ASTTraitAlias ASTTraitAlias::clone() const {
    ASTTraitAlias rv;
    rv.params = params.clone();
    for (const auto& p : this->traits) {
        rv.traits.push_back(p);
    }
    rv.lifetimes = lifetimes;
    return rv;
}

ASTStatic::ASTStatic(Class sClass, ASTType* type, ASTExpr value, ASTGenericParams params)
    : cls(sClass)
    , params_(std::move(params))
    , type_(std::move(type))
    , value_(std::move(value))
{
}

ASTFunction::Arg::Arg(ASTPattern pat, ASTType* ty, ASTAttributeList attrs)
    : attrs(mv$(attrs))
    , pat(mv$(pat))
    , ty(mv$(ty))
{
}

ASTFunction::Flags::Flags()
    : isConst(false)
    , isUnsafe(false)
    , isAsync(false)
    , isGen(false)
{
}

ASTFunction::Flags ASTFunction::Flags::setUnsafe() const {
    auto rv = *this;
    rv.isUnsafe = true;
    return rv;
}

ASTFunction::Flags ASTFunction::Flags::setConst() const {
    auto rv = *this;
    rv.isConst = true;
    return rv;
}

ASTFunction::Flags ASTFunction::Flags::setAsync() const {
    auto rv = *this;
    rv.isAsync = true;
    return rv;
}

ASTFunction::Flags ASTFunction::Flags::setGen() const {
    auto rv = *this;
    rv.isGen = true;
    return rv;
}

// Helper for derive, defines an ABI_RUST function with no generics
ASTFunction::ASTFunction(Span sp, ASTType* retType, Arglist args)
    : ASTFunction(sp, ABI_RUST, Flags(), ASTGenericParams(), std::move(retType), std::move(args), false)
{
}

ASTEnumVariant::ASTEnumVariant() {
}

ASTEnumVariant::ASTEnumVariant(ASTAttributeList attrs, RcString name)
    : attrs(mv$(attrs))
    , name(mv$(name))
    , data(ASTEnumVariantData::make_Unit({}))
{
}

ASTEnumVariant::ASTEnumVariant(ASTAttributeList attrs, RcString name, ::std::vector<ASTTupleItem> subTypes)
    : attrs(mv$(attrs))
    , name(::std::move(name))
    , data(ASTEnumVariantData::make_Tuple({std::move(subTypes)}))
{
}

ASTEnumVariant::ASTEnumVariant(ASTAttributeList attrs, RcString name, ::std::vector<ASTStructItem> fields)
    : attrs(mv$(attrs))
    , name(::std::move(name))
    , data(ASTEnumVariantData::make_Struct({std::move(fields)}))
{
}

ASTEnum::ASTEnum() {
}

ASTEnum::ASTEnum(ASTGenericParams params, ::std::vector<ASTEnumVariant> variants)
    : params_(::std::move(params))
    , variants_(::std::move(variants))
{
}

ASTStruct::Markings::Markings() {
}

ASTStruct::ASTStruct() {
}

ASTStruct::ASTStruct(ASTGenericParams params)
    : params_(::std::move(params))
    , data(ASTStructData::make_Unit({}))
{
}

ASTStruct::ASTStruct(ASTGenericParams params, ::std::vector<ASTStructItem> fields)
    : params_(::std::move(params))
    , data(ASTStructData::make_Struct({mv$(fields)}))
{
}

ASTStruct::ASTStruct(ASTGenericParams params, ::std::vector<ASTTupleItem> fields)
    : params_(::std::move(params))
    , data(ASTStructData::make_Tuple({mv$(fields)}))
{
}

ASTUnion::ASTUnion(ASTGenericParams params, ::std::vector<ASTStructItem> fields)
    : params_(::std::move(params))
    , variants(::std::move(fields))
{
}

ASTImplDef::ASTImplDef(ASTGenericParams params, Spanned<ASTPath> traitType, ASTType* implType)
    : isUnsafe_(false)
    , isConst_(false)
    , params_(mv$(params))
    , trait_(mv$(traitType))
    , type_(mv$(implType))
{
}

::std::ostream& operator<<(::std::ostream& os, const ASTEnumVariant& x) {
    os << "EnumVariant(" << x.name;
    switch (x.data.tag()) {
        case ASTEnumVariantData::TAG_Unit: {
            break;
        }
        case ASTEnumVariantData::TAG_Tuple: {
            auto& e = x.data.as_Tuple();
            os << "(" << e.items << ")";
            break;
        }
        case ASTEnumVariantData::TAG_Struct: {
            auto& e = x.data.as_Struct();
            os << " { " << e.fields << " }";
            break;
        }
    }
    if (x.discriminantValue) {
        os << " = " << x.discriminantValue;
    }
    return os << ")";
}
