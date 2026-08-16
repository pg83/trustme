#pragma once

#include "span.h"
#include "ident.h"
#include "common.h"
#include "ast_types.h"
#include "ast_expr_ptr.h"
#include "tagged_union.h"
#include "ast_lifetime_ref.h"

#include <string>
#include <string>
#include <vector>
#include <cassert>
#include <cstdint>
#include <stdexcept>
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
    ::std::vector<RcString> nodes;

    ASTAbsolutePath();

    ASTAbsolutePath(RcString crate, ::std::vector<RcString> nodes);

    ASTAbsolutePath operator+(RcString n) const;

    bool operator==(const ASTAbsolutePath& x) const;

    bool operator!=(const ASTAbsolutePath& x) const {
        return !(*this == x);
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const ASTAbsolutePath& x);

    // Returns true if this path is a prefix of the other path (or equal)
    bool isParentOf(const ASTAbsolutePath& other) const;
};

TAGGED_UNION_EX(
    ASTPathBindingValue,
    (),
    Unbound,
    ((Unbound, struct {}),
     (Struct,
      struct {
          const ASTStruct* struct_;
          const HIRStruct* hir;
      }),
     (Static,
      struct {
          const ASTStatic* static_;
          const HIRStatic* hir; // if nullptr and static_ == nullptr, points to a `const`
      }),
     (Function, struct { const ASTFunction* func_; }),
     (EnumVar,
      struct {
          const ASTEnum* enum_;
          unsigned int idx;
          const HIREnum* hir;
      }),
     (Generic, struct { unsigned int index; }),
     (Variable, struct { unsigned int slot; })),
    (),
    (),
    (public : ASTPathBindingValue clone() const;)
);
TAGGED_UNION_EX(
    ASTPathBindingType,
    (),
    Unbound,
    ((Unbound, struct {}),
     (Primitive, eCoreType),
     (Crate, struct { const ASTExternCrate* crate_; }),
     (Module,
      struct {
          const ASTModule* module_;
          struct Hir {
              const ASTExternCrate* crate;
              const HIRModule* mod;
          } hir;
      }),
     (Struct,
      struct {
          const ASTStruct* struct_;
          const HIRStruct* hir;
      }),
     (Enum,
      struct {
          const ASTEnum* enum_;
          const HIREnum* hir;
      }),
     (Union,
      struct {
          const ASTUnion* union_;
          const HIRUnion* hir;
      }),
     (Trait,
      struct {
          const ASTTrait* trait_;
          const HIRTrait* hir;
      }),
     (TraitAlias,
      struct {
          const ASTTraitAlias* trait_;
          const HIRTraitAlias* hir;
      }),

     (EnumVar,
      struct {
          const ASTEnum* enum_;
          unsigned int idx;
          const HIREnum* hir;
      }),
     (TypeAlias, struct { const ASTTypeAlias* alias_; }),

     (TypeParameter, struct { unsigned int slot; })),
    (),
    (),
    (public : ASTPathBindingType clone() const;)
);
TAGGED_UNION_EX(
    ASTPathBindingMacro,
    (),
    Unbound,
    ((Unbound, struct {}),
     (ProcMacroDerive,
      struct {
          const ASTExternCrate* crate_;
          RcString macName;
      }),
     (ProcMacroAttribute,
      struct {
          const ASTExternCrate* crate_;
          RcString macName;
      }),
     (ProcMacro,
      struct {
          const ASTExternCrate* crate_;
          RcString macName;
      }),
     (MacroRules,
      struct {
          const ASTExternCrate* crate_; // Can be NULL
          const MacroRules* mac;
      })),
    (),
    (),
    (public : ASTPathBindingMacro clone() const;)
);

extern ::std::ostream& operator<<(::std::ostream& os, const ASTPathBindingValue& x);
extern ::std::ostream& operator<<(::std::ostream& os, const ASTPathBindingType& x);
extern ::std::ostream& operator<<(::std::ostream& os, const ASTPathBindingMacro& x);

/// <summary>
/// Wrapper for PathBinding_* that also includes an item path
/// </summary>
/// <typeparam name="T">PathBinding_*</typeparam>
template <typename T>
struct ASTPathBinding {
    ASTAbsolutePath path;
    T binding;

    ASTPathBinding() {
    }

    ASTPathBinding(ASTAbsolutePath p, T b)
        : path(::std::move(p))
        , binding(::std::move(b))
    {
    }

    void set(ASTAbsolutePath p, T b) {
        path = ::std::move(p);
        binding = ::std::move(b);
    }

    bool is_Unbound() const {
        return this->binding.is_Unbound();
    }

    ASTPathBinding<T> clone() const {
        return ASTPathBinding(path, binding.clone());
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const ASTPathBinding<T>& x) {
        if (!x.is_Unbound()) {
            os << x.binding << "[" << x.path << "]";
        } else {
            os << "Unbound";
        }
        return os;
    }
};

class ASTPathParamEnt;

struct ASTPathParams {
    ::std::vector<ASTPathParamEnt> entries;
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

    friend ::std::ostream& operator<<(::std::ostream& os, const ASTPathParams& x);
};

class ASTPathNode {
    RcString name_;
    ASTPathParams params_;

public:
    ASTPathNode();

    ASTPathNode(RcString name, ASTPathParams args = {});

    const RcString& name() const {
        return name_;
    }

    const ASTPathParams& args() const {
        return params_;
    }

    ASTPathParams& args() {
        return params_;
    }

    Ordering ord(const ASTPathNode& x) const;
    void printPretty(::std::ostream& os, bool isTypeContext) const;

    bool operator==(const ASTPathNode& x) const {
        return ord(x) == OrdEqual;
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const ASTPathNode& pn);
};

/*
* TODO: New Path structure:
* - Local(Ident): a resolved local name (variable, generic, ...)
* - Relative(Ident, vector<RcString>, Generics): `foo::bar<...>`
* - ModRelative(unsigned, vector<RcString>, Generics): `super::foo::bar<...>` or `self::foo`
* - Absolute(RcString, vector<RcString>, Generics): `::foocrate::bar<...>`
* - FullyQualified(Type, Path, Ident, Generics): `<Foo as Bar>::baz<...>`
* - TypeQualified(Type, Ident, Generics): `<FooType>::baz<...>`
* - UnknownQualified(Path, Ident, Generics): `FooTrait<...>::baz<...>` 
* 
* Goal:
* - Reduce memory usage of AST (avoids `PathParams` everywhere)
* - Simplify manipulation?
* 
* Downsides:
* - Resolve uses append methods etc
*/

class ASTPath {
public:
    TAGGED_UNION(
        Class,
        Invalid,
        (Invalid, struct {}),
        (Local,
         struct { // Variable / Type param (resolved)
             RcString name;
         }),
        (Relative,
         struct { // General relative
             Ident::Hygiene hygiene;
             ::std::vector<ASTPathNode> nodes;
         }),
        (Self,
         struct { // Module-relative
             ::std::vector<ASTPathNode> nodes;
         }),
        (Super,
         struct {                // Parent-relative
             unsigned int count; // Number of `super` keywords, must be >= 1
             ::std::vector<ASTPathNode> nodes;
         }),
        (Absolute,
         struct { // Absolute
             RcString crate;
             ::std::vector<ASTPathNode> nodes;
         }),
        (UFCS, struct {                       // Type-relative
            ASTType* type;  // always non-null
            ::std::unique_ptr<ASTPath> trait; // nullptr = inherent, Invalid = unknown trait
            ::std::vector<ASTPathNode> nodes;
        })
    );

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

    // INVALID
    ASTPath();

    ASTPath(ASTPath&&) = default;
    ASTPath& operator=(ASTPath&& x) = default;

    /*explicit*/ ASTPath(const ASTPath& x);
    ASTPath& operator=(const ASTPath&) = delete;

    // ABSOLUTE
    ASTPath(RcString crate, ::std::vector<ASTPathNode> nodes);

    ASTPath(const ASTAbsolutePath& p);

    ASTPath(const ASTPathBinding<ASTPathBindingValue>& pb);

    ASTPath(const ASTPathBinding<ASTPathBindingType>& pb);

    ASTPath(const ASTPathBinding<ASTPathBindingMacro>& pb);

    ASTPath(const ASTAbsolutePath& p, ASTPathParams pp);

    // Local (variable/type param)
    ASTPath(RcString name)
        : cls(Class::make_Local({mv$(name)}))
    {
    }

    // UFCS
    static ASTPath newUfcsTy(ASTType* type, ::std::vector<ASTPathNode> nodes = {});
    static ASTPath newUfcsTrait(ASTType* type, ASTPath trait, ::std::vector<ASTPathNode> nodes = {});

    // VARIABLE
    static ASTPath newLocal(RcString name) {
        return ASTPath(mv$(name));
    }

    // RELATIVE
    static ASTPath newRelative(Ident::Hygiene hygiene, ::std::vector<ASTPathNode> nodes) {
        return ASTPath(Class::make_Relative({mv$(hygiene), mv$(nodes)}));
    }

    static ASTPath newSelf(::std::vector<ASTPathNode> nodes) {
        return ASTPath(Class::make_Self({mv$(nodes)}));
    }

    static ASTPath newSuper(unsigned int count, ::std::vector<ASTPathNode> nodes) {
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
        TU_MATCH_DEF(Class, (cls), (e), (return false;), (Local, return true;), (Relative, return e.nodes.size() == 1 && e.nodes[0].args().isEmpty();))
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

    ::std::vector<ASTPathNode>& nodes();

    const ::std::vector<ASTPathNode>& nodes() const {
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

    void printPretty(::std::ostream& os, bool isTypeContext, bool isDebug = false) const;
    friend ::std::ostream& operator<<(::std::ostream& os, const ASTPath& path);

private:
    static void resolveArgsNl(::std::vector<ASTPathNode>& nodes, ::std::function<ASTType*(const char*)> fcn);

    void checkParamCounts(const ASTGenericParams& params, bool expectParams, ASTPathNode& node);

public:
    //}
};

TAGGED_UNION_EX(ASTPathParamEnt, (), Null, ((Null, struct {}), (Lifetime, ASTLifetimeRef), (Type, ASTType*), (Value, ASTExprNodeP), (AssociatedTyEqual, ::std::pair<ASTPathNode, ASTType*>), (AssociatedTyBound, ::std::pair<ASTPathNode, std::vector<TypeTraitPath>>), (AssociatedValueEqual, ::std::pair<ASTPathNode, ASTExprNodeP>)), (), (), (public : ASTPathParamEnt clone() const; Ordering ord(const ASTPathParamEnt& x) const; void fmt(::std::ostream& os) const;));
