#pragma once

#include "span.h"
#include "ident.h"
#include "common.h"
#include "output.h"
#include "ast_types.h"
#include "ast_expr_ptr.h"
#include "ast_lifetime_ref.h"

#include <std/lib/vector.h>

#include <string>
#include <string>
#include <vector>
#include <cstdint>
#include <initializer_list>

class MacroRules;

class HIRModule;
class HIRTrait;
struct HIRTraitAlias;
class HIREnum;
class HIRStruct;
class HIRUnion;
class HIRStatic;

class ASTLifetimeRef;
class ASTGenericParams;
class ASTCrate;
class ASTModule;
class ASTTypeAlias;
class ASTEnum;
class ASTStruct;
class ASTUnion;
class ASTTrait;
class ASTTraitAlias;
class ASTStatic;
class ASTFunction;
class ASTExternCrate;

struct ASTAbsolutePath {
    RcString crate;
    stl::Vector<RcString> nodes;

    ASTAbsolutePath();

    ASTAbsolutePath(RcString crate, stl::Vector<RcString> nodes);

    ASTAbsolutePath operator+(RcString n) const;

    bool operator==(const ASTAbsolutePath& x) const;

    bool operator!=(const ASTAbsolutePath& x) const {
        return !(*this == x);
    }

    bool isParentOf(const ASTAbsolutePath& other) const;
};

struct ASTPathBindingModuleHir {
    const ASTExternCrate* crate;
    const HIRModule* mod;
};

#include "ast_path_binding_tu.h"

template <typename T>
struct ASTPathBinding {
    ASTAbsolutePath path;
    T binding;

    ASTPathBinding() {
    }

    ASTPathBinding(ASTAbsolutePath p, T b)
        : path(std::move(p))
        , binding(std::move(b))
    {
    }

    void set(ASTAbsolutePath p, T b) {
        path = std::move(p);
        binding = std::move(b);
    }

    bool is_Unbound() const {
        return this->binding.is_Unbound();
    }

    ASTPathBinding<T> clone() const {
        return ASTPathBinding(path, binding.clone());
    }
};

class ASTPathParamEnt;

struct ASTPathParams {
    std::vector<ASTPathParamEnt> entries;
    bool isParen = false;
    bool isRtn = false;

    ASTPathParams(ASTPathParams&& x);
    ASTPathParams(const ASTPathParams& x);
    ASTPathParams();
    ~ASTPathParams();

    ASTPathParams& operator=(ASTPathParams&& x);
    ASTPathParams& operator=(const ASTPathParams& x) = delete;

    bool isEmpty() const {
        return entries.empty() && !isRtn;
    }

    Ordering ord(const ASTPathParams& x) const;
};

class ASTPathNode {
    Ident ident_;
    ASTPathParams params_;

public:
    ASTPathNode();

    ASTPathNode(RcString name, ASTPathParams args = {});

    ASTPathNode(Ident::Hygiene hygiene, RcString name, ASTPathParams args = {});

    const RcString& name() const {
        return ident_.name;
    }

    RcString hygienicName() const {
        return ident_.hygienicName();
    }

    const ASTPathParams& args() const {
        return params_;
    }

    ASTPathParams& args() {
        return params_;
    }

    Ordering ord(const ASTPathNode& x) const;
    void printPretty(stl::ZeroCopyOutput& os, bool isTypeContext) const;

    bool operator==(const ASTPathNode& x) const {
        return ord(x) == OrdEqual;
    }
};

#include "ast_path_class_tu.h"

class ASTPath {
public:
    using Class = ASTPathClass;

    struct Bindings {
        ASTPathBinding<ASTPathBindingValue> value;
        ASTPathBinding<ASTPathBindingType> type;
        ASTPathBinding<ASTPathBindingMacro> macro;

        Bindings clone() const {
            return Bindings{value.clone(), type.clone(), macro.clone()};
        }

        bool hasBinding() const {
            return !value.is_Unbound() || !type.is_Unbound() || !macro.is_Unbound();
        }

        void mergeFrom(const Bindings& x);
    };

public:
    Class cls;
    Bindings bindings;

    virtual ~ASTPath();

    ASTPath(Class c);

    ASTPath();

    ASTPath(ASTPath&&) = default;
    ASTPath& operator=(ASTPath&& x) = default;

    /*explicit*/ ASTPath(const ASTPath& x);
    ASTPath& operator=(const ASTPath&) = delete;

    ASTPath(RcString crate, std::vector<ASTPathNode> nodes);

    ASTPath(const ASTAbsolutePath& p);

    ASTPath(const ASTPathBinding<ASTPathBindingValue>& pb);

    ASTPath(const ASTPathBinding<ASTPathBindingType>& pb);

    ASTPath(const ASTPathBinding<ASTPathBindingMacro>& pb);

    ASTPath(const ASTAbsolutePath& p, ASTPathParams pp);

    ASTPath(RcString name)
        : cls(Class::make_Local({mv$(name)}))
    {
    }

    static ASTPath newUfcsTy(ASTType* type, std::vector<ASTPathNode> nodes = {});
    static ASTPath newUfcsTrait(ASTType* type, ASTPath trait, std::vector<ASTPathNode> nodes = {});

    static ASTPath newLocal(RcString name) {
        return ASTPath(mv$(name));
    }

    static ASTPath newRelative(Ident::Hygiene hygiene, std::vector<ASTPathNode> nodes) {
        return ASTPath(Class::make_Relative({mv$(hygiene), mv$(nodes)}));
    }

    static ASTPath newSelf(std::vector<ASTPathNode> nodes) {
        return ASTPath(Class::make_Self({mv$(nodes)}));
    }

    static ASTPath newSuper(unsigned int count, std::vector<ASTPathNode> nodes) {
        return ASTPath(Class::make_Super({count, mv$(nodes)}));
    }

    ASTPath operator+(ASTPathNode pn) const;

    ASTPath operator+(const RcString& s) const;

    ASTPath operator+(const ASTPath& x) const {
        return ASTPath(*this) += x;
    }

    ASTPath& operator+=(const ASTPath& x);

    ASTPath& operator+=(ASTPathNode pn);

    void append(ASTPathNode node);

    bool isTrivial() const {
        switch (cls.tag()) {
            case Class::TAG_Local: {
                return true;
            }
            case Class::TAG_Relative: {
                auto& e = cls.as_Relative();
                return e.nodes.size() == 1 && e.nodes[0].args().isEmpty();
            }
            default: {
                return false;
            }
        }
    }

    const RcString& asTrivial() const;

    bool isValid() const {
        return !cls.is_Invalid();
    }

    bool isAbsolute() const {
        return cls.is_Absolute();
    }

    bool isRelative() const {
        return cls.is_Relative() || cls.is_Super() || cls.is_Self();
    }

    size_t size() const;

    bool isParentOf(const ASTPath& x) const;

    void bindVariable(unsigned int slot);

    std::vector<ASTPathNode>& nodes();

    const std::vector<ASTPathNode>& nodes() const {
        return ((ASTPath*)this)->nodes();
    }

    Ordering ord(const ASTPath& x) const;

    bool operator==(const ASTPath& x) const {
        return ord(x) == OrdEqual;
    }

    bool operator!=(const ASTPath& x) const {
        return ord(x) != OrdEqual;
    }

    bool operator<(const ASTPath& x) const {
        return ord(x) != OrdLess;
    }

    void printPretty(stl::ZeroCopyOutput& os, bool isTypeContext, bool isDebug = false) const;

private:
    void checkParamCounts(const ASTGenericParams& params, bool expectParams, ASTPathNode& node);

public:
};

#include "ast_path_tu.h"
