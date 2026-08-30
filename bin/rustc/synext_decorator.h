#pragma once

#include "span.h"
#include "slice.h"
#include "ast_expr.h"
#include "ast_item.h"

#include <memory>
#include <string>

struct ASTType;

class ASTCrate;
struct WireBoard;
class ExpandRegistry;
class ASTAttribute;
class ASTPath;

struct ASTStructItem;
struct ASTTupleItem;
struct ASTEnumVariant;

class ASTModule;
class ASTItem;

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

    virtual bool runDuringIter() const;

    virtual bool wantsAllAttrs() const;

    virtual void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const;

    virtual void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t modIdx, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const;

    virtual void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const;

    virtual void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const;

    virtual void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTStructItem& si) const;

    virtual void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTTupleItem& si) const;

    virtual void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTEnumVariant& ev) const;

    virtual ASTExprNode* handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNode* expr) const;

    virtual void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeMatchArm& expr) const;

    virtual void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeStructLiteral::Ent& expr) const;
};

void RegisterBuiltinDecorators(ExpandRegistry& registry);
