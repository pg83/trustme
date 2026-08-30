#include "ast_path.h"

#include "output.h"
#include "ast_ast.h"
#include "ast_expr.h"
#include "ast_types.h"
#include "parse_parseerror.h"

#include <std/lib/vector.h>

#include <algorithm>

using namespace stl;

#define PRETTY_PATH_PRINT 1

ASTPathParams::ASTPathParams() = default;
ASTPathParams::~ASTPathParams() = default;
ASTPathParams::ASTPathParams(ASTPathParams&&) = default;
ASTPathParams& ASTPathParams::operator=(ASTPathParams&&) = default;

ASTPathParams::ASTPathParams(const ASTPathParams& x)
    : isParen(x.isParen)
    , isRtn(x.isRtn)
{
    entries.reserve(x.entries.size());
    for (const auto& e : x.entries) {
        entries.push_back(e.clone());
    }
}

Ordering ASTPathParams::ord(const ASTPathParams& x) const {
    ORD(isParen, x.isParen);
    ORD(isRtn, x.isRtn);
    return ::ord(entries, x.entries);
}

ASTPathParamEnt ASTPathParamEnt::clone() const {
    switch ((*this).tag()) {
        case ASTPathParamEnt::TAG_Null: {
            auto& v = (*this).as_Null();
            return v;
        }
        case ASTPathParamEnt::TAG_Lifetime: {
            auto& v = (*this).as_Lifetime();
            return v;
        }
        case ASTPathParamEnt::TAG_Type: {
            auto& v = (*this).as_Type();
            return v->clone();
        }
        case ASTPathParamEnt::TAG_Value: {
            auto& v = (*this).as_Value();
            return v->clone();
        }
        case ASTPathParamEnt::TAG_AssociatedTyEqual: {
            auto& v = (*this).as_AssociatedTyEqual();
            return std::make_pair(v.first, v.second->clone());
        }
        case ASTPathParamEnt::TAG_AssociatedValueEqual: {
            auto& v = (*this).as_AssociatedValueEqual();
            return std::make_pair(v.first, v.second->clone());
        }
        case ASTPathParamEnt::TAG_AssociatedTyBound: {
            auto& v = (*this).as_AssociatedTyBound();
            return std::make_pair(v.first, v.second);
        }
    }
    UNREACHABLE();
}

Ordering ASTPathParamEnt::ord(const ASTPathParamEnt& x) const {
    if (this->tag() != x.tag()) {
        return ::ord(static_cast<int>(this->tag()), static_cast<int>(x.tag()));
    }

    switch ((*this).tag()) {
        case ASTPathParamEnt::TAG_Null: {
            return ::OrdEqual;
        }
        case ASTPathParamEnt::TAG_Lifetime: {
            auto& v1 = (*this).as_Lifetime();
            auto& v2 = x.as_Lifetime();
            return ::ord(v1, v2);
        }
        case ASTPathParamEnt::TAG_Type: {
            auto& v1 = (*this).as_Type();
            auto& v2 = x.as_Type();
            return ::ord(v1, v2);
        }
        case ASTPathParamEnt::TAG_Value: {
            auto& v1 = (*this).as_Value();
            auto& v2 = x.as_Value();
            return ::ord((uintptr_t)v1.get(), (uintptr_t)v2.get());
        }
        case ASTPathParamEnt::TAG_AssociatedTyEqual: {
            auto& v1 = (*this).as_AssociatedTyEqual();
            auto& v2 = x.as_AssociatedTyEqual();
            return ::ord(v1, v2);
        }
        case ASTPathParamEnt::TAG_AssociatedValueEqual: {
            auto& v1 = (*this).as_AssociatedValueEqual();
            auto& v2 = x.as_AssociatedValueEqual();
            ORD(v1.first, v2.first);
            return OrdEqual;
        }
        case ASTPathParamEnt::TAG_AssociatedTyBound: {
            auto& v1 = (*this).as_AssociatedTyBound();
            auto& v2 = x.as_AssociatedTyBound();
            ORD(v1.first, v2.first);
            ORD(v1.second.size(), v2.second.size());
            for (size_t i = 0; i < v1.second.size(); i++) {
                ORD(v1.second[i], v2.second[i]);
            }
            return ::OrdEqual;
        }
    }
    UNREACHABLE();
}

void ASTPathParamEnt::fmt(ZeroCopyOutput& os) const {
    switch ((*this).tag()) {
        case ASTPathParamEnt::TAG_Null: {
            auto& _ = (*this).as_Null();
            os << StringView("/*removed*/");
            break;
        }
        case ASTPathParamEnt::TAG_Lifetime: {
            auto& v = (*this).as_Lifetime();
            os << v;
            break;
        }
        case ASTPathParamEnt::TAG_Type: {
            auto& v = (*this).as_Type();
            os << v;
            break;
        }
        case ASTPathParamEnt::TAG_Value: {
            auto& v = (*this).as_Value();
            v->print(os);
            break;
        }
        case ASTPathParamEnt::TAG_AssociatedTyEqual: {
            auto& v = (*this).as_AssociatedTyEqual();
            os << v.first << StringView("=") << v.second;
            break;
        }
        case ASTPathParamEnt::TAG_AssociatedValueEqual: {
            auto& v = (*this).as_AssociatedValueEqual();
            os << v.first << StringView("=") << *v.second;
            break;
        }
        case ASTPathParamEnt::TAG_AssociatedTyBound: {
            auto& v = (*this).as_AssociatedTyBound();
            os << v.first << StringView(": ");
            for (const auto& trait : v.second) {
                if (&trait != v.second.data()) {
                    os << StringView(" + ");
                }
                os << trait.hrbs << *trait.path;
            }
            break;
        }
    }
}

ASTPathNode::ASTPathNode(RcString name, ASTPathParams args)
    : ident_(mv$(name))
    , params_(mv$(args))
{
}

ASTPathNode::ASTPathNode(Ident::Hygiene hygiene, RcString name, ASTPathParams args)
    : ident_(mv$(hygiene), mv$(name))
    , params_(mv$(args))
{
}

Ordering ASTPathNode::ord(const ASTPathNode& x) const {
    Ordering rv;
    rv = ::ord(hygienicName(), x.hygienicName());
    if (rv != OrdEqual) {
        return rv;
    }
    rv = params_.ord(x.params_);
    if (rv != OrdEqual) {
        return rv;
    }
    return OrdEqual;
}

void ASTPathNode::printPretty(ZeroCopyOutput& os, bool isTypeContext) const {
    os << ident_.name;
    if (!params_.isEmpty()) {
        if (!isTypeContext) {
            os << StringView("::");
        }
        os << params_;
    }
}

namespace {
    template <typename T>
    typename std::vector<ASTNamed<T>>::const_iterator findNamed(const std::vector<ASTNamed<T>>& vec, const std::string& name) {
        return std::find_if(vec.begin(), vec.end(), [&name](const ASTNamed<T>& x) {
            return x.name == name;
        });
    }
}

ASTPath::~ASTPath() {
}

ASTPath ASTPath::newUfcsTy(ASTType* type, std::vector<ASTPathNode> nodes) {
    return ASTPath(ASTPath::Class::make_UFCS({type, nullptr, nodes}));
}

ASTPath ASTPath::newUfcsTrait(ASTType* type, ASTPath trait, std::vector<ASTPathNode> nodes) {
    return ASTPath(ASTPath::Class::make_UFCS({type, box$(trait), nodes}));
}

ASTPath::ASTPath(const ASTPath& x)
    : cls()
    , bindings(x.bindings.clone())
{
    switch (x.cls.tag()) {
        case Class::TAG_Invalid: {
            cls = Class::make_Invalid({});
            break;
        }
        case Class::TAG_Local: {
            auto& ent = x.cls.as_Local();
            cls = Class::make_Local({ent.name});
            break;
        }
        case Class::TAG_Relative: {
            auto& ent = x.cls.as_Relative();
            cls = Class::make_Relative({ent.hygiene, ent.nodes});
            break;
        }
        case Class::TAG_Self: {
            auto& ent = x.cls.as_Self();
            cls = Class::make_Self({ent.nodes});
            break;
        }
        case Class::TAG_Super: {
            auto& ent = x.cls.as_Super();
            cls = Class::make_Super({ent.count, ent.nodes});
            break;
        }
        case Class::TAG_Absolute: {
            auto& ent = x.cls.as_Absolute();
            cls = Class::make_Absolute({ent.crate, ent.nodes});
            break;
        }
        case Class::TAG_UFCS: {
            auto& ent = x.cls.as_UFCS();
            if (ent.trait) {
                cls = Class::make_UFCS({ent.type->clone(), std::unique_ptr<ASTPath>(new ASTPath(*ent.trait)), ent.nodes});
            } else {
                cls = Class::make_UFCS({ent.type->clone(), nullptr, ent.nodes});
            }
            break;
        }
    }
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
    bindings.value.set(ASTAbsolutePath(), ASTPathBindingValue::make_Variable({slot}));
}

ASTPath& ASTPath::operator+=(const ASTPath& other) {
    for (auto& node : other.nodes()) {
        append(node);
    }
    bindings = Bindings();
    return *this;
}

Ordering ASTPath::ord(const ASTPath& x) const {
    Ordering rv;

    rv = ::ord((unsigned)cls.tag(), (unsigned)x.cls.tag());
    if (rv != OrdEqual) {
        return rv;
    }

    switch (cls.tag()) {
        case ASTPath::Class::TAG_Invalid: {
            return OrdEqual;
        }
        case ASTPath::Class::TAG_Local: {
            auto& ent = cls.as_Local();
            auto& xEnt = x.cls.as_Local();
            return ::ord(ent.name, xEnt.name);
        }
        case ASTPath::Class::TAG_Relative: {
            auto& ent = cls.as_Relative();
            auto& xEnt = x.cls.as_Relative();
            return ::ord(ent.nodes, xEnt.nodes);
        }
        case ASTPath::Class::TAG_Self: {
            auto& ent = cls.as_Self();
            auto& xEnt = x.cls.as_Self();
            return ::ord(ent.nodes, xEnt.nodes);
        }
        case ASTPath::Class::TAG_Super: {
            auto& ent = cls.as_Super();
            auto& xEnt = x.cls.as_Super();
            return ::ord(ent.nodes, xEnt.nodes);
        }
        case ASTPath::Class::TAG_Absolute: {
            auto& ent = cls.as_Absolute();
            auto& xEnt = x.cls.as_Absolute();
            rv = ::ord(ent.crate, xEnt.crate);
            if (rv != OrdEqual) {
                return rv;
            }
            return ::ord(ent.nodes, xEnt.nodes);
            break;
        }
        case ASTPath::Class::TAG_UFCS: {
            auto& ent = cls.as_UFCS();
            auto& xEnt = x.cls.as_UFCS();
            rv = ent.type->ord(*xEnt.type);
            if (rv != OrdEqual) {
                return rv;
            }
            rv = ent.trait->ord(*xEnt.trait);
            if (rv != OrdEqual) {
                return rv;
            }
            return ::ord(ent.nodes, xEnt.nodes);
            break;
        }
    }

    return OrdEqual;
}

void ASTPath::printPretty(ZeroCopyOutput& os, bool isTypeContext, bool isDebug) const {
    switch (cls.tag()) {
        case ASTPathClass::TAG_Invalid: {
            os << StringView("/*inv*/");
            return;
        }
        case ASTPathClass::TAG_Local: {
            auto& ent = cls.as_Local();
            if (bindings.value.is_Unbound() && bindings.type.is_Unbound()) {
                if (isDebug) {
                    os << StringView("/*var*/");
                }
            } else {
                BUG_ASSERT(bindings.value.binding.is_Variable() || bindings.value.binding.is_Generic() || bindings.type.binding.is_TypeParameter());
            }
            os << ent.name;
            break;
        }
        case ASTPathClass::TAG_Relative: {
            auto& ent = cls.as_Relative();
            if (isDebug) {
                os << ent.hygiene;
            }
            for (const auto& n : ent.nodes) {
                if (&n != &ent.nodes[0]) {
                    os << StringView("::");
                }
                n.printPretty(os, isTypeContext);
            }
            break;
        }
        case ASTPathClass::TAG_Self: {
            auto& ent = cls.as_Self();
            os << StringView("self");
            for (const auto& n : ent.nodes) {
                os << StringView("::");
                n.printPretty(os, isTypeContext);
            }
            break;
        }
        case ASTPathClass::TAG_Super: {
            auto& ent = cls.as_Super();
            os << StringView("super");
            for (const auto& n : ent.nodes) {
                os << StringView("::");
                n.printPretty(os, isTypeContext);
            }
            break;
        }
        case ASTPathClass::TAG_Absolute: {
            auto& ent = cls.as_Absolute();
            const char* cn = ent.crate.c_str();
            if (!cn[0]) {
                os << StringView("crate");
            } else if (cn[0] == '=') {
                os << StringView("::") << cn + 1;
            } else {
                os << StringView("::\"") << cn << StringView("\"");
            }
            for (const auto& n : ent.nodes) {
                os << StringView("::");
                n.printPretty(os, isTypeContext);
            }
            break;
        }
        case ASTPathClass::TAG_UFCS: {
            auto& ent = cls.as_UFCS();
            if (ent.trait) {
                os << StringView("<") << *ent.type << StringView(" as ");
                if (ent.trait->cls.is_Invalid()) {
                    os << StringView("_");
                } else {
                    os << *ent.trait;
                }
                os << StringView(">");
            } else {
                os << StringView("<") << *ent.type << StringView(">");
            }
            for (const auto& n : ent.nodes) {
                os << StringView("::");
                n.printPretty(os, isTypeContext);
            }
            break;
        }
    }
    if (isDebug) {
        os << StringView("/*");
        bool printed = false;
        if (!bindings.value.is_Unbound()) {
            if (printed) {
                os << StringView(",");
            }
            os << StringView("v:") << bindings.value;
            printed = true;
        }
        if (!bindings.type.is_Unbound()) {
            if (printed) {
                os << StringView(",");
            }
            os << StringView("t:") << bindings.type;
            printed = true;
        }
        if (!bindings.macro.is_Unbound()) {
            if (printed) {
                os << StringView(",");
            }
            os << StringView("m:") << bindings.macro;
            printed = true;
        }
        if (!printed) {
            os << StringView("?");
        }
        os << StringView("*/");
    }
}

ASTAbsolutePath::ASTAbsolutePath() {
}

ASTAbsolutePath::ASTAbsolutePath(RcString crate, Vector<RcString> nodes)
    : crate(std::move(crate))
    , nodes(std::move(nodes))
{
}

ASTAbsolutePath ASTAbsolutePath::operator+(RcString n) const {
    ASTAbsolutePath rv;
    rv.crate = this->crate;
    rv.nodes.grow(this->nodes.length() + 1);
    rv.nodes.append(this->nodes.begin(), this->nodes.end());
    rv.nodes.pushBack(std::move(n));
    return rv;
}

bool ASTAbsolutePath::operator==(const ASTAbsolutePath& x) const {
    if (this->crate != x.crate) {
        return false;
    }
    if (::ord(this->nodes, x.nodes) != OrdEqual) {
        return false;
    }
    return true;
}

bool ASTAbsolutePath::isParentOf(const ASTAbsolutePath& other) const {
    if (this->crate != other.crate) {
        return false;
    }
    if (this->nodes.length() > other.nodes.length()) {
        return false;
    }
    for (size_t i = 0; i < this->nodes.length(); i++) {
        if (this->nodes[i] != other.nodes[i]) {
            return false;
        }
    }
    return true;
}

ASTPathNode::ASTPathNode()
    : ident_(RcString())
{
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
    : cls(std::move(c))
{
}

ASTPath::ASTPath()
    : cls()
{
}

ASTPath::ASTPath(RcString crate, std::vector<ASTPathNode> nodes)
    : cls(Class::make_Absolute({mv$(crate), mv$(nodes)}))
{
}

ASTPath::ASTPath(const ASTAbsolutePath& p)
    : cls(Class::make_Absolute({p.crate, {}}))
{
    auto& n = cls.as_Absolute().nodes;
    n.reserve(p.nodes.length());
    for (const auto& v : p.nodes) {
        n.push_back(v);
    }
}

ASTPath::ASTPath(const ASTPathBinding<ASTPathBindingValue>& pb)
    : ASTPath(pb.path)
{
    this->bindings.value = pb.clone();
}

ASTPath::ASTPath(const ASTPathBinding<ASTPathBindingType>& pb)
    : ASTPath(pb.path)
{
    this->bindings.type = pb.clone();
}

ASTPath::ASTPath(const ASTPathBinding<ASTPathBindingMacro>& pb)
    : ASTPath(pb.path)
{
    this->bindings.macro = pb.clone();
}

ASTPath::ASTPath(const ASTAbsolutePath& p, ASTPathParams pp)
    : ASTPath(p)
{
    auto& n = cls.as_Absolute().nodes;
    BUG_ASSERT(n.size() > 0);
    n.back().args() = std::move(pp);
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
    switch (cls.tag()) {
        default:
            break;
        case ASTPathClass::TAG_Local: {
            auto& e = cls.as_Local();
            return e.name;
        }
        case ASTPathClass::TAG_Relative: {
            auto& e = cls.as_Relative();
            return e.nodes[0].name();
        }
    }
    BUG(Span(), StringView("as_trivial on non-trivial path"));
}

size_t ASTPath::size() const {
    switch (cls.tag()) {
        case Class::TAG_Invalid: {
            BUG(Span(), StringView("Path::size() on Invalid"));
        }
        case Class::TAG_Local: {
            return 1;
        }
        case Class::TAG_Relative: {
            auto& ent = cls.as_Relative();
            return ent.nodes.size();
        }
        case Class::TAG_Self: {
            auto& ent = cls.as_Self();
            return ent.nodes.size();
        }
        case Class::TAG_Super: {
            auto& ent = cls.as_Super();
            return ent.nodes.size();
        }
        case Class::TAG_Absolute: {
            auto& ent = cls.as_Absolute();
            return ent.nodes.size();
        }
        case Class::TAG_UFCS: {
            auto& ent = cls.as_UFCS();
            return ent.nodes.size();
        }
    }
    BUG(Span(), StringView("Path::size() fell off"));
}

std::vector<ASTPathNode>& ASTPath::nodes() {
    switch (cls.tag()) {
        case Class::TAG_Invalid: {
            BUG(Span(), StringView("Path::nodes() on Invalid"));
        }
        case Class::TAG_Local: {
            BUG(Span(), StringView("Path::nodes() on Local"));
        }
        case Class::TAG_Relative: {
            auto& ent = cls.as_Relative();
            return ent.nodes;
        }
        case Class::TAG_Self: {
            auto& ent = cls.as_Self();
            return ent.nodes;
        }
        case Class::TAG_Super: {
            auto& ent = cls.as_Super();
            return ent.nodes;
        }
        case Class::TAG_Absolute: {
            auto& ent = cls.as_Absolute();
            return ent.nodes;
        }
        case Class::TAG_UFCS: {
            auto& ent = cls.as_UFCS();
            return ent.nodes;
        }
    }
    BUG(Span(), StringView("Path::nodes() fell off"));
}

void ASTPath::append(ASTPathNode node) {
    BUG_ASSERT(!cls.is_Invalid());
    nodes().push_back(mv$(node));
    bindings = Bindings();
}

template <>
void stl::output<ZeroCopyOutput, ASTPathBindingModuleHir>(ZeroCopyOutput& out, ASTPathBindingModuleHir value) {
    out << StringView("ASTPathBindingModuleHir(crate = ") << static_cast<const void*>(value.crate) << StringView(", mod = ") << static_cast<const void*>(value.mod) << StringView(")");
}

template <>
void stl::output<ZeroCopyOutput, ASTPathBinding<ASTPathBindingValue>>(ZeroCopyOutput& out, const ASTPathBinding<ASTPathBindingValue>& value) {
    if (value.is_Unbound()) {
        out << StringView("Unbound");
    } else {
        out << value.binding << StringView("[") << value.path << StringView("]");
    }
}

template <>
void stl::output<ZeroCopyOutput, ASTPathBinding<ASTPathBindingType>>(ZeroCopyOutput& out, const ASTPathBinding<ASTPathBindingType>& value) {
    if (value.is_Unbound()) {
        out << StringView("Unbound");
    } else {
        out << value.binding << StringView("[") << value.path << StringView("]");
    }
}

template <>
void stl::output<ZeroCopyOutput, ASTPathBinding<ASTPathBindingMacro>>(ZeroCopyOutput& out, const ASTPathBinding<ASTPathBindingMacro>& value) {
    if (value.is_Unbound()) {
        out << StringView("Unbound");
    } else {
        out << value.binding << StringView("[") << value.path << StringView("]");
    }
}

template <>
void stl::output<ZeroCopyOutput, ASTPathParams>(ZeroCopyOutput& os, const ASTPathParams& x) {
    if (x.isRtn) {
        os << StringView("(..)");
        return;
    }
    if (x.isParen) {
        auto& t = x.entries.at(0).as_Type();
        os << t;
        auto& rv = x.entries.at(1).as_AssociatedTyEqual();
        os << StringView("->");
        os << rv.second;
        return;
    }
    bool needsComma = false;
    os << StringView(x.isParen ? "(" : "<");
    for (const auto& e : x.entries) {
        if (e.is_Null()) {
            continue;
        }
        if (needsComma) {
            os << StringView(", ");
        }
        needsComma = true;

        e.fmt(os);
    }
    os << StringView(x.isParen ? ")" : ">");
    return;
}

template <>
void stl::output<ZeroCopyOutput, ASTPathNode>(ZeroCopyOutput& os, const ASTPathNode& pn) {
    pn.printPretty(os, false);
    return;
}

template <>
void stl::output<ZeroCopyOutput, ASTPath>(ZeroCopyOutput& os, const ASTPath& path) {
    path.printPretty(os, false, true);
    return;
}

template <>
void stl::output<ZeroCopyOutput, ASTAbsolutePath>(ZeroCopyOutput& os, const ASTAbsolutePath& x) {
    if (x.crate != "") {
        os << StringView("::\"") << x.crate << StringView("\"");
    } else {
        os << StringView("crate");
    }
    for (const auto& n : x.nodes) {
        os << StringView("::") << n;
    }
    return;
}

template <>
void stl::output<ZeroCopyOutput, std::vector<ASTPathNode>>(ZeroCopyOutput& out, const std::vector<ASTPathNode>& values) {
    outCont(out, values);
}

template <>
void stl::output<ZeroCopyOutput, std::vector<ASTPath>>(ZeroCopyOutput& out, const std::vector<ASTPath>& values) {
    outCont(out, values);
}
