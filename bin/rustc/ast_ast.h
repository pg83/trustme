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
    ASTAttributeList mAttrs;
    ASTVisibility vis;
    RcString mName;
    ASTType* mType;
    // RFC3681
    ASTExpr defaultValue;

    //StructItem() {}

    ASTStructItem(ASTAttributeList attrs, ASTVisibility vis, RcString name, ASTType* ty, ASTExpr defaultValue);

    friend ::std::ostream& operator<<(::std::ostream& os, const ASTStructItem& x) {
        return os << x.vis << x.mName << ": " << x.mType;
    }

    ASTStructItem clone() const;
};

struct ASTTupleItem {
    ASTAttributeList mAttrs;
    ASTVisibility vis;
    ASTType* mType;

    //TupleItem() {}

    ASTTupleItem(ASTAttributeList attrs, ASTVisibility vis, ASTType* ty);

    friend ::std::ostream& operator<<(::std::ostream& os, const ASTTupleItem& x) {
        return os << x.vis << x.mType;
    }

    ASTTupleItem clone() const;
};

class ASTTypeAlias {
public:
    /// Normal generic parameter definitions
    ASTGenericParams mParams;
    /// Holds bounds on this type, all bounds encoded as `Self: ...`
    ASTGenericParams selfBounds;
    ASTType* mType;

    //TypeAlias() {}
    ASTTypeAlias(ASTGenericParams params, ASTType* type);

    static ASTTypeAlias newAssociatedType(ASTGenericParams params, ASTGenericParams typeBounds, ASTType* defaultType);

    const ASTGenericParams& params() const {
        return mParams;
    }

    ASTGenericParams& params() {
        return mParams;
    }

    ASTType* type() const {
        return mType;
    }

    ASTType*& type() {
        return mType;
    }

    ASTTypeAlias clone() const;
};

class ASTTraitAlias {
public:
    ASTGenericParams params;
    std::vector<Spanned<TypeTraitPath>> traits;

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
    ASTType* mType;
    ASTExpr mValue;

public:
    struct Markings {
        std::string linkName;
        std::string linkSection;
        ASTLinkage linkage = ASTLinkage::Default;
    } markings;

    ASTStatic(Class sClass, ASTType* type, ASTExpr value);

    const Class& sClass() const {
        return cls;
    }

    ASTType* type() const {
        return mType;
    }

    const ASTExpr& value() const {
        return mValue;
    }

    ASTType*& type() {
        return mType;
    }

    ASTExpr& value() {
        return mValue;
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

    struct Flags {
        bool isConst;
        bool isUnsafe;
        bool isAsync;

        Flags();

        static Flags makeUnsafe() {
            return Flags().setUnsafe();
        }

        Flags setUnsafe() const;

        Flags setConst() const;

        Flags setAsync() const;
    };

private:
    Span mSpan;
    ASTGenericParams mParams;
    ASTExpr mCode;
    ASTType* mRettype;
    Arglist mArgs;
    bool mIsVariadic; // extern only

    ::std::string mAbi;
    Flags flags;

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
        std::vector<unsigned> rustcLegacyConstGenerics;

        std::string linkName;
        std::string linkSection;
        ASTLinkage linkage = ASTLinkage::Default;
    } markings;

    ASTFunction(const ASTFunction&) = delete;
    ASTFunction& operator=(const ASTFunction&) = delete;
    ASTFunction(ASTFunction&&) = default;
    ASTFunction& operator=(ASTFunction&&) = default;

    ASTFunction(Span sp, ::std::string abi, Flags flags, ASTGenericParams params, ASTType* retType, Arglist args, bool isVariadic);

    // Helper for derive, defines an ABI_RUST function with no generics
    ASTFunction(Span sp, ASTType* retType, Arglist args);

    void setCode(ASTExpr code) {
        mCode = ::std::move(code);
    }

    const Span& sp() const {
        return mSpan;
    }

    const ::std::string& abi() const {
        return mAbi;
    };

    void setAbi(std::string s) {
        mAbi = std::move(s);
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

    const ASTGenericParams& params() const {
        return mParams;
    }

    ASTGenericParams& params() {
        return mParams;
    }

    const ASTExpr& code() const {
        return mCode;
    }

    ASTExpr& code() {
        return mCode;
    }

    ASTType* rettype() const {
        return mRettype;
    }

    ASTType*& rettype() {
        return mRettype;
    }

    const Arglist& args() const {
        return mArgs;
    }

    Arglist& args() {
        return mArgs;
    }

    bool isVariadic() const {
        return mIsVariadic;
    }

    ASTFunction clone() const;
};

class ASTTrait {
    ASTGenericParams mParams;
    ::std::vector<Spanned<TypeTraitPath>> mSupertraits;
    ::std::vector<Spanned<ASTLifetimeRef>> mLifetimes;

    bool mIsMarker;
    bool mIsUnsafe;
    ASTNamedList<ASTItem> mItems;

public:
    ASTTrait();
    ASTTrait(ASTGenericParams params, ::std::vector<Spanned<TypeTraitPath>> supertraits, ::std::vector<Spanned<ASTLifetimeRef>> lifetimes);
    ~ASTTrait();
    ASTTrait(ASTTrait&&);
    ASTTrait& operator=(ASTTrait&&);

    const ASTGenericParams& params() const {
        return mParams;
    }

    ASTGenericParams& params() {
        return mParams;
    }

    const ::std::vector<Spanned<TypeTraitPath>>& supertraits() const {
        return mSupertraits;
    }

    ::std::vector<Spanned<TypeTraitPath>>& supertraits() {
        return mSupertraits;
    }

    const ::std::vector<Spanned<ASTLifetimeRef>>& lifetimes() const {
        return mLifetimes;
    }

    ::std::vector<Spanned<ASTLifetimeRef>>& lifetimes() {
        return mLifetimes;
    }

    const ASTNamedList<ASTItem>& items() const {
        return mItems;
    }

    ASTNamedList<ASTItem>& items() {
        return mItems;
    }

    void addType(Span sp, RcString name, ASTAttributeList attrs, ASTType* type);
    void addFunction(Span sp, RcString name, ASTAttributeList attrs, ASTFunction fcn);
    void addStatic(Span sp, RcString name, ASTAttributeList attrs, ASTStatic v);

    void setIsMarker();
    bool isMarker() const;

    void setIsUnsafe() {
        mIsUnsafe = true;
    }

    bool isUnsafe() const {
        return mIsUnsafe;
    }

    bool hasNamedItem(const RcString& name, bool& outIsFcn) const;

    ASTTrait clone() const;
};

TAGGED_UNION_EX(ASTEnumVariantData, (), Unit, ((Unit, struct {}), (Tuple, struct { ::std::vector<ASTTupleItem> mItems; }), (Struct, struct { ::std::vector<ASTStructItem> fields; })), (), (), (public:));

struct ASTEnumVariant {
    ASTAttributeList mAttrs;
    RcString mName;
    ASTEnumVariantData mData;
    /// Optional discriminant value
    ASTExpr discriminantValue;

    ASTEnumVariant();

    ASTEnumVariant(ASTAttributeList attrs, RcString name);

    ASTEnumVariant(ASTAttributeList attrs, RcString name, ::std::vector<ASTTupleItem> subTypes);

    ASTEnumVariant(ASTAttributeList attrs, RcString name, ::std::vector<ASTStructItem> fields);

    friend ::std::ostream& operator<<(::std::ostream& os, const ASTEnumVariant& x);
};

class ASTEnum {
    ASTGenericParams mParams;
    ::std::vector<ASTEnumVariant> mVariants;

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
        uint64_t alignValue = 0;
    } markings;

    ASTEnum();

    ASTEnum(ASTGenericParams params, ::std::vector<ASTEnumVariant> variants);

    const ASTGenericParams& params() const {
        return mParams;
    }

    ASTGenericParams& params() {
        return mParams;
    }

    const ::std::vector<ASTEnumVariant>& variants() const {
        return mVariants;
    }

    ::std::vector<ASTEnumVariant>& variants() {
        return mVariants;
    }

    ASTEnum clone() const;
};

TAGGED_UNION_EX(ASTStructData, (), Struct, ((Unit, struct {}), (Tuple, struct { ::std::vector<ASTTupleItem> ents; }), (Struct, struct { ::std::vector<ASTStructItem> ents; })), (), (), (public:));

class ASTStruct {
    ASTGenericParams mParams;

public:
    ASTStructData mData;

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
        uint64_t maxFieldAlign = 0;

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
        return mParams;
    }

    ASTGenericParams& params() {
        return mParams;
    }

    ASTStruct clone() const;
};

class ASTUnion {
public:
    ASTGenericParams mParams;
    ::std::vector<ASTStructItem> mVariants;

    struct Markings {
        enum class Repr {
            Rust,
            C,
            Transparent,
        } repr = Repr::Rust;
    } markings;

    ASTUnion(ASTGenericParams params, ::std::vector<ASTStructItem> fields);

    const ASTGenericParams& params() const {
        return mParams;
    }

    ASTGenericParams& params() {
        return mParams;
    }

    ASTUnion clone() const;
};

class ASTImplDef {
    bool mIsUnsafe;
    bool mIsConst;
    ASTGenericParams mParams;
    Spanned<ASTPath> mTrait;
    ASTType* mType;

public:
    ASTImplDef(ASTGenericParams params, Spanned<ASTPath> traitType, ASTType* implType);

    ASTImplDef(ASTImplDef&&) /*noexcept*/ = default;
    ASTImplDef& operator=(ASTImplDef&&) = default;

    void setIsUnsafe() {
        mIsUnsafe = true;
    }

    bool isUnsafe() const {
        return mIsUnsafe;
    }

    void setIsConst() {
        mIsConst = true;
    }

    bool isConst() const {
        return mIsConst;
    }

    const ASTGenericParams& params() const {
        return mParams;
    }

    ASTGenericParams& params() {
        return mParams;
    }

    const Spanned<ASTPath>& trait() const {
        return mTrait;
    }

    Spanned<ASTPath>& trait() {
        return mTrait;
    }

    ASTType* type() const {
        return mType;
    }

    ASTType*& type() {
        return mType;
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

        ::std::unique_ptr<ASTItem> data;
    };

private:
    ASTImplDef mDef;

    ::std::vector<ImplItem> mItems;
    //NamedList<ASTType*>   m_types;
    //NamedList<Function>  m_functions;
    //NamedList<Static>    m_statics;

public:
    ASTImpl(ASTImpl&&) /*noexcept*/;
    ASTImpl(ASTImplDef def);
    ~ASTImpl();
    ASTImpl& operator=(ASTImpl&&);

    void addFunction(Span sp, ASTAttributeList attrs, ASTVisibility vis, bool isSpecialisable, RcString name, ASTFunction fcn);
    void addType(Span sp, ASTAttributeList attrs, ASTVisibility vis, bool isSpecialisable, RcString name, ASTGenericParams params, ASTType* type);
    void addStatic(Span sp, ASTAttributeList attrs, ASTVisibility vis, bool isSpecialisable, RcString name, ASTStatic v);
    void addMacroInvocation(ASTMacroInvocation inv);

    const ASTImplDef& def() const {
        return mDef;
    }

    ASTImplDef& def() {
        return mDef;
    }

    const ::std::vector<ImplItem>& items() const {
        return mItems;
    }

    ::std::vector<ImplItem>& items() {
        return mItems;
    }

    bool hasNamedItem(const RcString& name) const;

    friend ::std::ostream& operator<<(::std::ostream& os, const ASTImpl& impl);

private:
};

struct ASTUseItem {
    Span sp; // Span covering the entire `use foo;`

    struct Ent {
        Span sp; // Span covering just the path (final component)
        ASTPath path;
        RcString name; // If "", this is a glob/wildcard use
        friend ::std::ostream& operator<<(::std::ostream& os, const ASTUseItem::Ent& x);
    };

    ::std::vector<Ent> entries;

    ASTUseItem clone() const;
};

class ASTExternBlock {
    ::std::string mAbi;
    ::std::vector<ASTNamed<ASTItem>> mItems;

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
        return mAbi;
    }

    void addItem(ASTNamed<ASTItem> namedItem);

    // NOTE: Only Function and Static are valid.
    ::std::vector<ASTNamed<ASTItem>>& items() {
        return mItems;
    }

    const ::std::vector<ASTNamed<ASTItem>>& items() const {
        return mItems;
    }

    ASTExternBlock clone() const;
};

class ASTGlobalAsm {
public:
    TAGGED_UNION(
        Operand,
        Const,
        (Const, ASTExprNodeP),
        (Sym, ASTPath)
    );

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
    ::std::vector<std::unique_ptr<ASTNamed<ASTItem>>> mItems;

private:
    // --- Runtime caches and state ---
    ::std::vector<::std::shared_ptr<ASTModule>> anonModules;

    ::std::vector<ASTNamed<MacroRulesPtr>> mMacros;

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
        ASTVisibility vis;
        ASTPath path;
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
        return mMacros;
    }

    const ASTNamedList<MacroRulesPtr>& macros() const {
        return mMacros;
    }
};

TAGGED_UNION_EX(
    ASTItem,
    (),
    None,
    ((None, struct {}),
     (MacroInv, ASTMacroInvocation),
     // TODO: MacroDefinition
     (Use, ASTUseItem),

     // Nameless items
     (ExternBlock, ASTExternBlock),
     (GlobalAsm, ASTGlobalAsm),
     (Impl, ASTImpl),
     (NegImpl, ASTImplDef),

     (Macro, MacroRulesPtr),
     (Module, ASTModule),
     (Crate, struct { RcString name; }),

     (Type, ASTTypeAlias),
     (Struct, ASTStruct),
     (Enum, ASTEnum),
     (Union, ASTUnion),
     (Trait, ASTTrait),
     (TraitAlias, ASTTraitAlias),

     (Function, ASTFunction),
     (Static, ASTStatic)),

    (),
    (),
    (ASTItem clone() const;)
);
