#pragma once

#include "target_version.h"

#include <string>
#include <vector>
#include <stdexcept>
#include "coretypes.h"
#include <memory>
#include <map>
#include <unordered_map>
#include <algorithm>

#include "parse_tokentree.h"
#include "ast_types.h"

#include "ast_pattern.h"
#include "ast_attrs.h"
#include "ast_expr_ptr.h"
#include "ast_item.h"
#include "ast_macro.h" // MacroInvocation

#include "ast_generics.h"

#include "macro_rules_macro_rules_ptr.h"
#include "expand_common.h"
#include "hir_asm.h"

namespace AST {

    class Crate;

    class Module;
    class Item;

    using ::std::move;
    using ::std::unique_ptr;

    struct StructItem {
        ::AST::AttributeList mAttrs;
        ::AST::Visibility vis;
        RcString mName;
        TypeRef mType;
        // RFC3681
        AST::Expr defaultValue;

        //StructItem() {}

        StructItem(::AST::AttributeList attrs, AST::Visibility vis, RcString name, TypeRef ty, Expr default_value);

        friend ::std::ostream& operator<<(::std::ostream& os, const StructItem& x) {
            return os << x.vis << x.mName << ": " << x.mType;
        }

        StructItem clone() const;
    };

    struct TupleItem {
        ::AST::AttributeList mAttrs;
        ::AST::Visibility vis;
        TypeRef mType;

        //TupleItem() {}

        TupleItem(::AST::AttributeList attrs, AST::Visibility vis, TypeRef ty);

        friend ::std::ostream& operator<<(::std::ostream& os, const TupleItem& x) {
            return os << x.vis << x.mType;
        }

        TupleItem clone() const;
    };

    class TypeAlias {
    public:
        /// Normal generic parameter definitions
        GenericParams mParams;
        /// Holds bounds on this type, all bounds encoded as `Self: ...`
        GenericParams selfBounds;
        TypeRef mType;

        //TypeAlias() {}
        TypeAlias(GenericParams params, TypeRef type);

        static TypeAlias new_associated_type(GenericParams params, GenericParams type_bounds, TypeRef default_type);

        const GenericParams& params() const {
            return mParams;
        }

        GenericParams& params() {
            return mParams;
        }

        const TypeRef& type() const {
            return mType;
        }

        TypeRef& type() {
            return mType;
        }

        TypeAlias clone() const;
    };

    class TraitAlias {
    public:
        GenericParams params;
        std::vector<Spanned<TypeTraitPath>> traits;

        TraitAlias clone() const;
    };

    enum class Linkage {
        // no `#[linkage]` specified
        Default,
        // "weak" - allow multiple definitions
        Weak,
        // "extern_weak" - This external symbol can be missing
        // - Must be on a `static`
        ExternWeak,
    };

    class Static {
    public:
        enum Class {
            CONST,
            STATIC,
            MUT,
        };

    private:
        Class cls;
        TypeRef mType;
        Expr mValue;

    public:
        struct Markings {
            std::string link_name;
            std::string link_section;
            Linkage linkage = Linkage::Default;
        } markings;

        Static(Class s_class, TypeRef type, Expr value);

        const Class& s_class() const {
            return cls;
        }

        const TypeRef& type() const {
            return mType;
        }

        const Expr& value() const {
            return mValue;
        }

        TypeRef& type() {
            return mType;
        }

        Expr& value() {
            return mValue;
        }

        Static clone() const;
    };

    class Function {
    public:
        struct Arg {
            ::AST::AttributeList attrs;
            ::AST::Pattern pat;
            TypeRef ty;

            Arg(::AST::Pattern pat, TypeRef ty, ::AST::AttributeList attrs = {});
        };

        typedef ::std::vector<Arg> Arglist;

        struct Flags {
            bool is_const;
            bool is_unsafe;
            bool is_async;

            Flags();

            static Flags make_unsafe() {
                return Flags().set_unsafe();
            }

            Flags set_unsafe() const;

            Flags set_const() const;

            Flags set_async() const;
        };

    private:
        Span mSpan;
        GenericParams mParams;
        Expr mCode;
        TypeRef mRettype;
        Arglist mArgs;
        bool isVariadic; // extern only

        ::std::string mAbi;
        Flags flags;

    public:
        struct Markings {
            enum Inline {
                Auto,
                Never,
                Normal,
                Always
            } inline_type = Inline::Auto;

            bool is_cold = false;
            bool is_naked = false;
            std::vector<unsigned> rustc_legacy_const_generics;

            std::string link_name;
            std::string link_section;
            Linkage linkage = Linkage::Default;
        } markings;

        Function(const Function&) = delete;
        Function& operator=(const Function&) = delete;
        Function(Function&&) = default;
        Function& operator=(Function&&) = default;

        Function(Span sp, ::std::string abi, Flags flags, GenericParams params, TypeRef ret_type, Arglist args, bool is_variadic);

        // Helper for derive, defines an ABI_RUST function with no generics
        Function(Span sp, TypeRef ret_type, Arglist args);

        void set_code(Expr code) {
            mCode = ::std::move(code);
        }

        const Span& sp() const {
            return mSpan;
        }

        const ::std::string& abi() const {
            return mAbi;
        };

        void set_abi(std::string s) {
            mAbi = std::move(s);
        }

        bool is_const() const {
            return flags.is_const;
        }

        bool is_unsafe() const {
            return flags.is_unsafe;
        }

        bool is_async() const {
            return flags.is_async;
        }

        const GenericParams& params() const {
            return mParams;
        }

        GenericParams& params() {
            return mParams;
        }

        const Expr& code() const {
            return mCode;
        }

        Expr& code() {
            return mCode;
        }

        const TypeRef& rettype() const {
            return mRettype;
        }

        TypeRef& rettype() {
            return mRettype;
        }

        const Arglist& args() const {
            return mArgs;
        }

        Arglist& args() {
            return mArgs;
        }

        bool is_variadic() const {
            return isVariadic;
        }

        Function clone() const;
    };

    class Trait {
        GenericParams mParams;
        ::std::vector<Spanned<TypeTraitPath>> mSupertraits;
        ::std::vector<Spanned<LifetimeRef>> mLifetimes;

        bool isMarker;
        bool isUnsafe;
        NamedList<Item> mItems;

    public:
        Trait();
        Trait(GenericParams params, ::std::vector<Spanned<TypeTraitPath>> supertraits, ::std::vector<Spanned<LifetimeRef>> lifetimes);
        ~Trait();
        Trait(Trait&&);
        Trait& operator=(Trait&&);

        const GenericParams& params() const {
            return mParams;
        }

        GenericParams& params() {
            return mParams;
        }

        const ::std::vector<Spanned<TypeTraitPath>>& supertraits() const {
            return mSupertraits;
        }

        ::std::vector<Spanned<TypeTraitPath>>& supertraits() {
            return mSupertraits;
        }

        const ::std::vector<Spanned<LifetimeRef>>& lifetimes() const {
            return mLifetimes;
        }

        ::std::vector<Spanned<LifetimeRef>>& lifetimes() {
            return mLifetimes;
        }

        const NamedList<Item>& items() const {
            return mItems;
        }

        NamedList<Item>& items() {
            return mItems;
        }

        void addType(Span sp, RcString name, AttributeList attrs, TypeRef type);
        void addFunction(Span sp, RcString name, AttributeList attrs, Function fcn);
        void addStatic(Span sp, RcString name, AttributeList attrs, Static v);

        void set_is_marker();
        bool is_marker() const;

        void set_is_unsafe() {
            isUnsafe = true;
        }

        bool is_unsafe() const {
            return isUnsafe;
        }

        bool has_named_item(const RcString& name, bool& out_is_fcn) const;

        Trait clone() const;
    };

    TAGGED_UNION_EX(EnumVariantData, (), Unit, ((Unit, struct {}), (Tuple, struct { ::std::vector<TupleItem> mItems; }), (Struct, struct { ::std::vector<StructItem> fields; })), (), (), (public:));

    struct EnumVariant {
        AttributeList mAttrs;
        RcString mName;
        EnumVariantData mData;
        /// Optional discriminant value
        Expr discriminantValue;

        EnumVariant();

        EnumVariant(AttributeList attrs, RcString name);

        EnumVariant(AttributeList attrs, RcString name, ::std::vector<TupleItem> sub_types);

        EnumVariant(AttributeList attrs, RcString name, ::std::vector<StructItem> fields);

        friend ::std::ostream& operator<<(::std::ostream& os, const EnumVariant& x);
    };

    class Enum {
        GenericParams mParams;
        ::std::vector<EnumVariant> mVariants;

    public:
        struct Markings {
            enum class Repr {
                Rust,
                U8,
                U16,
                U32,
                U64,
                Usize,
                I8,
                I16,
                I32,
                I64,
                Isize,
                U128,
                I128
            } repr = Repr::Rust;
            bool is_repr_c = false;
            uint64_t alignValue = 0;
        } markings;

        Enum();

        Enum(GenericParams params, ::std::vector<EnumVariant> variants);

        const GenericParams& params() const {
            return mParams;
        }

        GenericParams& params() {
            return mParams;
        }

        const ::std::vector<EnumVariant>& variants() const {
            return mVariants;
        }

        ::std::vector<EnumVariant>& variants() {
            return mVariants;
        }

        Enum clone() const;
    };

    TAGGED_UNION_EX(StructData, (), Struct, ((Unit, struct {}), (Tuple, struct { ::std::vector<TupleItem> ents; }), (Struct, struct { ::std::vector<StructItem> ents; })), (), (), (public:));

    class Struct {
        GenericParams mParams;

    public:
        StructData mData;

        struct Markings {
            Markings();

            enum class Repr {
                Rust,
                C,
                Simd,
                Transparent,
            } repr = Repr::Rust;
            uint64_t alignValue = 0;
            // Indicates packing
            uint64_t max_field_align = 0;

            // 1.39 nonzero etc
            bool scalar_valid_start_set = false;
            U128 scalar_valid_start;
            bool scalar_valid_end_set = false;
            U128 scalar_valid_end;
        } markings;

        Struct();

        Struct(GenericParams params);

        Struct(GenericParams params, ::std::vector<StructItem> fields);

        Struct(GenericParams params, ::std::vector<TupleItem> fields);

        const GenericParams& params() const {
            return mParams;
        }

        GenericParams& params() {
            return mParams;
        }

        Struct clone() const;
    };

    class Union {
    public:
        GenericParams mParams;
        ::std::vector<StructItem> mVariants;

        struct Markings {
            enum class Repr {
                Rust,
                C,
                Transparent,
            } repr = Repr::Rust;
        } markings;

        Union(GenericParams params, ::std::vector<StructItem> fields);

        const GenericParams& params() const {
            return mParams;
        }

        GenericParams& params() {
            return mParams;
        }

        Union clone() const;
    };

    class ImplDef {
        bool isUnsafe;
        bool isConst;
        GenericParams mParams;
        Spanned<Path> mTrait;
        TypeRef mType;

    public:
        ImplDef(GenericParams params, Spanned<Path> trait_type, TypeRef impl_type);

        ImplDef(ImplDef&&) /*noexcept*/ = default;
        ImplDef& operator=(ImplDef&&) = default;

        void set_is_unsafe() {
            isUnsafe = true;
        }

        bool is_unsafe() const {
            return isUnsafe;
        }

        void set_is_const() {
            isConst = true;
        }

        bool is_const() const {
            return isConst;
        }

        const GenericParams& params() const {
            return mParams;
        }

        GenericParams& params() {
            return mParams;
        }

        const Spanned<Path>& trait() const {
            return mTrait;
        }

        Spanned<Path>& trait() {
            return mTrait;
        }

        const TypeRef& type() const {
            return mType;
        }

        TypeRef& type() {
            return mType;
        }

        friend ::std::ostream& operator<<(::std::ostream& os, const ImplDef& impl);
    };

    class Impl {
    public:
        struct ImplItem {
            Span sp;
            AttributeList attrs;
            AST::Visibility vis; // Ignored for trait impls
            bool is_specialisable;
            RcString name;

            ::std::unique_ptr<Item> data;
        };

    private:
        ImplDef mDef;

        ::std::vector<ImplItem> mItems;
        //NamedList<TypeRef>   m_types;
        //NamedList<Function>  m_functions;
        //NamedList<Static>    m_statics;

    public:
        Impl(Impl&&) /*noexcept*/;
        Impl(ImplDef def);
        ~Impl();
        Impl& operator=(Impl&&);

        void addFunction(Span sp, AttributeList attrs, AST::Visibility vis, bool is_specialisable, RcString name, Function fcn);
        void addType(Span sp, AttributeList attrs, AST::Visibility vis, bool is_specialisable, RcString name, GenericParams params, TypeRef type);
        void addStatic(Span sp, AttributeList attrs, AST::Visibility vis, bool is_specialisable, RcString name, Static v);
        void addMacroInvocation(MacroInvocation inv);

        const ImplDef& def() const {
            return mDef;
        }

        ImplDef& def() {
            return mDef;
        }

        const ::std::vector<ImplItem>& items() const {
            return mItems;
        }

        ::std::vector<ImplItem>& items() {
            return mItems;
        }

        bool has_named_item(const RcString& name) const;

        friend ::std::ostream& operator<<(::std::ostream& os, const Impl& impl);

    private:
    };

    struct UseItem {
        Span sp; // Span covering the entire `use foo;`

        struct Ent {
            Span sp; // Span covering just the path (final component)
            ::AST::Path path;
            RcString name; // If "", this is a glob/wildcard use
            friend ::std::ostream& operator<<(::std::ostream& os, const UseItem::Ent& x);
        };

        ::std::vector<Ent> entries;

        UseItem clone() const;
        //friend ::std::ostream& operator<<(::std::ostream& os, const UseItem& x);
    };

    class ExternBlock {
        ::std::string mAbi;
        ::std::vector<Named<Item>> mItems;

    public:
        struct Link {
            std::string lib_name;
        };

        std::vector<Link> libraries;

        ExternBlock(::std::string abi);
        ~ExternBlock();
        ExternBlock(ExternBlock&&);
        ExternBlock& operator=(ExternBlock&&);

        const ::std::string& abi() const {
            return mAbi;
        }

        void addItem(Named<Item> named_item);

        // NOTE: Only Function and Static are valid.
        ::std::vector<Named<Item>>& items() {
            return mItems;
        }

        const ::std::vector<Named<Item>>& items() const {
            return mItems;
        }

        ExternBlock clone() const;
    };

    class GlobalAsm {
    public:
        ::std::vector<AsmCommon::Line> lines;
        ::std::vector<AST::Path> symbols;
        AsmCommon::Options options;
    };

    /// Representation of a parsed (and being converted) function
    class Module {
        ::AST::AbsolutePath myPath;

        // Module-level items
        /// General items
    public:
        ::std::vector<std::unique_ptr<Named<Item>>> mItems;

    private:
        // --- Runtime caches and state ---
        ::std::vector<::std::shared_ptr<Module>> anonModules;

        ::std::vector<Named<MacroRulesPtr>> mMacros;

    public:
        struct FileInfo {
            bool controls_dir = false;
            ////
            //bool    force_no_load = false;
            // Is this module disabled (i.e. it's tagged with a failing `#[cfg]`)?
            // Disables down-stream file loading (as that might fail)
            bool is_disabled = false;
            // Is this a `mod foo { ... }` or `mod foo;` (changes how `#[path]` is processed inside)
            bool in_mod_block = false;
            // Path to this module
            ::std::string path = "!";
            // Directory controlled by this module
            ::std::string dir = "";
        };

        FileInfo fileInfo;

        bool insertPrelude = true; // Set to false by `#[no_prelude]` handler
        char indexPopulated = 0;   // 0 = no, 1 = partial, 2 = complete

        struct IndexEnt {
            bool is_import; // Set if this item has a path that isn't `mod->path() + name`
            ::AST::Visibility vis;
            ::AST::Path path;
        };

        // TODO: Document difference between namespace and Type
        // TODO: These should use IndexEnt<AST::PathBinding<AST::PathBinding_*>>` instead
        ::std::unordered_map<RcString, IndexEnt> namespaceItems;
        ::std::unordered_map<RcString, IndexEnt> typeItems;
        ::std::unordered_map<RcString, IndexEnt> valueItems;
        ::std::unordered_map<RcString, IndexEnt> macroItems;
        // Imported traits are in a different list, because collisions still apply for method lookup
        ::std::vector<::AST::AbsolutePath> traits;

        // List of macros imported from other modules (via #[macro_use], includes proc macros)
        // - First value is an absolute path to the macro (including crate name)
        struct MacroImport {
            bool is_pub;
            RcString name; // Can be different, if `use foo as bar` is used
            AST::AbsolutePath path;
            MacroRef ref;

            MacroImport clone() const {
                return MacroImport{is_pub, name, path, ref.clone()};
            }
        };

        ::std::vector<MacroImport> macroImports;

        struct Import {
            bool is_pub;
            RcString name;
            ::AST::Path path; // If `name` is "", then this is a module/enum to glob
        };

        ::std::vector<Import> itemImports;

    public:
        Module();
        Module(::AST::AbsolutePath path);
        ~Module();
        Module(Module&&);
        Module& operator=(Module&&);

        bool is_anon() const {
            return myPath.nodes.size() > 0 && myPath.nodes.back().c_str()[0] == '#';
        }

        /// Create an anon module (for use inside expressions)
        ::std::shared_ptr<AST::Module> addAnon();

        void addItem(Named<Item> item);
        void addItem(Span sp, Visibility vis, RcString name, Item it, AttributeList attrs);
        void addExtCrate(Span sp, Visibility vis, RcString ext_name, RcString imp_name, AttributeList attrs);
        void addMacroInvocation(MacroInvocation item);

        void addMacro(bool is_exported, RcString name, MacroRulesPtr macro);

        const ::AST::AbsolutePath& path() const {
            return myPath;
        }

        //      ::std::vector<Named<Item>>& items()       { return m_items; }
        //const ::std::vector<Named<Item>>& items() const { return m_items; }

        ::std::vector<::std::shared_ptr<Module>>& anonMods() {
            return anonModules;
        }

        const ::std::vector<::std::shared_ptr<Module>>& anonMods() const {
            return anonModules;
        }

        NamedList<MacroRulesPtr>& macros() {
            return mMacros;
        }

        const NamedList<MacroRulesPtr>& macros() const {
            return mMacros;
        }
    };

    TAGGED_UNION_EX(
        Item,
        (),
        None,
        ((None, struct {}),
         (MacroInv, MacroInvocation),
         // TODO: MacroDefinition
         (Use, UseItem),

         // Nameless items
         (ExternBlock, ExternBlock),
         (GlobalAsm, GlobalAsm),
         (Impl, Impl),
         (NegImpl, ImplDef),

         (Macro, MacroRulesPtr),
         (Module, Module),
         (Crate, struct { RcString name; }),

         (Type, TypeAlias),
         (Struct, Struct),
         (Enum, Enum),
         (Union, Union),
         (Trait, Trait),
         (TraitAlias, TraitAlias),

         (Function, Function),
         (Static, Static)),

        (),
        (),
        (Item clone() const;)
    );

} // namespace AST
