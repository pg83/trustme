#include "ast_path.h"

#include "ast_ast.h"
#include "ast_expr.h"
#include "ast_types.h"
#include "parse_parseerror.h"

#include <iostream>
#include <algorithm>

#define PRETTY_PATH_PRINT 1

// --- AST::PathBinding
::std::ostream& operator<<(::std::ostream& os, const ASTPathBindingType& x) {
    TU_MATCHA((x), (i), (Unbound, os << "_";), (Crate, os << "Crate";), (Primitive, os << "Primitive";), (Module, os << "Module";), (Trait, os << "Trait";), (TraitAlias, os << "TraitAlias";), (Struct, os << "Struct";), (Enum, os << "Enum";), (Union, os << "Union";), (EnumVar, os << "EnumVar(" << i.idx << ")";), (TypeAlias, os << "TypeAlias";), (TypeParameter, os << "TyParam(" << i.slot << ")";))
    return os;
}

ASTPathBindingType ASTPathBindingType::clone() const {
    TU_MATCHA(
        (*this),
        (e),
        (Unbound, return ASTPathBindingType::make_Unbound({});),
        (Primitive, return e;),
        (Module, return ASTPathBindingType::make_Module(e);),
        (Crate, return ASTPathBindingType(e);),
        (Trait, return ASTPathBindingType(e);),
        (TraitAlias, return ASTPathBindingType(e);),
        (Struct, return ASTPathBindingType(e);),
        (Enum, return ASTPathBindingType(e);),
        (Union, return ASTPathBindingType(e);),
        (TypeAlias, return ASTPathBindingType::make_TypeAlias(e);),
        (EnumVar, return ASTPathBindingType::make_EnumVar(e);),

        (TypeParameter, return ASTPathBindingType::make_TypeParameter(e);)
    )
    throw "BUG: Fell off the end of PathBinding_Type::clone";
}

::std::ostream& operator<<(::std::ostream& os, const ASTPathBindingValue& x) {
    TU_MATCHA((x), (i), (Unbound, os << "_";), (Struct, os << "Struct";), (Static, os << "Static";), (Function, os << "Function";), (EnumVar, os << "EnumVar(" << i.idx << ")";), (Generic, os << "Param(" << i.index << ")";), (Variable, os << "Var(" << i.slot << ")";))
    return os;
}

ASTPathBindingValue ASTPathBindingValue::clone() const {
    TU_MATCHA((*this), (e), (Unbound, return ASTPathBindingValue::make_Unbound({});), (Struct, return ASTPathBindingValue(e);), (Static, return ASTPathBindingValue(e);), (Function, return ASTPathBindingValue(e);), (EnumVar, return ASTPathBindingValue::make_EnumVar(e);), (Generic, return ASTPathBindingValue::make_Generic(e);), (Variable, return ASTPathBindingValue::make_Variable(e);))
    throw "BUG: Fell off the end of PathBinding_Value::clone";
}

::std::ostream& operator<<(::std::ostream& os, const ASTPathBindingMacro& x) {
    TU_MATCHA((x), (i), (Unbound, os << "_";), (ProcMacroDerive, os << "ProcMacroDerive(? " << i.macName << ")";), (ProcMacroAttribute, os << "ProcMacroAttribute(? " << i.macName << ")";), (ProcMacro, os << "ProcMacro(? " << i.macName << ")";), (MacroRules, os << "MacroRules(? ?)";))
    return os;
}

ASTPathBindingMacro ASTPathBindingMacro::clone() const {
    TU_MATCHA((*this), (e), (Unbound, return ASTPathBindingMacro::make_Unbound({});), (ProcMacroDerive, return ASTPathBindingMacro(e);), (ProcMacroAttribute, return ASTPathBindingMacro(e);), (ProcMacro, return ASTPathBindingMacro(e);), (MacroRules, return ASTPathBindingMacro(e);))
    throw "BUG: Fell off the end of PathBinding_Macro::clone";
}

::std::ostream& operator<<(::std::ostream& os, const ASTPathParams& x) {
    if (x.isParen) {
        auto& t = x.entries.at(0).as_Type();
        os << t; // Should be a tuple
        auto& rv = x.entries.at(1).as_AssociatedTyEqual();
        os << "->";
        os << rv.second;
        return os;
    }
    bool needsComma = false;
    os << (x.isParen ? "(" : "<");
    for (const auto& e : x.entries) {
        if (e.is_Null()) {
            continue;
        }
        if (needsComma) {
            os << ", ";
        }
        needsComma = true;

        e.fmt(os);
    }
    os << (x.isParen ? ")" : ">");
    return os;
}

ASTPathParams::ASTPathParams() = default;
ASTPathParams::~ASTPathParams() = default;
ASTPathParams::ASTPathParams(ASTPathParams&&) = default;
ASTPathParams& ASTPathParams::operator=(ASTPathParams&&) = default;

ASTPathParams::ASTPathParams(const ASTPathParams& x)
    : isParen(x.isParen)
{
    entries.reserve(x.entries.size());
    for (const auto& e : x.entries) {
        entries.push_back(e.clone());
    }
}

Ordering ASTPathParams::ord(const ASTPathParams& x) const {
    return ::ord(entries, x.entries);
}

ASTPathParamEnt ASTPathParamEnt::clone() const {
    TU_MATCH_HDRA( (*this), {)
    TU_ARMA(Null, v) {
            return v;
        }
        TU_ARMA(Lifetime, v) {
            return v;
        }
        TU_ARMA(Type, v) {
            return v.clone();
        }
        TU_ARMA(Value, v) {
            return v->clone();
        }
        TU_ARMA(AssociatedTyEqual, v) {
            return ::std::make_pair(v.first, v.second.clone());
        }
        TU_ARMA(AssociatedTyBound, v) {
            return ::std::make_pair(v.first, v.second);
        }
    }
    throw "";
}

Ordering ASTPathParamEnt::ord(const ASTPathParamEnt& x) const {
    if (this->tag() != x.tag()) {
        return ::ord(static_cast<int>(this->tag()), static_cast<int>(x.tag()));
    }

    TU_MATCH_HDRA( (*this, x), {)
    TU_ARMA(Null, v1, v2) {
            return ::OrdEqual;
        }
        TU_ARMA(Lifetime, v1, v2) {
            return ::ord(v1, v2);
        }
        TU_ARMA(Type, v1, v2) {
            return ::ord(v1, v2);
        }
        TU_ARMA(Value, v1, v2) {
            return ::ord((uintptr_t)v1.get(), (uintptr_t)v2.get());
        }
        TU_ARMA(AssociatedTyEqual, v1, v2) {
            return ::ord(v1, v2);
        }
        TU_ARMA(AssociatedTyBound, v1, v2) {
            ORD(v1.first, v2.first);
            ORD(v1.second.size(), v2.second.size());
            for (size_t i = 0; i < v1.second.size(); i++) {
                ORD(v1.second[i], v2.second[i]);
            }
            return ::OrdEqual;
        }
    }
    throw "";
}

void ASTPathParamEnt::fmt(::std::ostream& os) const {
    TU_MATCH_HDRA( (*this), {)
    TU_ARMA(Null, _) {
            os << "/*removed*/";
        }
        TU_ARMA(Lifetime, v) {
            os << v;
        }
        TU_ARMA(Type, v) {
            os << v;
        }
        TU_ARMA(Value, v) {
            v->print(os);
        }
        TU_ARMA(AssociatedTyEqual, v) {
            os << v.first << "=" << v.second;
        }
        TU_ARMA(AssociatedTyBound, v) {
            os << v.first << ": " << v.second;
        }
    }
}

// --- AST::PathNode
ASTPathNode::ASTPathNode(RcString name, ASTPathParams args)
    : mName(mv$(name))
    , mParams(mv$(args))
{
}

Ordering ASTPathNode::ord(const ASTPathNode& x) const {
    Ordering rv;
    rv = ::ord(mName, x.mName);
    if (rv != OrdEqual) {
        return rv;
    }
    rv = mParams.ord(x.mParams);
    if (rv != OrdEqual) {
        return rv;
    }
    return OrdEqual;
}

void ASTPathNode::printPretty(::std::ostream& os, bool isTypeContext) const {
    os << mName;
    if (!mParams.isEmpty()) {
        if (!isTypeContext) {
            os << "::";
        }
        os << mParams;
    }
}

::std::ostream& operator<<(::std::ostream& os, const ASTPathNode& pn) {
    pn.printPretty(os, false);
    return os;
}

/// Return an iterator to the named item
template <typename T>
typename ::std::vector<ASTNamed<T>>::const_iterator findNamed(const ::std::vector<ASTNamed<T>>& vec, const ::std::string& name) {
    return ::std::find_if(vec.begin(), vec.end(), [&name](const ASTNamed<T>& x) {
        return x.name == name;
    });
}

// --- AST::Path
ASTPath::~ASTPath() {
}

ASTPath ASTPath::newUfcsTy(TypeRef type, ::std::vector<ASTPathNode> nodes) {
    return ASTPath(ASTPath::Class::make_UFCS({box$(type), nullptr, nodes}));
}

ASTPath ASTPath::newUfcsTrait(TypeRef type, ASTPath trait, ::std::vector<ASTPathNode> nodes) {
    return ASTPath(ASTPath::Class::make_UFCS({box$(type), box$(trait), nodes}));
}

ASTPath::ASTPath(const ASTPath& x)
    : cls()
    , mBindings(x.mBindings.clone())
{
    TU_MATCH(Class, (x.cls), (ent), (Invalid, cls = Class::make_Invalid({});), (Local, cls = Class::make_Local({ent.name});), (Relative, cls = Class::make_Relative({ent.hygiene, ent.nodes});), (Self, cls = Class::make_Self({ent.nodes});), (Super, cls = Class::make_Super({ent.count, ent.nodes});), (Absolute, cls = Class::make_Absolute({ent.crate, ent.nodes});), (UFCS, if (ent.trait) cls = Class::make_UFCS({box$(ent.type->clone()), ::std::unique_ptr<ASTPath>(new ASTPath(*ent.trait)), ent.nodes}); else cls = Class::make_UFCS({box$(ent.type->clone()), nullptr, ent.nodes});))
}

bool ASTPath::isParentOf(const ASTPath& x) const {
    if (!this->cls.is_Absolute() || !x.cls.is_Absolute()) {
        return false;
    }
    const auto& te = this->cls.as_Absolute();
    const auto& xe = x.cls.as_Absolute();

    if (te.crate != xe.crate) {
        return false;
    }

    if (te.nodes.size() > xe.nodes.size()) {
        return false;
    }

    for (size_t i = 0; i < te.nodes.size(); i++) {
        if (te.nodes[i].name() != xe.nodes[i].name()) {
            return false;
        }
    }

    return true;
}

void ASTPath::bindVariable(unsigned int slot) {
    mBindings.value.set(ASTAbsolutePath(), ASTPathBindingValue::make_Variable({slot}));
}

ASTPath& ASTPath::operator+=(const ASTPath& other) {
    for (auto& node : other.nodes()) {
        append(node);
    }
    // If the path is modified, clear the binding
    mBindings = Bindings();
    return *this;
}

Ordering ASTPath::ord(const ASTPath& x) const {
    Ordering rv;

    rv = ::ord((unsigned)cls.tag(), (unsigned)x.cls.tag());
    if (rv != OrdEqual) {
        return rv;
    }

    TU_MATCH(ASTPath::Class, (cls, x.cls), (ent, xEnt), (Invalid, return OrdEqual;), (Local, return ::ord(ent.name, xEnt.name);), (Relative, return ::ord(ent.nodes, xEnt.nodes);), (Self, return ::ord(ent.nodes, xEnt.nodes);), (Super, return ::ord(ent.nodes, xEnt.nodes);), (Absolute, rv = ::ord(ent.crate, xEnt.crate); if (rv != OrdEqual) return rv; return ::ord(ent.nodes, xEnt.nodes);), (UFCS, rv = ent.type->ord(*xEnt.type); if (rv != OrdEqual) return rv; rv = ent.trait->ord(*xEnt.trait); if (rv != OrdEqual) return rv; return ::ord(ent.nodes, xEnt.nodes);))

    return OrdEqual;
}

void ASTPath::printPretty(::std::ostream& os, bool isTypeContext, bool isDebug) const {
    TU_MATCH_HDRA( (cls), {)
    TU_ARMA(Invalid, ent) {
            os << "/*inv*/";
            // NOTE: Don't print the binding for invalid paths
            return;
        }
        TU_ARMA(Local, ent) {
            // Only print comment if there's no binding
            if (mBindings.value.is_Unbound() && mBindings.type.is_Unbound()) {
                if (isDebug) {
                    os << "/*var*/";
                }
            } else {
                assert(mBindings.value.binding.is_Variable() || mBindings.value.binding.is_Generic() || mBindings.type.binding.is_TypeParameter());
            }
            os << ent.name;
        }
        TU_ARMA(Relative, ent) {
            if (isDebug) {
                os << ent.hygiene;
            }
            for (const auto& n : ent.nodes) {
                if (&n != &ent.nodes[0]) {
                    os << "::";
                }
                n.printPretty(os, isTypeContext);
            }
        }
        TU_ARMA(Self, ent) {
            os << "self";
            for (const auto& n : ent.nodes) {
                os << "::";
                n.printPretty(os, isTypeContext);
            }
        }
        TU_ARMA(Super, ent) {
            os << "super";
            for (const auto& n : ent.nodes) {
                os << "::";
                n.printPretty(os, isTypeContext);
            }
        }
        TU_ARMA(Absolute, ent) {
            const char* cn = ent.crate.c_str();
            if (!cn[0]) {
                os << "crate";
            } else if (cn[0] == '=') {
                os << "::" << cn + 1;
            } else {
                os << "::\"" << cn << "\"";
            }
            for (const auto& n : ent.nodes) {
                os << "::";
                n.printPretty(os, isTypeContext);
            }
        }
        TU_ARMA(UFCS, ent) {
            //os << "/*ufcs*/";
            if (ent.trait) {
                os << "<" << *ent.type << " as ";
                if (ent.trait->cls.is_Invalid()) {
                    os << "_";
                } else {
                    os << *ent.trait;
                }
                os << ">";
            } else {
                os << "<" << *ent.type << ">";
            }
            for (const auto& n : ent.nodes) {
                os << "::";
                n.printPretty(os, isTypeContext);
            }
        }
    }
    if( isDebug ) {
        os << "/*";
        bool printed = false;
        if (!mBindings.value.is_Unbound()) {
            if (printed) {
                os << ",";
            }
            os << "v:" << mBindings.value;
            printed = true;
        }
        if (!mBindings.type.is_Unbound()) {
            if (printed) {
                os << ",";
            }
            os << "t:" << mBindings.type;
            printed = true;
        }
        if (!mBindings.macro.is_Unbound()) {
            if (printed) {
                os << ",";
            }
            os << "m:" << mBindings.macro;
            printed = true;
        }
        if (!printed) {
            os << "?";
        }
        os << "*/";
    }
}

::std::ostream& operator<<(::std::ostream& os, const ASTPath& path) {
    path.printPretty(os, false, true);
    return os;
}

ASTAbsolutePath::ASTAbsolutePath() {
}

ASTAbsolutePath::ASTAbsolutePath(RcString crate, ::std::vector<RcString> nodes)
    : crate(::std::move(crate))
    , nodes(::std::move(nodes))
{
}

ASTAbsolutePath ASTAbsolutePath::operator+(RcString n) const {
    // Maybe being overly efficient here, but meh.
    ASTAbsolutePath rv;
    rv.crate = this->crate;
    rv.nodes.reserve(this->nodes.size() + 1);
    rv.nodes.insert(rv.nodes.end(), this->nodes.begin(), this->nodes.end());
    rv.nodes.push_back(::std::move(n));
    return rv;
}

bool ASTAbsolutePath::operator==(const ASTAbsolutePath& x) const {
    if (this->crate != x.crate) {
        return false;
    }
    if (this->nodes != x.nodes) {
        return false;
    }
    return true;
}

// Returns true if this path is a prefix of the other path (or equal)
bool ASTAbsolutePath::isParentOf(const ASTAbsolutePath& other) const {
    if (this->crate != other.crate) {
        return false;
    }
    if (this->nodes.size() > other.nodes.size()) {
        return false;
    }
    for (size_t i = 0; i < this->nodes.size(); i++) {
        if (this->nodes[i] != other.nodes[i]) {
            return false;
        }
    }
    return true;
}

ASTPathNode::ASTPathNode() {
}

void ASTPath::Bindings::mergeFrom(const Bindings& x) {
    if (value.is_Unbound()) {
        value = x.value.clone();
    }
    if (type.is_Unbound()) {
        type = x.type.clone();
    }
    if (macro.is_Unbound()) {
        macro = x.macro.clone();
    }
}

ASTPath::ASTPath(Class c)
    : cls(::std::move(c))
{
}

// INVALID
ASTPath::ASTPath()
    : cls()
{
}

// ABSOLUTE
ASTPath::ASTPath(RcString crate, ::std::vector<ASTPathNode> nodes)
    : cls(Class::make_Absolute({mv$(crate), mv$(nodes)}))
{
}

ASTPath::ASTPath(const ASTAbsolutePath& p)
    : cls(Class::make_Absolute({p.crate, {}}))
{
    auto& n = cls.as_Absolute().nodes;
    n.reserve(p.nodes.size());
    for (const auto& v : p.nodes) {
        n.push_back(v);
    }
}

ASTPath::ASTPath(const ASTPathBinding<ASTPathBindingValue>& pb)
    : ASTPath(pb.path)
{
    this->mBindings.value = pb.clone();
}

ASTPath::ASTPath(const ASTPathBinding<ASTPathBindingType>& pb)
    : ASTPath(pb.path)
{
    this->mBindings.type = pb.clone();
}

ASTPath::ASTPath(const ASTPathBinding<ASTPathBindingMacro>& pb)
    : ASTPath(pb.path)
{
    this->mBindings.macro = pb.clone();
}

ASTPath::ASTPath(const ASTAbsolutePath& p, ASTPathParams pp)
    : ASTPath(p)
{
    auto& n = cls.as_Absolute().nodes;
    assert(n.size() > 0);
    n.back().args() = ::std::move(pp);
}

ASTPath ASTPath::operator+(ASTPathNode pn) const {
    ASTPath tmp = ASTPath(*this);
    tmp.append(mv$(pn));
    return tmp;
}

ASTPath ASTPath::operator+(const RcString& s) const {
    ASTPath tmp = ASTPath(*this);
    tmp.append(ASTPathNode(s, {}));
    return tmp;
}

ASTPath& ASTPath::operator+=(ASTPathNode pn) {
    this->append(mv$(pn));
    return *this;
}

const RcString& ASTPath::asTrivial() const {
TU_MATCH_HDRA( (cls), {)
default:
    break;
        TU_ARMA(Local, e) {
            return e.name;
        }
        TU_ARMA(Relative, e) {
            return e.nodes[0].name();
        }
}
throw std::runtime_error("as_trivial on non-trivial path");
}

size_t ASTPath::size() const {
    TU_MATCH(Class, (cls), (ent), (Invalid, assert(!cls.is_Invalid()); throw ::std::runtime_error("Path::nodes() on Invalid");), (Local, return 1;), (Relative, return ent.nodes.size();), (Self, return ent.nodes.size();), (Super, return ent.nodes.size();), (Absolute, return ent.nodes.size();), (UFCS, return ent.nodes.size();))
    throw ::std::runtime_error("Path::nodes() fell off");
}

::std::vector<ASTPathNode>& ASTPath::nodes() {
    TU_MATCH(Class, (cls), (ent), (Invalid, assert(!cls.is_Invalid()); throw ::std::runtime_error("Path::nodes() on Invalid");), (Local, assert(!cls.is_Local()); throw ::std::runtime_error("Path::nodes() on Local");), (Relative, return ent.nodes;), (Self, return ent.nodes;), (Super, return ent.nodes;), (Absolute, return ent.nodes;), (UFCS, return ent.nodes;))
    throw ::std::runtime_error("Path::nodes() fell off");
}

::std::ostream& operator<<(::std::ostream& os, const ASTAbsolutePath& x) {
    if (x.crate != "") {
        os << "::\"" << x.crate << "\"";
    } else {
        os << "crate";
    }
    for (const auto& n : x.nodes) {
        os << "::" << n;
    }
    return os;
}
