#pragma once

#include "span.h"
#include "slice.h"
#include "ast_expr.h"
#include "ast_item.h"

#include <memory>
#include <string>

class TypeRef;

class ASTCrate;
class ASTAttribute;
class ASTPath;

struct ASTStructItem;
struct ASTTupleItem;
struct ASTEnumVariant;

class ASTModule;
class ASTItem;

class ASTExpr;
class ASTExprNode;
struct ASTExprNodeMatchArm;

class ASTImplDef;
class ASTImpl;

enum class AttrStage {
    Pre,
    Post,
};

class ExpandDecorator {
    void unexpected(const Span& sp, const ASTAttribute& mi, const char* locStr) const;

public:
    virtual ~ExpandDecorator() = default;
    virtual AttrStage stage() const = 0;

    virtual bool runDuringIter() const {
        return false;
    }

    // Whether `handle` should receive the item's full attribute list instead of only the
    // attributes written after the invoking one (derive macros need the full set).
    virtual bool wantsAllAttrs() const {
        return false;
    }

    virtual void handle(const Span& sp, const ASTAttribute& mi, ASTCrate& crate) const {
        unexpected(sp, mi, "crate");
    }

    virtual void handle(const Span& sp, const ASTAttribute& mi, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t modIdx, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const {
        unexpected(sp, mi, "item");
    }

    virtual void handle(const Span& sp, const ASTAttribute& mi, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const {
        unexpected(sp, mi, "associated item");
    }

    virtual void handle(const Span& sp, const ASTAttribute& mi, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const {
        unexpected(sp, mi, "trait item");
    }

    // NOTE: To delete, clear the name
    virtual void handle(const Span& sp, const ASTAttribute& mi, ASTCrate& crate, ASTStructItem& si) const {
        unexpected(sp, mi, "struct item");
    }

    // NOTE: To delete, make the type invalid
    virtual void handle(const Span& sp, const ASTAttribute& mi, ASTCrate& crate, ASTTupleItem& si) const {
        unexpected(sp, mi, "tuple item");
    }

    // NOTE: To delete, clear the name
    virtual void handle(const Span& sp, const ASTAttribute& mi, ASTCrate& crate, ASTEnumVariant& ev) const {
        unexpected(sp, mi, "enum variant");
    }

    virtual void handle(const Span& sp, const ASTAttribute& mi, ASTCrate& crate, ASTExprNodeP& expr) const {
        unexpected(sp, mi, "expression");
    }

    // NOTE: To delete, clear the patterns vector
    virtual void handle(const Span& sp, const ASTAttribute& mi, ASTCrate& crate, ASTExprNodeMatchArm& expr) const {
        unexpected(sp, mi, "match arm");
    }

    // NOTE: To delete, clear the value
    virtual void handle(const Span& sp, const ASTAttribute& mi, ASTCrate& crate, ASTExprNodeStructLiteral::Ent& expr) const {
        unexpected(sp, mi, "struct literal ent");
    }
};

struct DecoratorDef;
extern void RegisterSynextDecorator(::std::string name, ::std::unique_ptr<ExpandDecorator> handler);
extern void RegisterSynextDecoratorStatic(DecoratorDef* def);

template <typename T>
void RegisterSynextDecoratorG(::std::string name) {
    RegisterSynextDecorator(mv$(name), ::std::unique_ptr<ExpandDecorator>(new T()));
}

struct DecoratorDef {
    DecoratorDef* prev;
    ::std::string name;
    ::std::unique_ptr<ExpandDecorator> def;

    DecoratorDef(::std::string name, ::std::unique_ptr<ExpandDecorator> def);
};

#define STATIC_DECORATOR(ident, _handler_class) static DecoratorDef s_register_##_handler_class(ident, ::std::unique_ptr<ExpandDecorator>(new _handler_class()));
