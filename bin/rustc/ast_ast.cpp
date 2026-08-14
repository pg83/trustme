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
    : mIsMarker(false)
    , mIsUnsafe(false)
{
}

ASTTrait::ASTTrait(ASTGenericParams params, ::std::vector<Spanned<TypeTraitPath>> supertraits, ::std::vector<Spanned<ASTLifetimeRef>> lifetimes)
    : mParams(mv$(params))
    , mSupertraits(mv$(supertraits))
    , mLifetimes(mv$(lifetimes))
    , mIsMarker(false)
    , mIsUnsafe(false)
{
}

ASTTrait::~ASTTrait() = default;
ASTTrait::ASTTrait(ASTTrait&&) = default;
ASTTrait& ASTTrait::operator=(ASTTrait&&) = default;

ASTImpl::ASTImpl(ASTImplDef def)
    : mDef(mv$(def))
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
    return ASTAttributeList(cloneMivec(mItems));
}

void ASTAttributeList::push_back(ASTAttribute i) {
    mItems.push_back(::std::move(i));
}

const ASTAttribute* ASTAttributeList::get(const char* name) const {
    for (auto& i : mItems) {
        if (i.name() == name) {
            return &i;
        }
    }
    return nullptr;
}

::std::ostream& operator<<(::std::ostream& os, const ASTAttributeList& x) {
    for (const auto& i : x.mItems) {
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
    : mSpan(x.mSpan)
    , mName(x.mName)
    , mData(x.mData.clone())
    , mIsInert(x.mIsInert)
{
}

ASTAttribute ASTAttribute::clone() const {
    return ASTAttribute(*this);
}

void ASTAttribute::fmt(std::ostream& os) const {
    os << mName;
    os << mData;
}

std::string ASTAttribute::parseEqualsString(const WireBoard& wb, const ASTCrate& crate, const ASTModule& mod) const {
    TTStream lex(this->mSpan, ParseState(), this->data());
    lex.parseState().wb = &wb;
    lex.getTokenCheck(TOK_EQUAL);
    auto n = ExpandParseAndExpandExprVal(crate, mod, lex);

    std::string rv;
    if (auto* v = cast<ASTExprNodeString>(&*n)) {
        rv = v->mValue;
    } else {
        throw ParseErrorUnexpected(lex, Token(InterpolatedFragment(InterpolatedFragment::EXPR, n.release())), TOK_STRING);
    }
    lex.getTokenCheck(TOK_EOF);
    return rv;
}

std::string ASTAttribute::parseParenString() const {
    TTStream lex(this->mSpan, ParseState(), this->data());
    lex.getTokenCheck(TOK_PAREN_OPEN);
    auto rv = lex.getTokenCheck(TOK_STRING).str();
    lex.getTokenCheck(TOK_PAREN_CLOSE);
    return rv;
}

void ASTAttribute::parseParenIdentList(std::function<void(const Span& sp, RcString ident)> itemCb) const {
    TTStream lex(this->mSpan, ParseState(), this->data());
    lex.getTokenCheck(TOK_PAREN_OPEN);
    while (lex.lookahead(0) != TOK_PAREN_CLOSE) {
        itemCb(lex.pointSpan(), lex.getTokenCheck(TOK_IDENT).ident().name);
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
    rv.mTy = ty;
    rv.mVisPath = std::make_shared<ASTAbsolutePath>(std::move(p));
    return rv;
}

ASTVisibility ASTVisibility::makeRestricted(ASTAbsolutePath p, ASTPath inPath) {
    ASTVisibility rv;
    rv.mTy = Ty::PubIn;
    rv.mVisPath = std::make_shared<ASTAbsolutePath>(std::move(p));
    rv.mInPath = std::make_shared<ASTPath>(std::move(inPath));
    return rv;
}

void ASTVisibility::fmt(::std::ostream& os) const {
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
            if (mInPath) {
                os << *mInPath;
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
    if (mVisPath) {
        if (mVisPath->crate != fromMod.crate) {
            return false;
        }
        if (mVisPath->nodes.size() > fromMod.nodes.size()) {
            return false;
        }
        for (size_t i = 0; i < mVisPath->nodes.size(); i++) {
            if (mVisPath->nodes[i] != fromMod.nodes[i]) {
                return false;
            }
        }
        return true;
    } else {
        return true;
    }
}

bool ASTVisibility::contains(const ASTVisibility& x) const {
    if (mVisPath) {
        return x.isVisible(*mVisPath);
    } else {
        return true;
    }
}

void ASTVisibility::inplaceUnion(const ASTVisibility& x) {
    if (this->contains(x)) {
    } else if (x.contains(*this)) {
        mVisPath = x.mVisPath;
    } else {
        TODO(Span(), "Union with incompatible visbility");
    }
}

// ---

ASTStructItem ASTStructItem::clone() const {
    return ASTStructItem(mAttrs.clone(), vis, mName, mType->clone(), defaultValue.clone());
}

ASTTupleItem ASTTupleItem::clone() const {
    return ASTTupleItem(mAttrs.clone(), vis, mType->clone());
}

ASTTypeAlias ASTTypeAlias::clone() const {
    return ASTTypeAlias(mParams.clone(), mType->clone());
}

ASTStatic ASTStatic::clone() const {
    return ASTStatic(cls, mType->clone(), mValue.isValid() ? ASTExpr(mValue.node().clone()) : ASTExpr());
}

ASTFunction::ASTFunction(Span sp, ::std::string abi, Flags flags, ASTGenericParams params, ASTType* retType, Arglist args, bool isVariadic)
    : mSpan(sp)
    , mParams(mv$(params))
    , mRettype(mv$(retType))
    , mArgs(mv$(args))
    , mIsVariadic(isVariadic)
    , mAbi(mv$(abi))
    , flags(flags)
{
}

ASTFunction ASTFunction::clone() const {
    decltype(mArgs) newArgs;
    for (const auto& arg : mArgs) {
        newArgs.push_back(ASTFunction::Arg(arg.pat.clone(), arg.ty->clone(), arg.attrs.clone()));
    }

    auto rv = ASTFunction(mSpan, mAbi, flags, mParams.clone(), mRettype->clone(), mv$(newArgs), mIsVariadic);
    if (mCode.isValid()) {
        rv.mCode = ASTExpr(mCode.node().clone());
    }
    return rv;
}

void ASTTrait::addType(Span sp, RcString name, ASTAttributeList attrs, ASTType* type) {
    mItems.push_back(ASTNamed<ASTItem>(sp, mv$(attrs), ASTVisibility::makeGlobal(), mv$(name), ASTItem::make_Type({ASTTypeAlias(ASTGenericParams(), mv$(type))})));
}

void ASTTrait::addFunction(Span sp, RcString name, ASTAttributeList attrs, ASTFunction fcn) {
    mItems.push_back(ASTNamed<ASTItem>(sp, mv$(attrs), ASTVisibility::makeGlobal(), mv$(name), ASTItem::make_Function({mv$(fcn)})));
}

void ASTTrait::addStatic(Span sp, RcString name, ASTAttributeList attrs, ASTStatic v) {
    mItems.push_back(ASTNamed<ASTItem>(sp, mv$(attrs), ASTVisibility::makeGlobal(), mv$(name), ASTItem::make_Static({mv$(v)})));
}

void ASTTrait::setIsMarker() {
    mIsMarker = true;
}

bool ASTTrait::isMarker() const {
    return mIsMarker;
}

bool ASTTrait::hasNamedItem(const RcString& name, bool& outIsFcn) const {
    for (const auto& i : mItems) {
        if (i.name == name) {
            outIsFcn = i.data.is_Function();
            return true;
        }
    }
    return false;
}

ASTTrait ASTTrait::clone() const {
    auto rv = ASTTrait(mParams.clone(), mSupertraits, mLifetimes);
    for (const auto& item : mItems) {
        rv.mItems.push_back(ASTNamed<ASTItem>{item.span, item.attrs.clone(), item.vis, item.name, item.data.clone()});
    }
    return rv;
}

ASTEnum ASTEnum::clone() const {
    decltype(mVariants) newVariants;
    for (const auto& var : mVariants) {
        TU_MATCHA((var.mData), (e), (Unit, newVariants.push_back(ASTEnumVariant(var.mAttrs.clone(), var.mName));), (Tuple, decltype(e.mItems) newSt; for (const auto& f : e.mItems) newSt.push_back(f.clone()); newVariants.push_back(ASTEnumVariant(var.mAttrs.clone(), var.mName, mv$(newSt)));), (Struct, decltype(e.fields) newFields; for (const auto& f : e.fields) newFields.push_back(f.clone()); newVariants.push_back(ASTEnumVariant(var.mAttrs.clone(), var.mName, mv$(newFields)));))
        newVariants.back().discriminantValue = var.discriminantValue.clone();
    }
    auto rv = ASTEnum(mParams.clone(), mv$(newVariants));
    rv.markings = markings;
    return rv;
}

ASTStruct ASTStruct::clone() const {
    TU_MATCHA((mData), (e), (Unit, return ASTStruct(mParams.clone());), (Tuple, decltype(e.ents) newFields; for (const auto& f : e.ents) newFields.push_back(f.clone()); return ASTStruct(mParams.clone(), mv$(newFields));), (Struct, decltype(e.ents) newFields; for (const auto& f : e.ents) newFields.push_back(f.clone()); return ASTStruct(mParams.clone(), mv$(newFields));))
    throw "";
}

ASTUnion ASTUnion::clone() const {
    decltype(mVariants) newVars;
    for (const auto& f : mVariants) {
        newVars.push_back(f.clone());
    }
    return ASTUnion(mParams.clone(), mv$(newVars));
}

::std::ostream& operator<<(::std::ostream& os, const ASTImplDef& impl) {
    return os << "impl " << (impl.mIsConst ? "const " : "") << "<" << impl.mParams << "> " << impl.mTrait.ent << " for " << impl.mType << "";
}

void ASTImpl::addFunction(Span sp, ASTAttributeList attrs, ASTVisibility vis, bool isSpecialisable, RcString name, ASTFunction fcn) {
    mItems.push_back(ImplItem{sp, mv$(attrs), mv$(vis), isSpecialisable, mv$(name), box$(ASTItem::make_Function(mv$(fcn)))});
}

void ASTImpl::addType(Span sp, ASTAttributeList attrs, ASTVisibility vis, bool isSpecialisable, RcString name, ASTGenericParams params, ASTType* type) {
    mItems.push_back(ImplItem{sp, mv$(attrs), mv$(vis), isSpecialisable, mv$(name), box$(ASTItem::make_Type(ASTTypeAlias(mv$(params), mv$(type))))});
}

void ASTImpl::addStatic(Span sp, ASTAttributeList attrs, ASTVisibility vis, bool isSpecialisable, RcString name, ASTStatic v) {
    mItems.push_back(ImplItem{sp, mv$(attrs), mv$(vis), isSpecialisable, mv$(name), box$(ASTItem::make_Static(mv$(v)))});
}

void ASTImpl::addMacroInvocation(ASTMacroInvocation item) {
    mItems.push_back(ImplItem{item.span(), {}, ASTVisibility::makeGlobal(), false, "", box$(ASTItem::make_MacroInv(mv$(item)))});
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
    return os << impl.mDef;
}

::std::ostream& operator<<(::std::ostream& os, const ASTUseItem::Ent& x) {
    return os << x.name << "=" << x.path;
}

ASTMacroInvocation ASTMacroInvocation::clone() const {
    return ASTMacroInvocation(mSpan, ASTPath(macroPath), ident, input.clone());
}

ASTUseItem ASTUseItem::clone() const {
    decltype(this->entries) entries;
    for (const auto& e : this->entries) {
        entries.push_back({e.sp, e.path, e.name});
    }
    return ASTUseItem{this->sp, mv$(entries)};
}

ASTExternBlock::ASTExternBlock(::std::string abi)
    : mAbi(mv$(abi))
{
}

ASTExternBlock::~ASTExternBlock() = default;
ASTExternBlock::ASTExternBlock(ASTExternBlock&&) = default;
ASTExternBlock& ASTExternBlock::operator=(ASTExternBlock&&) = default;

void ASTExternBlock::addItem(ASTNamed<ASTItem> namedItem) {
    ASSERT_BUG(namedItem.span, namedItem.data.is_Function() || namedItem.data.is_Static() || namedItem.data.is_Type() || namedItem.data.is_MacroInv(), "Incorrect item type for ExternBlock - " << namedItem.data.tagStr());
    mItems.push_back(mv$(namedItem));
}

ASTExternBlock ASTExternBlock::clone() const {
    TODO(Span(), "Clone an extern block");
}

ASTGlobalAsm ASTGlobalAsm::clone() const {
    std::vector<Operand> clonedOperands;
    clonedOperands.reserve(operands.size());
    for (const auto& operand : operands) {
        TU_MATCH_HDRA((operand), {)
        TU_ARMA(Const, expr) {
                clonedOperands.push_back(Operand::make_Const(expr->clone()));
            }
            TU_ARMA(Sym, path) {
                clonedOperands.push_back(Operand::make_Sym(path));
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
    mItems.push_back(box$(namedItem));
    const auto& i = mItems.back();
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
    mMacros.push_back(
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
    TU_MATCHA(
        (*this),
        (e),
        (None, return ASTItem(e);),
        (MacroInv, TODO(Span(), "Clone on Item::MacroInv");),
        (Macro, TODO(Span(), "Clone on Item::Macro");),
        (Use, return ASTItem(e.clone());),
        (ExternBlock, TODO(Span(), "Clone on Item::" << this->tagStr());),
        (GlobalAsm, return ASTItem(e.clone());),
        (Impl, TODO(Span(), "Clone on Item::" << this->tagStr());),
        (NegImpl, TODO(Span(), "Clone on Item::" << this->tagStr());),
        (Module, TODO(Span(), "Clone on Item::" << this->tagStr());),
        (Crate, return ASTItem(e);),
        (Type, return ASTItem(e.clone());),
        (Struct, return ASTItem(e.clone());),
        (Enum, return ASTItem(e.clone());),
        (Union, return ASTItem(e.clone());),
        (Trait, return ASTItem(e.clone());),
        (TraitAlias, return ASTItem(e.clone());),

        (Function, return ASTItem(e.clone());),
        (Static, return ASTItem(e.clone());)
    )
    throw "";
}

::std::ostream& operator<<(::std::ostream& os, const ASTTypeParam& tp) {
    os << tp.mName;
    os << " = ";
    os << tp.mDefaultValue;
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const ASTLifetimeParam& p) {
    os << "'" << p.mName;
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const ASTValueParam& p) {
    os << "const " << p.mName << ": " << p.mType;
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const ASTHigherRankedBounds& x) {
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
        return ASTLifetimeParam(e);
        TU_ARMA(Type, e)
        return ASTTypeParam(e);
        TU_ARMA(Value, e)
        return ASTValueParam(e);
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

::std::ostream& operator<<(::std::ostream& os, const ASTGenericBound& x) {
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
            os << ent.outerHrbs << ent.type << ": ";
            if (ent.constness == ASTBoundConstness::Always) {
                os << "const ";
            } else if (ent.constness == ASTBoundConstness::Maybe) {
                os << "[const] ";
            }
            os << ent.innerHrbs << ent.trait;
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
//    }
//}

::std::ostream& operator<<(::std::ostream& os, const ASTGenericParams& tps) {
    return os << "<" << tps.mParams << "> where {" << tps.bounds << "}";
}

ASTAttributeList::ASTAttributeList() = default;

ASTAttributeList::ASTAttributeList(::std::vector<ASTAttribute> items)
    : mItems(mv$(items))
{
}

ASTAttributeList::~ASTAttributeList() = default;
ASTAttributeList::ASTAttributeList(ASTAttributeList&&) = default;
ASTAttributeList& ASTAttributeList::operator=(ASTAttributeList&&) = default;
ASTAttributeList::ASTAttributeList(const ASTAttributeList&) = default;

//StructItem() {}

ASTStructItem::ASTStructItem(ASTAttributeList attrs, ASTVisibility vis, RcString name, ASTType* ty, ASTExpr defaultValue)
    : mAttrs(mv$(attrs))
    , vis(mv$(vis))
    , mName(mv$(name))
    , mType(mv$(ty))
    , defaultValue(mv$(defaultValue))
{
}

//TupleItem() {}

ASTTupleItem::ASTTupleItem(ASTAttributeList attrs, ASTVisibility vis, ASTType* ty)
    : mAttrs(mv$(attrs))
    , vis(mv$(vis))
    , mType(mv$(ty))
{
}

//TypeAlias() {}
ASTTypeAlias::ASTTypeAlias(ASTGenericParams params, ASTType* type)
    : mParams(std::move(params))
    , mType(std::move(type))
{
}

ASTTypeAlias ASTTypeAlias::newAssociatedType(ASTGenericParams params, ASTGenericParams typeBounds, ASTType* defaultType) {
    ASTTypeAlias rv{std::move(params), std::move(defaultType)};
    rv.selfBounds = std::move(typeBounds);
    return rv;
}

ASTTraitAlias ASTTraitAlias::clone() const {
    ASTTraitAlias rv;
    for (const auto& p : this->traits) {
        rv.traits.push_back(p);
    }
    return rv;
}

ASTStatic::ASTStatic(Class sClass, ASTType* type, ASTExpr value)
    : cls(sClass)
    , mType(std::move(type))
    , mValue(std::move(value))
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

// Helper for derive, defines an ABI_RUST function with no generics
ASTFunction::ASTFunction(Span sp, ASTType* retType, Arglist args)
    : ASTFunction(sp, ABI_RUST, Flags(), ASTGenericParams(), std::move(retType), std::move(args), false)
{
}

ASTEnumVariant::ASTEnumVariant() {
}

ASTEnumVariant::ASTEnumVariant(ASTAttributeList attrs, RcString name)
    : mAttrs(mv$(attrs))
    , mName(mv$(name))
    , mData(ASTEnumVariantData::make_Unit({}))
{
}

ASTEnumVariant::ASTEnumVariant(ASTAttributeList attrs, RcString name, ::std::vector<ASTTupleItem> subTypes)
    : mAttrs(mv$(attrs))
    , mName(::std::move(name))
    , mData(ASTEnumVariantData::make_Tuple({std::move(subTypes)}))
{
}

ASTEnumVariant::ASTEnumVariant(ASTAttributeList attrs, RcString name, ::std::vector<ASTStructItem> fields)
    : mAttrs(mv$(attrs))
    , mName(::std::move(name))
    , mData(ASTEnumVariantData::make_Struct({std::move(fields)}))
{
}

ASTEnum::ASTEnum() {
}

ASTEnum::ASTEnum(ASTGenericParams params, ::std::vector<ASTEnumVariant> variants)
    : mParams(::std::move(params))
    , mVariants(::std::move(variants))
{
}

ASTStruct::Markings::Markings() {
}

ASTStruct::ASTStruct() {
}

ASTStruct::ASTStruct(ASTGenericParams params)
    : mParams(::std::move(params))
    , mData(ASTStructData::make_Unit({}))
{
}

ASTStruct::ASTStruct(ASTGenericParams params, ::std::vector<ASTStructItem> fields)
    : mParams(::std::move(params))
    , mData(ASTStructData::make_Struct({mv$(fields)}))
{
}

ASTStruct::ASTStruct(ASTGenericParams params, ::std::vector<ASTTupleItem> fields)
    : mParams(::std::move(params))
    , mData(ASTStructData::make_Tuple({mv$(fields)}))
{
}

ASTUnion::ASTUnion(ASTGenericParams params, ::std::vector<ASTStructItem> fields)
    : mParams(::std::move(params))
    , mVariants(::std::move(fields))
{
}

ASTImplDef::ASTImplDef(ASTGenericParams params, Spanned<ASTPath> traitType, ASTType* implType)
    : mIsUnsafe(false)
    , mIsConst(false)
    , mParams(mv$(params))
    , mTrait(mv$(traitType))
    , mType(mv$(implType))
{
}

::std::ostream& operator<<(::std::ostream& os, const ASTEnumVariant& x) {
    os << "EnumVariant(" << x.mName;
    TU_MATCH(ASTEnumVariantData, (x.mData), (e), (Unit, ), (Tuple, os << "(" << e.mItems << ")";), (Struct, os << " { " << e.fields << " }";))
    if (x.discriminantValue) {
        os << " = " << x.discriminantValue;
    }
    return os << ")";
}
