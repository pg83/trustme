#pragma once

#include "common.h"
#include <cstdint>
#include <string>
#include <stdexcept>
#include <vector>
#include <initializer_list>
#include <cassert>
#include "tagged_union.h"
#include <string>
#include "span.h"
#include "ident.h"
#include "ast_lifetime_ref.h"
#include "ast_types.h"
#include "ast_expr_ptr.h"

class MacroRules;

namespace HIR {
    class Module;
    class Trait;
    struct TraitAlias;
    class Enum;
    class Struct;
    class Union;
    class Static;
} // namespace HIR

namespace AST {

    class LifetimeRef;
    class GenericParams;
    class Crate;
    class Module;
    class TypeAlias;
    class Enum;
    class Struct;
    class Union;
    class Trait;
    class TraitAlias;
    class Static;
    class Function;
    class ExternCrate;

    struct AbsolutePath {
        RcString crate;
        ::std::vector<RcString> nodes;

        AbsolutePath();

        AbsolutePath(RcString crate, ::std::vector<RcString> nodes);

        AbsolutePath operator+(RcString n) const;

        bool operator==(const AbsolutePath& x) const;

        bool operator!=(const AbsolutePath& x) const {
            return !(*this == x);
        }

        friend ::std::ostream& operator<<(::std::ostream& os, const AbsolutePath& x);

        // Returns true if this path is a prefix of the other path (or equal)
        bool is_parent_of(const AbsolutePath& other) const;
    };

    TAGGED_UNION_EX(
        PathBinding_Value,
        (),
        Unbound,
        ((Unbound, struct {}),
         (Struct,
          struct {
              const Struct* struct_;
              const ::HIR::Struct* hir;
          }),
         (Static,
          struct {
              const Static* static_;
              const ::HIR::Static* hir; // if nullptr and static_ == nullptr, points to a `const`
          }),
         (Function, struct { const Function* func_; }),
         (EnumVar,
          struct {
              const Enum* enum_;
              unsigned int idx;
              const ::HIR::Enum* hir;
          }),
         (Generic, struct { unsigned int index; }),
         (Variable, struct { unsigned int slot; })),
        (),
        (),
        (public : PathBinding_Value clone() const;)
    );
    TAGGED_UNION_EX(
        PathBinding_Type,
        (),
        Unbound,
        ((Unbound, struct {}),
         (Primitive, eCoreType),
         (Crate, struct { const ExternCrate* crate_; }),
         (Module,
          struct {
              const Module* module_;
              struct Hir {
                  const ::AST::ExternCrate* crate;
                  const ::HIR::Module* mod;
              } hir;
          }),
         (Struct,
          struct {
              const Struct* struct_;
              const ::HIR::Struct* hir;
          }),
         (Enum,
          struct {
              const Enum* enum_;
              const ::HIR::Enum* hir;
          }),
         (Union,
          struct {
              const Union* union_;
              const ::HIR::Union* hir;
          }),
         (Trait,
          struct {
              const Trait* trait_;
              const ::HIR::Trait* hir;
          }),
         (TraitAlias,
          struct {
              const TraitAlias* trait_;
              const ::HIR::TraitAlias* hir;
          }),

         (EnumVar,
          struct {
              const Enum* enum_;
              unsigned int idx;
              const ::HIR::Enum* hir;
          }),
         (TypeAlias, struct { const TypeAlias* alias_; }),

         (TypeParameter, struct { unsigned int slot; })),
        (),
        (),
        (public : PathBinding_Type clone() const;)
    );
    TAGGED_UNION_EX(
        PathBinding_Macro,
        (),
        Unbound,
        ((Unbound, struct {}),
         (ProcMacroDerive,
          struct {
              const ExternCrate* crate_;
              RcString mac_name;
          }),
         (ProcMacroAttribute,
          struct {
              const ExternCrate* crate_;
              RcString mac_name;
          }),
         (ProcMacro,
          struct {
              const ExternCrate* crate_;
              RcString mac_name;
          }),
         (MacroRules,
          struct {
              const ExternCrate* crate_; // Can be NULL
              const MacroRules* mac;
          })),
        (),
        (),
        (public : PathBinding_Macro clone() const;)
    );

    extern ::std::ostream& operator<<(::std::ostream& os, const PathBinding_Value& x);
    extern ::std::ostream& operator<<(::std::ostream& os, const PathBinding_Type& x);
    extern ::std::ostream& operator<<(::std::ostream& os, const PathBinding_Macro& x);

    /// <summary>
    /// Wrapper for PathBinding_* that also includes an item path
    /// </summary>
    /// <typeparam name="T">PathBinding_*</typeparam>
    template <typename T>
    struct PathBinding {
        AbsolutePath path;
        T binding;

        PathBinding() {
        }

        PathBinding(AbsolutePath p, T b)
            : path(::std::move(p))
            , binding(::std::move(b))
        {
        }

        void set(AbsolutePath p, T b) {
            path = ::std::move(p);
            binding = ::std::move(b);
        }

        bool is_Unbound() const {
            return this->binding.is_Unbound();
        }

        PathBinding<T> clone() const {
            return PathBinding(path, binding.clone());
        }

        friend ::std::ostream& operator<<(::std::ostream& os, const PathBinding<T>& x) {
            if (!x.is_Unbound()) {
                os << x.binding << "[" << x.path << "]";
            } else {
                os << "Unbound";
            }
            return os;
        }
    };

    class PathParamEnt;

    struct PathParams {
        ::std::vector<PathParamEnt> m_entries;
        bool m_is_paren = false;

        PathParams(PathParams&& x);
        PathParams(const PathParams& x);
        PathParams();
        ~PathParams();

        PathParams& operator=(PathParams&& x);
        PathParams& operator=(const PathParams& x) = delete;

        bool is_empty() const {
            return m_entries.empty();
        }

        Ordering ord(const PathParams& x) const;

        friend ::std::ostream& operator<<(::std::ostream& os, const PathParams& x);
    };

    class PathNode {
        RcString m_name;
        PathParams m_params;

    public:
        PathNode();

        PathNode(RcString name, PathParams args = {});

        const RcString& name() const {
            return m_name;
        }

        const ::AST::PathParams& args() const {
            return m_params;
        }

        ::AST::PathParams& args() {
            return m_params;
        }

        Ordering ord(const PathNode& x) const;
        void print_pretty(::std::ostream& os, bool is_type_context) const;

        bool operator==(const PathNode& x) const {
            return ord(x) == OrdEqual;
        }

        friend ::std::ostream& operator<<(::std::ostream& os, const PathNode& pn);
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

    class Path {
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
                 ::std::vector<PathNode> nodes;
             }),
            (Self,
             struct { // Module-relative
                 ::std::vector<PathNode> nodes;
             }),
            (Super,
             struct {                // Parent-relative
                 unsigned int count; // Number of `super` keywords, must be >= 1
                 ::std::vector<PathNode> nodes;
             }),
            (Absolute,
             struct { // Absolute
                 RcString crate;
                 ::std::vector<PathNode> nodes;
             }),
            (UFCS, struct {                      // Type-relative
                ::std::unique_ptr<TypeRef> type; // always non-null
                ::std::unique_ptr<Path> trait;   // nullptr = inherent, Invalid = unknown trait
                ::std::vector<PathNode> nodes;
            })
        );

        struct Bindings {
            PathBinding<PathBinding_Value> value;
            PathBinding<PathBinding_Type> type;
            PathBinding<PathBinding_Macro> macro;

            Bindings clone() const {
                return Bindings{value.clone(), type.clone(), macro.clone()};
            }

            bool has_binding() const {
                return !value.is_Unbound() || !type.is_Unbound() || !macro.is_Unbound();
            }

            void merge_from(const Bindings& x);
        };

    public:
        Class m_class;
        Bindings m_bindings;

        virtual ~Path();

        Path(Class c);

        // INVALID
        Path();

        Path(Path&&) = default;
        Path& operator=(AST::Path&& x) = default;

        /*explicit*/ Path(const Path& x);
        Path& operator=(const AST::Path&) = delete;

        // ABSOLUTE
        Path(RcString crate, ::std::vector<PathNode> nodes);

        Path(const AbsolutePath& p);

        Path(const PathBinding<PathBinding_Value>& pb);

        Path(const PathBinding<PathBinding_Type>& pb);

        Path(const PathBinding<PathBinding_Macro>& pb);

        Path(const AbsolutePath& p, ::AST::PathParams pp);

        // Local (variable/type param)
        Path(RcString name)
            : m_class(Class::make_Local({mv$(name)}))
        {
        }

        // UFCS
        static Path new_ufcs_ty(TypeRef type, ::std::vector<PathNode> nodes = {});
        static Path new_ufcs_trait(TypeRef type, Path trait, ::std::vector<PathNode> nodes = {});

        // VARIABLE
        static Path new_local(RcString name) {
            return Path(mv$(name));
        }

        // RELATIVE
        static Path new_relative(Ident::Hygiene hygiene, ::std::vector<PathNode> nodes) {
            return Path(Class::make_Relative({mv$(hygiene), mv$(nodes)}));
        }

        static Path new_self(::std::vector<PathNode> nodes) {
            return Path(Class::make_Self({mv$(nodes)}));
        }

        static Path new_super(unsigned int count, ::std::vector<PathNode> nodes) {
            return Path(Class::make_Super({count, mv$(nodes)}));
        }

        Path operator+(PathNode pn) const;

        Path operator+(const RcString& s) const;

        Path operator+(const Path& x) const {
            return Path(*this) += x;
        }

        Path& operator+=(const Path& x);

        Path& operator+=(PathNode pn);

    #if 1
        void append(PathNode node) {
            assert(!m_class.is_Invalid());
            //if( m_class.is_Invalid() )
            //    m_class = Class::make_Relative({});
            nodes().push_back(mv$(node));
            m_bindings = Bindings();
        }
    #endif

        bool is_trivial() const {
            TU_MATCH_DEF(Class, (m_class), (e), (return false;), (Local, return true;), (Relative, return e.nodes.size() == 1 && e.nodes[0].args().is_empty();))
        }

        const RcString& as_trivial() const;

        bool is_valid() const {
            return !m_class.is_Invalid();
        }

        bool is_absolute() const {
            return m_class.is_Absolute();
        }

        bool is_relative() const {
            return m_class.is_Relative() || m_class.is_Super() || m_class.is_Self();
        }

        size_t size() const;

        bool is_parent_of(const Path& x) const;

        void bind_variable(unsigned int slot);

        ::std::vector<PathNode>& nodes();

        const ::std::vector<PathNode>& nodes() const {
            return ((Path*)this)->nodes();
        }


        Ordering ord(const Path& x) const;

        bool operator==(const Path& x) const {
            return ord(x) == OrdEqual;
        }

        bool operator!=(const Path& x) const {
            return ord(x) != OrdEqual;
        }

        bool operator<(const Path& x) const {
            return ord(x) != OrdLess;
        }

        void print_pretty(::std::ostream& os, bool is_type_context, bool is_debug = false) const;
        friend ::std::ostream& operator<<(::std::ostream& os, const Path& path);

    private:
        static void resolve_args_nl(::std::vector<PathNode>& nodes, ::std::function<TypeRef(const char*)> fcn);

        void check_param_counts(const GenericParams& params, bool expect_params, PathNode& node);

    public:
        //void bind_enum_var(const Enum& ent, const RcString& name);
        //void bind_function(const Function& ent) {
        //    m_bindings.value = PathBinding_Value::make_Function({&ent});
        //}
    };

    TAGGED_UNION_EX(PathParamEnt, (), Null, ((Null, struct {}), (Lifetime, LifetimeRef), (Type, TypeRef), (Value, AST::ExprNodeP), (AssociatedTyEqual, ::std::pair<PathNode, TypeRef>), (AssociatedTyBound, ::std::pair<PathNode, std::vector<Path>>)), (), (), (public : PathParamEnt clone() const; Ordering ord(const PathParamEnt& x) const; void fmt(::std::ostream& os) const;));

} // namespace AST

