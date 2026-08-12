#pragma once

#include <string>
#include <memory>
#include "span.h"
#include "slice.h"
#include "ast_item.h"
#include "ast_expr.h"

class TypeRef;

namespace AST {
    class Crate;
    class Attribute;
    class Path;

    struct StructItem;
    struct TupleItem;
    struct EnumVariant;

    class Module;
    class Item;

    class Expr;
    class ExprNode;
    struct ExprNodeMatchArm;

    class ImplDef;
    class Impl;
}

enum class AttrStage {
    Pre,
    Post,
};

class ExpandDecorator {
    void unexpected(const Span& sp, const AST::Attribute& mi, const char* locStr) const;

public:
    virtual ~ExpandDecorator() = default;
    virtual AttrStage stage() const = 0;

    virtual bool runDuringIter() const {
        return false;
    }

    // Whether `handle` should receive the item's full attribute list instead of only the
    // attributes written after the invoking one (derive macros need the full set).
    virtual bool wants_all_attrs() const {
        return false;
    }

    virtual void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate) const {
        unexpected(sp, mi, "crate");
    }

    virtual void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, const AST::AbsolutePath& path, AST::Module& mod, size_t modIdx, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const {
        unexpected(sp, mi, "item");
    }

    virtual void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, AST::Impl& impl, const RcString& name, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const {
        unexpected(sp, mi, "associated item");
    }

    virtual void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, const AST::AbsolutePath& path, AST::Trait& trait, slice<const AST::Attribute> attrs, AST::Item& i) const {
        unexpected(sp, mi, "trait item");
    }

    // NOTE: To delete, clear the name
    virtual void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::StructItem& si) const {
        unexpected(sp, mi, "struct item");
    }

    // NOTE: To delete, make the type invalid
    virtual void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::TupleItem& si) const {
        unexpected(sp, mi, "tuple item");
    }

    // NOTE: To delete, clear the name
    virtual void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::EnumVariant& ev) const {
        unexpected(sp, mi, "enum variant");
    }

    virtual void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::ExprNodeP& expr) const {
        unexpected(sp, mi, "expression");
    }

    // NOTE: To delete, clear the patterns vector
    virtual void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::ExprNodeMatchArm& expr) const {
        unexpected(sp, mi, "match arm");
    }

    // NOTE: To delete, clear the value
    virtual void handle(const Span& sp, const AST::Attribute& mi, AST::Crate& crate, ::AST::ExprNodeStructLiteral::Ent& expr) const {
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
