#include "ast_path.h"
#include "ast_ast.h"
#include "ast_types.h"
#include <iostream>
#include "parse_parseerror.h"
#include <algorithm>
#include "ast_expr.h"

#define PRETTY_PATH_PRINT 1

namespace AST {

    // --- AST::PathBinding
    ::std::ostream& operator<<(::std::ostream& os, const PathBindingType& x) {
        TU_MATCHA((x), (i), (Unbound, os << "_";), (Crate, os << "Crate";), (Primitive, os << "Primitive";), (Module, os << "Module";), (Trait, os << "Trait";), (TraitAlias, os << "TraitAlias";), (Struct, os << "Struct";), (Enum, os << "Enum";), (Union, os << "Union";), (EnumVar, os << "EnumVar(" << i.idx << ")";), (TypeAlias, os << "TypeAlias";), (TypeParameter, os << "TyParam(" << i.slot << ")";))
        return os;
    }

    PathBindingType PathBindingType::clone() const {
        TU_MATCHA(
            (*this),
            (e),
            (Unbound, return PathBindingType::make_Unbound({});),
            (Primitive, return e;),
            (Module, return PathBindingType::make_Module(e);),
            (Crate, return PathBindingType(e);),
            (Trait, return PathBindingType(e);),
            (TraitAlias, return PathBindingType(e);),
            (Struct, return PathBindingType(e);),
            (Enum, return PathBindingType(e);),
            (Union, return PathBindingType(e);),
            (TypeAlias, return PathBindingType::make_TypeAlias(e);),
            (EnumVar, return PathBindingType::make_EnumVar(e);),

            (TypeParameter, return PathBindingType::make_TypeParameter(e);)
        )
        throw "BUG: Fell off the end of PathBinding_Type::clone";
    }

    ::std::ostream& operator<<(::std::ostream& os, const PathBindingValue& x) {
        TU_MATCHA((x), (i), (Unbound, os << "_";), (Struct, os << "Struct";), (Static, os << "Static";), (Function, os << "Function";), (EnumVar, os << "EnumVar(" << i.idx << ")";), (Generic, os << "Param(" << i.index << ")";), (Variable, os << "Var(" << i.slot << ")";))
        return os;
    }

    PathBindingValue PathBindingValue::clone() const {
        TU_MATCHA((*this), (e), (Unbound, return PathBindingValue::make_Unbound({});), (Struct, return PathBindingValue(e);), (Static, return PathBindingValue(e);), (Function, return PathBindingValue(e);), (EnumVar, return PathBindingValue::make_EnumVar(e);), (Generic, return PathBindingValue::make_Generic(e);), (Variable, return PathBindingValue::make_Variable(e);))
        throw "BUG: Fell off the end of PathBinding_Value::clone";
    }

    ::std::ostream& operator<<(::std::ostream& os, const PathBindingMacro& x) {
        TU_MATCHA((x), (i), (Unbound, os << "_";), (ProcMacroDerive, os << "ProcMacroDerive(? " << i.mac_name << ")";), (ProcMacroAttribute, os << "ProcMacroAttribute(? " << i.mac_name << ")";), (ProcMacro, os << "ProcMacro(? " << i.mac_name << ")";), (MacroRules, os << "MacroRules(? ?)";))
        return os;
    }

    PathBindingMacro PathBindingMacro::clone() const {
        TU_MATCHA((*this), (e), (Unbound, return PathBindingMacro::make_Unbound({});), (ProcMacroDerive, return PathBindingMacro(e);), (ProcMacroAttribute, return PathBindingMacro(e);), (ProcMacro, return PathBindingMacro(e);), (MacroRules, return PathBindingMacro(e);))
        throw "BUG: Fell off the end of PathBinding_Macro::clone";
    }

    ::std::ostream& operator<<(::std::ostream& os, const PathParams& x) {
        if (x.m_is_paren) {
            auto& t = x.m_entries.at(0).as_Type();
            os << t; // Should be a tuple
            auto& rv = x.m_entries.at(1).as_AssociatedTyEqual();
            os << "->";
            os << rv.second;
            return os;
        }
        bool needs_comma = false;
        os << (x.m_is_paren ? "(" : "<");
        for (const auto& e : x.m_entries) {
            if (e.is_Null()) {
                continue;
            }
            if (needs_comma) {
                os << ", ";
            }
            needs_comma = true;

            e.fmt(os);
        }
        os << (x.m_is_paren ? ")" : ">");
        return os;
    }

    PathParams::PathParams() = default;
    PathParams::~PathParams() = default;
    PathParams::PathParams(PathParams&&) = default;
    PathParams& PathParams::operator=(PathParams&&) = default;

    PathParams::PathParams(const PathParams& x)
        : m_is_paren(x.m_is_paren)
    {
        m_entries.reserve(x.m_entries.size());
        for (const auto& e : x.m_entries) {
            m_entries.push_back(e.clone());
        }
    }

    Ordering PathParams::ord(const PathParams& x) const {
        return ::ord(m_entries, x.m_entries);
    }

    PathParamEnt PathParamEnt::clone() const {
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

    Ordering PathParamEnt::ord(const PathParamEnt& x) const {
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

    void PathParamEnt::fmt(::std::ostream& os) const {
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
    PathNode::PathNode(RcString name, PathParams args)
        : m_name(mv$(name))
        , m_params(mv$(args))
    {
    }

    Ordering PathNode::ord(const PathNode& x) const {
        Ordering rv;
        rv = ::ord(m_name, x.m_name);
        if (rv != OrdEqual) {
            return rv;
        }
        rv = m_params.ord(x.m_params);
        if (rv != OrdEqual) {
            return rv;
        }
        return OrdEqual;
    }

    void PathNode::print_pretty(::std::ostream& os, bool is_type_context) const {
        os << m_name;
        if (!m_params.is_empty()) {
            if (!is_type_context) {
                os << "::";
            }
            os << m_params;
        }
    }

    ::std::ostream& operator<<(::std::ostream& os, const PathNode& pn) {
        pn.print_pretty(os, false);
        return os;
    }

    /// Return an iterator to the named item
    template <typename T>
    typename ::std::vector<Named<T>>::const_iterator find_named(const ::std::vector<Named<T>>& vec, const ::std::string& name) {
        return ::std::find_if(vec.begin(), vec.end(), [&name](const Named<T>& x) {
            return x.name == name;
        });
    }

    // --- AST::Path
    AST::Path::~Path() {
    }

    AST::Path AST::Path::new_ufcs_ty(TypeRef type, ::std::vector<AST::PathNode> nodes) {
        return AST::Path(AST::Path::Class::make_UFCS({box$(type), nullptr, nodes}));
    }

    AST::Path AST::Path::new_ufcs_trait(TypeRef type, Path trait, ::std::vector<AST::PathNode> nodes) {
        return AST::Path(AST::Path::Class::make_UFCS({box$(type), box$(trait), nodes}));
    }

    AST::Path::Path(const Path& x)
        : m_class()
        , m_bindings(x.m_bindings.clone())
    {
        TU_MATCH(Class, (x.m_class), (ent), (Invalid, m_class = Class::make_Invalid({});), (Local, m_class = Class::make_Local({ent.name});), (Relative, m_class = Class::make_Relative({ent.hygiene, ent.nodes});), (Self, m_class = Class::make_Self({ent.nodes});), (Super, m_class = Class::make_Super({ent.count, ent.nodes});), (Absolute, m_class = Class::make_Absolute({ent.crate, ent.nodes});), (UFCS, if (ent.trait) m_class = Class::make_UFCS({box$(ent.type->clone()), ::std::unique_ptr<Path>(new Path(*ent.trait)), ent.nodes}); else m_class = Class::make_UFCS({box$(ent.type->clone()), nullptr, ent.nodes});))
    }

    bool Path::is_parent_of(const Path& x) const {
        if (!this->m_class.is_Absolute() || !x.m_class.is_Absolute()) {
            return false;
        }
        const auto& te = this->m_class.as_Absolute();
        const auto& xe = x.m_class.as_Absolute();

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

    void Path::bind_variable(unsigned int slot) {
        m_bindings.value.set(AST::AbsolutePath(), PathBindingValue::make_Variable({slot}));
    }

    Path& Path::operator+=(const Path& other) {
        for (auto& node : other.nodes()) {
            append(node);
        }
        // If the path is modified, clear the binding
        m_bindings = Bindings();
        return *this;
    }

    Ordering Path::ord(const Path& x) const {
        Ordering rv;

        rv = ::ord((unsigned)m_class.tag(), (unsigned)x.m_class.tag());
        if (rv != OrdEqual) {
            return rv;
        }

        TU_MATCH(Path::Class, (m_class, x.m_class), (ent, x_ent), (Invalid, return OrdEqual;), (Local, return ::ord(ent.name, x_ent.name);), (Relative, return ::ord(ent.nodes, x_ent.nodes);), (Self, return ::ord(ent.nodes, x_ent.nodes);), (Super, return ::ord(ent.nodes, x_ent.nodes);), (Absolute, rv = ::ord(ent.crate, x_ent.crate); if (rv != OrdEqual) return rv; return ::ord(ent.nodes, x_ent.nodes);), (UFCS, rv = ent.type->ord(*x_ent.type); if (rv != OrdEqual) return rv; rv = ent.trait->ord(*x_ent.trait); if (rv != OrdEqual) return rv; return ::ord(ent.nodes, x_ent.nodes);))

        return OrdEqual;
    }

    void Path::print_pretty(::std::ostream& os, bool is_type_context, bool is_debug) const {
    TU_MATCH_HDRA( (m_class), {)
    TU_ARMA(Invalid, ent) {
                os << "/*inv*/";
                // NOTE: Don't print the binding for invalid paths
                return;
            }
            TU_ARMA(Local, ent) {
                // Only print comment if there's no binding
                if (m_bindings.value.is_Unbound() && m_bindings.type.is_Unbound()) {
                    if (is_debug) {
                        os << "/*var*/";
                    }
                } else {
                    assert(m_bindings.value.binding.is_Variable() || m_bindings.value.binding.is_Generic() || m_bindings.type.binding.is_TypeParameter());
                }
                os << ent.name;
            }
            TU_ARMA(Relative, ent) {
                if (is_debug) {
                    os << ent.hygiene;
                }
                for (const auto& n : ent.nodes) {
                    if (&n != &ent.nodes[0]) {
                        os << "::";
                    }
                    n.print_pretty(os, is_type_context);
                }
            }
            TU_ARMA(Self, ent) {
                os << "self";
                for (const auto& n : ent.nodes) {
                    os << "::";
                    n.print_pretty(os, is_type_context);
                }
            }
            TU_ARMA(Super, ent) {
                os << "super";
                for (const auto& n : ent.nodes) {
                    os << "::";
                    n.print_pretty(os, is_type_context);
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
                    n.print_pretty(os, is_type_context);
                }
            }
            TU_ARMA(UFCS, ent) {
                //os << "/*ufcs*/";
                if (ent.trait) {
                    os << "<" << *ent.type << " as ";
                    if (ent.trait->m_class.is_Invalid()) {
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
                    n.print_pretty(os, is_type_context);
                }
            }
    }
    if( is_debug ) {
            os << "/*";
            bool printed = false;
            if (!m_bindings.value.is_Unbound()) {
                if (printed) {
                    os << ",";
                }
                os << "v:" << m_bindings.value;
                printed = true;
            }
            if (!m_bindings.type.is_Unbound()) {
                if (printed) {
                    os << ",";
                }
                os << "t:" << m_bindings.type;
                printed = true;
            }
            if (!m_bindings.macro.is_Unbound()) {
                if (printed) {
                    os << ",";
                }
                os << "m:" << m_bindings.macro;
                printed = true;
            }
            if (!printed) {
                os << "?";
            }
            os << "*/";
    }
    }

    ::std::ostream& operator<<(::std::ostream& os, const Path& path) {
        path.print_pretty(os, false, true);
        return os;
    }

}

namespace AST {

AbsolutePath::AbsolutePath() {
}
AbsolutePath::AbsolutePath(RcString crate, ::std::vector<RcString> nodes)
    : crate(::std::move(crate))
    , nodes(::std::move(nodes)) {
}
AbsolutePath AbsolutePath::operator+(RcString n) const {
    // Maybe being overly efficient here, but meh.
    AbsolutePath rv;
    rv.crate = this->crate;
    rv.nodes.reserve(this->nodes.size() + 1);
    rv.nodes.insert(rv.nodes.end(), this->nodes.begin(), this->nodes.end());
    rv.nodes.push_back(::std::move(n));
    return rv;
}
bool AbsolutePath::operator==(const AbsolutePath& x) const {
    if (this->crate != x.crate) {
        return false;
    }
    if (this->nodes != x.nodes) {
        return false;
    }
    return true;
}
// Returns true if this path is a prefix of the other path (or equal)
bool AbsolutePath::is_parent_of(const AbsolutePath& other) const {
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
PathNode::PathNode() {
}
void Path::Bindings::merge_from(const Bindings& x) {
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
Path::Path(Class c)
    : m_class(::std::move(c)) {
}
// INVALID
Path::Path()
    : m_class() {
}
// ABSOLUTE
Path::Path(RcString crate, ::std::vector<PathNode> nodes)
    : m_class(Class::make_Absolute({mv$(crate), mv$(nodes)})) {
}
Path::Path(const AbsolutePath& p)
    : m_class(Class::make_Absolute({p.crate, {}})) {
    auto& n = m_class.as_Absolute().nodes;
    n.reserve(p.nodes.size());
    for (const auto& v : p.nodes) {
        n.push_back(v);
    }
}
Path::Path(const PathBinding<PathBindingValue>& pb)
    : Path(pb.path) {
    this->m_bindings.value = pb.clone();
}
Path::Path(const PathBinding<PathBindingType>& pb)
    : Path(pb.path) {
    this->m_bindings.type = pb.clone();
}
Path::Path(const PathBinding<PathBindingMacro>& pb)
    : Path(pb.path) {
    this->m_bindings.macro = pb.clone();
}
Path::Path(const AbsolutePath& p, ::AST::PathParams pp)
    : Path(p) {
    auto& n = m_class.as_Absolute().nodes;
    assert(n.size() > 0);
    n.back().args() = ::std::move(pp);
}
Path Path::operator+(PathNode pn) const {
    Path tmp = Path(*this);
    tmp.append(mv$(pn));
    return tmp;
}
Path Path::operator+(const RcString& s) const {
    Path tmp = Path(*this);
    tmp.append(PathNode(s, {}));
    return tmp;
}
Path& Path::operator+=(PathNode pn) {
    this->append(mv$(pn));
    return *this;
}
const RcString& Path::as_trivial() const {
TU_MATCH_HDRA( (m_class), {)
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
size_t Path::size() const {
    TU_MATCH(Class, (m_class), (ent), (Invalid, assert(!m_class.is_Invalid()); throw ::std::runtime_error("Path::nodes() on Invalid");), (Local, return 1;), (Relative, return ent.nodes.size();), (Self, return ent.nodes.size();), (Super, return ent.nodes.size();), (Absolute, return ent.nodes.size();), (UFCS, return ent.nodes.size();))
    throw ::std::runtime_error("Path::nodes() fell off");
}
::std::vector<PathNode>& Path::nodes() {
    TU_MATCH(Class, (m_class), (ent), (Invalid, assert(!m_class.is_Invalid()); throw ::std::runtime_error("Path::nodes() on Invalid");), (Local, assert(!m_class.is_Local()); throw ::std::runtime_error("Path::nodes() on Local");), (Relative, return ent.nodes;), (Self, return ent.nodes;), (Super, return ent.nodes;), (Absolute, return ent.nodes;), (UFCS, return ent.nodes;))
    throw ::std::runtime_error("Path::nodes() fell off");
}
}

namespace AST {

::std::ostream& operator<<(::std::ostream& os, const AbsolutePath& x) {
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
}
