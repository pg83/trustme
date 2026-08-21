#pragma once

#include "hir_asm.h"
#include "ast_item.h"
#include "ast_attrs.h"
#include "ast_macro.h" // MacroInvocation
#include "ast_types.h"
#include "coretypes.h"
#include "ast_pattern.h"
#include "ast_expr_ptr.h"
#include "ast_generics.h"
#include "expand_common.h"
#include "settings.h"
#include "target_version.h"
#include "parse_tokentree.h"
#include "macro_rules_macro_rules_ptr.h"

#include <map>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <unordered_map>

class ASTCrate;

class ASTModule;
class ASTItem;

using ::std::move;
using ::std::unique_ptr;

struct ASTStructItem {
    ASTAttributeList attrs;
    ASTVisibility vis;
    RcString name;
    ASTType* type;
    // RFC3681
    ASTExpr defaultValue;

    //StructItem() {}

    ASTStructItem(ASTAttributeList attrs, ASTVisibility vis, RcString name, ASTType* ty, ASTExpr defaultValue);

    friend ::std::ostream& operator<<(::std::ostream& os, const ASTStructItem& x) {
        return os << x.vis << x.name << ": " << x.type;
    }

    ASTStructItem clone() const;
};

struct ASTTupleItem {
    ASTAttributeList attrs;
    ASTVisibility vis;
    ASTType* type;

    //TupleItem() {}

    ASTTupleItem(ASTAttributeList attrs, ASTVisibility vis, ASTType* ty);

    friend ::std::ostream& operator<<(::std::ostream& os, const ASTTupleItem& x) {
        return os << x.vis << x.type;
    }

    ASTTupleItem clone() const;
};

class ASTTypeAlias {
public:
    /// Normal generic parameter definitions
    ASTGenericParams params_;
    /// Holds bounds on this type, all bounds encoded as `Self: ...`
    ASTGenericParams selfBounds;
    ASTType* type_;

    //TypeAlias() {}
    ASTTypeAlias(ASTGenericParams params, ASTType* type);

    static ASTTypeAlias newAssociatedType(ASTGenericParams params, ASTGenericParams typeBounds, ASTType* defaultType);

    const ASTGenericParams& params() const {
        return params_;
    }

    ASTGenericParams& params() {
        return params_;
    }

    ASTType* type() const {
        return type_;
    }

    ASTType*& type() {
        return type_;
    }

    ASTTypeAlias clone() const;
};

class ASTTraitAlias {
public:
    ASTGenericParams params;
    std::vector<Spanned<TypeTraitPath>> traits;
    std::vector<Spanned<ASTLifetimeRef>> lifetimes;

    ASTTraitAlias clone() const;
};

enum class ASTLinkage {
    // no `#[linkage]` specified
    Default,
    // "weak" - allow multiple definitions
    Weak,
    // "extern_weak" - This external symbol can be missing
    // - Must be on a `static`
    ExternWeak,
};

class ASTStatic {
public:
    enum Class {
        CONST,
        STATIC,
        MUT,
    };

private:
    Class cls;
    ASTGenericParams params_;
    ASTType* type_;
    ASTExpr value_;

public:
    struct Markings {
        std::string linkName;
        std::string linkSection;
        ASTLinkage linkage = ASTLinkage::Default;
    } markings;

    ASTStatic(Class sClass, ASTType* type, ASTExpr value, ASTGenericParams params = {});

    const Class& sClass() const {
        return cls;
    }

    const ASTGenericParams& params() const {
        return params_;
    }

    ASTGenericParams& params() {
        return params_;
    }

    ASTType* type() const {
        return type_;
    }

    const ASTExpr& value() const {
        return value_;
    }

    ASTType*& type() {
        return type_;
    }

    ASTExpr& value() {
        return value_;
    }

    ASTStatic clone() const;
};

class ASTFunction {
public:
    struct Arg {
        ASTAttributeList attrs;
        ASTPattern pat;
        ASTType* ty;

        Arg(ASTPattern pat, ASTType* ty, ASTAttributeList attrs = {});
    };

    typedef ::std::vector<Arg> Arglist;

    struct Delegation {
        struct Target {
            ASTPath path;
            RcString name;
        };

        ::std::vector<Target> targets;
        ASTExpr body;
    };

    struct Flags {
        bool isConst;
        bool isUnsafe;
        bool isAsync;
        /// `gen fn`: the body is a coroutine and the function returns an iterator.
        bool isGen;

        Flags();

        static Flags makeUnsafe() {
            return Flags().setUnsafe();
        }

        Flags setUnsafe() const;

        Flags setConst() const;

        Flags setAsync() const;

        Flags setGen() const;
    };

private:
    Span span_;
    ASTGenericParams params_;
    ASTExpr code_;
    ASTType* rettype_;
    Arglist args_;
    bool isVariadic_; // extern only
    bool hasNamedVariadic_; // the last entry in args_ is the body-only VaList binding

    ::std::string abi_;
    Flags flags;
    ::std::unique_ptr<Delegation> delegation_;

public:
    struct Markings {
        enum Inline {
            Auto,
            Never,
            Normal,
            Always
        } inlineType = Inline::Auto;

        bool isCold = false;
        bool isNaked = false;
        /// Requested function entry alignment from `#[rustc_align(N)]`.
        u64 alignment = 0;
        std::vector<unsigned> rustcLegacyConstGenerics;

        std::string linkName;
        std::string linkSection;
        ASTLinkage linkage = ASTLinkage::Default;

        /// Lint levels set on this function by `#[allow]` and friends, by exact
        /// name and by group. Only the crate being compiled is linted, so these
        /// never leave it.
        ::std::map<RcString, CfgLintLevel> lintLevels;
        ::std::map<RcString, CfgLintLevel> lintGroupLevels;
    } markings;

    ASTFunction(const ASTFunction&) = delete;
    ASTFunction& operator=(const ASTFunction&) = delete;
    ASTFunction(ASTFunction&&) = default;
    ASTFunction& operator=(ASTFunction&&) = default;

    ASTFunction(Span sp, ::std::string abi, Flags flags, ASTGenericParams params, ASTType* retType, Arglist args, bool isVariadic, bool hasNamedVariadic = false);

    // Helper for derive, defines an ABI_RUST function with no generics
    ASTFunction(Span sp, ASTType* retType, Arglist args);

    void setCode(ASTExpr code) {
        code_ = ::std::move(code);
    }

    const Span& sp() const {
        return span_;
    }

    const ::std::string& abi() const {
        return abi_;
    };

    void setAbi(std::string s) {
        abi_ = std::move(s);
    }

    bool isConst() const {
        return flags.isConst;
    }

    bool isUnsafe() const {
        return flags.isUnsafe;
    }

    bool isAsync() const {
        return flags.isAsync;
    }

    bool isGen() const {
        return flags.isGen;
    }

    const ASTGenericParams& params() const {
        return params_;
    }

    ASTGenericParams& params() {
        return params_;
    }

    const ASTExpr& code() const {
        return code_;
    }

    ASTExpr& code() {
        return code_;
    }

    ASTType* rettype() const {
        return rettype_;
    }

    ASTType*& rettype() {
        return rettype_;
    }

    const Arglist& args() const {
        return args_;
    }

    Arglist& args() {
        return args_;
    }

    bool isVariadic() const {
        return isVariadic_;
    }

    bool hasNamedVariadic() const {
        return hasNamedVariadic_;
    }

    const Delegation* delegation() const {
        return delegation_.get();
    }

    Delegation* delegation() {
        return delegation_.get();
    }

    void setDelegation(Delegation delegation) {
        delegation_ = ::std::make_unique<Delegation>(::std::move(delegation));
    }

    ::std::unique_ptr<Delegation> takeDelegation() {
        return ::std::move(delegation_);
    }

    ASTFunction clone() const;
};

class ASTTrait {
    ASTGenericParams params_;
    ::std::vector<Spanned<TypeTraitPath>> supertraits_;
    ::std::vector<Spanned<ASTLifetimeRef>> lifetimes_;

    bool isMarker_;
    bool isUnsafe_;
    ASTNamedList<ASTItem> items_;

public:
    ASTTrait();
    ASTTrait(ASTGenericParams params, ::std::vector<Spanned<TypeTraitPath>> supertraits, ::std::vector<Spanned<ASTLifetimeRef>> lifetimes);
    ~ASTTrait();
    ASTTrait(ASTTrait&&);
    ASTTrait& operator=(ASTTrait&&);

    const ASTGenericParams& params() const {
        return params_;
    }

    ASTGenericParams& params() {
        return params_;
    }

    const ::std::vector<Spanned<TypeTraitPath>>& supertraits() const {
        return supertraits_;
    }

    ::std::vector<Spanned<TypeTraitPath>>& supertraits() {
        return supertraits_;
    }

    const ::std::vector<Spanned<ASTLifetimeRef>>& lifetimes() const {
        return lifetimes_;
    }

    ::std::vector<Spanned<ASTLifetimeRef>>& lifetimes() {
        return lifetimes_;
    }

    const ASTNamedList<ASTItem>& items() const {
        return items_;
    }

    ASTNamedList<ASTItem>& items() {
        return items_;
    }

    void addType(Span sp, RcString name, ASTAttributeList attrs, ASTType* type);
    void addFunction(Span sp, RcString name, ASTAttributeList attrs, ASTFunction fcn);
    void addStatic(Span sp, RcString name, ASTAttributeList attrs, ASTStatic v);

    void setIsMarker();
    bool isMarker() const;

    void setIsUnsafe() {
        isUnsafe_ = true;
    }

    bool isUnsafe() const {
        return isUnsafe_;
    }

    bool hasNamedItem(const RcString& name, bool& outIsFcn) const;

    ASTTrait clone() const;
};

// Item classes some ASTItem variants name; ASTItem stores through a pointer,
// so declarations are enough here and the classes follow below.
class ASTEnum;
class ASTStruct;
class ASTUnion;
class ASTImplDef;
class ASTImpl;
struct ASTUseItem;
class ASTExternBlock;
class ASTGlobalAsm;

// Definitions generated from ast_ast.tu.
#include "ast_ast_tu.h"

struct ASTEnumVariant {
    ASTAttributeList attrs;
    RcString name;
    ASTEnumVariantData data;
    /// Optional discriminant value
    ASTExpr discriminantValue;

    ASTEnumVariant();

    ASTEnumVariant(ASTAttributeList attrs, RcString name);

    ASTEnumVariant(ASTAttributeList attrs, RcString name, ::std::vector<ASTTupleItem> subTypes);

    ASTEnumVariant(ASTAttributeList attrs, RcString name, ::std::vector<ASTStructItem> fields);

    friend ::std::ostream& operator<<(::std::ostream& os, const ASTEnumVariant& x);
};

class ASTEnum {
    ASTGenericParams params_;
    ::std::vector<ASTEnumVariant> variants_;

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
        bool isReprC = false;
        u64 alignValue = 0;
    } markings;

    ASTEnum();

    ASTEnum(ASTGenericParams params, ::std::vector<ASTEnumVariant> variants);

    const ASTGenericParams& params() const {
        return params_;
    }

    ASTGenericParams& params() {
        return params_;
    }

    const ::std::vector<ASTEnumVariant>& variants() const {
        return variants_;
    }

    ::std::vector<ASTEnumVariant>& variants() {
        return variants_;
    }

    ASTEnum clone() const;
};

class ASTStruct {
    ASTGenericParams params_;

public:
    ASTStructData data;

    struct Markings {
        Markings();

        enum class Repr {
            Rust,
            C,
            Simd,
            Transparent,
        } repr = Repr::Rust;
        u64 alignValue = 0;
        // Indicates packing
        u64 maxFieldAlign = 0;

        // 1.39 nonzero etc
        bool scalarValidStartSet = false;
        U128 scalarValidStart;
        bool scalarValidEndSet = false;
        U128 scalarValidEnd;
    } markings;

    ASTStruct();

    ASTStruct(ASTGenericParams params);

    ASTStruct(ASTGenericParams params, ::std::vector<ASTStructItem> fields);

    ASTStruct(ASTGenericParams params, ::std::vector<ASTTupleItem> fields);

    const ASTGenericParams& params() const {
        return params_;
    }

    ASTGenericParams& params() {
        return params_;
    }

    ASTStruct clone() const;
};

class ASTUnion {
public:
    ASTGenericParams params_;
    ::std::vector<ASTStructItem> variants;

    struct Markings {
        enum class Repr {
            Rust,
            C,
            Transparent,
        } repr = Repr::Rust;
        /// `#[repr(align(N))]`
        u64 alignValue = 0;
        /// `#[repr(packed(N))]`, which caps the alignment of every member
        u64 maxFieldAlign = 0;
    } markings;

    ASTUnion(ASTGenericParams params, ::std::vector<ASTStructItem> fields);

    const ASTGenericParams& params() const {
        return params_;
    }

    ASTGenericParams& params() {
        return params_;
    }

    ASTUnion clone() const;
};

class ASTImplDef {
    bool isUnsafe_;
    bool isConst_;
    ASTGenericParams params_;
    Spanned<ASTPath> trait_;
    ASTType* type_;

public:
    ASTImplDef(ASTGenericParams params, Spanned<ASTPath> traitType, ASTType* implType);

    ASTImplDef(ASTImplDef&&) /*noexcept*/ = default;
    ASTImplDef& operator=(ASTImplDef&&) = default;

    void setIsUnsafe() {
        isUnsafe_ = true;
    }

    bool isUnsafe() const {
        return isUnsafe_;
    }

    void setIsConst() {
        isConst_ = true;
    }

    bool isConst() const {
        return isConst_;
    }

    const ASTGenericParams& params() const {
        return params_;
    }

    ASTGenericParams& params() {
        return params_;
    }

    const Spanned<ASTPath>& trait() const {
        return trait_;
    }

    Spanned<ASTPath>& trait() {
        return trait_;
    }

    ASTType* type() const {
        return type_;
    }

    ASTType*& type() {
        return type_;
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const ASTImplDef& impl);
};

class ASTImpl {
public:
    struct ImplItem {
        Span sp;
        ASTAttributeList attrs;
        ASTVisibility vis; // Ignored for trait impls
        bool isSpecialisable;
        RcString name;
        RcString sourceName;

        ::std::unique_ptr<ASTItem> data;
    };

private:
    ASTImplDef def_;

    ::std::vector<ImplItem> items_;
    //NamedList<ASTType*>   m_types;
    //NamedList<Function>  m_functions;
    //NamedList<Static>    m_statics;

public:
    ASTImpl(ASTImpl&&) /*noexcept*/;
    ASTImpl(ASTImplDef def);
    ~ASTImpl();
    ASTImpl& operator=(ASTImpl&&);

    void addFunction(Span sp, ASTAttributeList attrs, ASTVisibility vis, bool isSpecialisable, RcString name, ASTFunction fcn, RcString sourceName = {});
    void addType(Span sp, ASTAttributeList attrs, ASTVisibility vis, bool isSpecialisable, RcString name, ASTGenericParams params, ASTType* type, RcString sourceName = {});
    void addStatic(Span sp, ASTAttributeList attrs, ASTVisibility vis, bool isSpecialisable, RcString name, ASTStatic v, RcString sourceName = {});
    void addMacroInvocation(ASTMacroInvocation inv);

    const ASTImplDef& def() const {
        return def_;
    }

    ASTImplDef& def() {
        return def_;
    }

    const ::std::vector<ImplItem>& items() const {
        return items_;
    }

    ::std::vector<ImplItem>& items() {
        return items_;
    }

    bool hasNamedItem(const RcString& name) const;

    friend ::std::ostream& operator<<(::std::ostream& os, const ASTImpl& impl);

private:
};

struct ASTUseItem {
    Span sp; // Span covering the entire `use foo;`
    bool isPrelude = false; // Synthetic implicit prelude import

    struct Ent {
        Span sp; // Span covering just the path (final component)
        ASTPath path;
        RcString name; // If "", this is a glob/wildcard use
        /// Written as `self` in a use tree (`use m::foo::{self}`). Such an entry
        /// names what the prefix resolved to -- a module, an enum or a trait --
        /// and not a value of the same name beside it.
        bool isSelf = false;
        friend ::std::ostream& operator<<(::std::ostream& os, const ASTUseItem::Ent& x);
    };

    ::std::vector<Ent> entries;

    ASTUseItem clone() const;
};

class ASTExternBlock {
    ::std::string abi_;
    ::std::vector<ASTNamed<ASTItem>> items_;

public:
    struct Link {
        std::string libName;
    };

    std::vector<Link> libraries;

    ASTExternBlock(::std::string abi);
    ~ASTExternBlock();
    ASTExternBlock(ASTExternBlock&&);
    ASTExternBlock& operator=(ASTExternBlock&&);

    const ::std::string& abi() const {
        return abi_;
    }

    void addItem(ASTNamed<ASTItem> namedItem);

    // NOTE: Only Function and Static are valid.
    ::std::vector<ASTNamed<ASTItem>>& items() {
        return items_;
    }

    const ::std::vector<ASTNamed<ASTItem>>& items() const {
        return items_;
    }

    ASTExternBlock clone() const;
};

class ASTGlobalAsm {
public:
    using Operand = ASTGlobalAsmOperand;

    ::std::vector<AsmLine> lines;
    ::std::vector<Operand> operands;
    AsmOptions options;

    ASTGlobalAsm clone() const;
};

/// Representation of a parsed (and being converted) function
class ASTModule {
    ASTAbsolutePath myPath;

    // Module-level items
    /// General items
public:
    ::std::vector<std::unique_ptr<ASTNamed<ASTItem>>> items;

private:
    // --- Runtime caches and state ---
    ::std::vector<::std::shared_ptr<ASTModule>> anonModules;

    ::std::vector<ASTNamed<MacroRulesPtr>> macros_;

public:
    struct FileInfo {
        bool controlsDir = false;
        ////
        // Is this module disabled (i.e. it's tagged with a failing `#[cfg]`)?
        // Disables down-stream file loading (as that might fail)
        bool isDisabled = false;
        // Is this a `mod foo { ... }` or `mod foo;` (changes how `#[path]` is processed inside)
        bool inModBlock = false;
        // Path to this module
        ::std::string path = "!";
        // Directory controlled by this module
        ::std::string dir = "";
    };

    FileInfo fileInfo;

    bool insertPrelude = true; // Set to false by `#[no_prelude]` handler
    char indexPopulated = 0;   // 0 = no, 1 = partial, 2 = complete

    struct IndexEnt {
        bool isImport; // Set if this item has a path that isn't `mod->path() + name`
        bool fromPrelude; // Prelude names are local defaults, not module exports
        ASTVisibility vis;
        ASTPath path;
        /// A glob brought this name in. Two globs offering different items for
        /// one name is not an error until the name is used, so which of them
        /// won is not decided here.
        bool fromGlob = false;
        /// The glob that brought it in was itself reached through another
        /// module's glob, so what this module wrote does not decide it.
        bool fromNestedGlob = false;
        /// A macro expansion wrote the item. rustc resolves imports before it
        /// expands macros, so a name only two of them provide is a
        /// future-compatibility lint there rather than an error.
        bool fromMacro = false;
        /// Two globs offer this name, and neither shadows the other.
        bool ambiguous = false;
    };

    // TODO: Document difference between namespace and Type
    // TODO: These should use IndexEnt<AST::PathBinding<AST::PathBinding_*>>` instead
    ::std::unordered_map<RcString, IndexEnt> namespaceItems;
    ::std::unordered_map<RcString, IndexEnt> typeItems;
    ::std::unordered_map<RcString, IndexEnt> valueItems;
    ::std::unordered_map<RcString, IndexEnt> macroItems;
    // Imported traits are in a different list, because collisions still apply for method lookup
    ::std::vector<ASTAbsolutePath> traits;

    // List of macros imported from other modules (via #[macro_use], includes proc macros)
    // - First value is an absolute path to the macro (including crate name)
    struct MacroImport {
        bool isPub;
        RcString name; // Can be different, if `use foo as bar` is used
        ASTAbsolutePath path;
        MacroRef ref;

        MacroImport clone() const {
            return MacroImport{isPub, name, path, ref.clone()};
        }
    };

    ::std::vector<MacroImport> macroImports;

    struct Import {
        bool isPub;
        RcString name;
        ASTPath path; // If `name` is "", then this is a module/enum to glob
    };

    ::std::vector<Import> itemImports;

public:
    ASTModule();
    ASTModule(ASTAbsolutePath path);
    ~ASTModule();
    ASTModule(ASTModule&&);
    ASTModule& operator=(ASTModule&&);

    bool isAnon() const {
        return myPath.nodes.size() > 0 && myPath.nodes.back().c_str()[0] == '#';
    }

    /// Create an anon module (for use inside expressions)
    ::std::shared_ptr<ASTModule> addAnon();

    void addItem(ASTNamed<ASTItem> item);
    void addItem(Span sp, ASTVisibility vis, RcString name, ASTItem it, ASTAttributeList attrs);
    void addExtCrate(Span sp, ASTVisibility vis, RcString extName, RcString impName, ASTAttributeList attrs);
    void addMacroInvocation(ASTMacroInvocation item);

    void addMacro(bool isExported, RcString name, MacroRulesPtr macro);

    const ASTAbsolutePath& path() const {
        return myPath;
    }

    //      ::std::vector<Named<Item>>& items()       { return m_items; }
    //const ::std::vector<Named<Item>>& items() const { return m_items; }

    ::std::vector<::std::shared_ptr<ASTModule>>& anonMods() {
        return anonModules;
    }

    const ::std::vector<::std::shared_ptr<ASTModule>>& anonMods() const {
        return anonModules;
    }

    ASTNamedList<MacroRulesPtr>& macros() {
        return macros_;
    }

    const ASTNamedList<MacroRulesPtr>& macros() const {
        return macros_;
    }
};
