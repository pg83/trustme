#pragma once

#include "ast_attrs.h"
#include "macro_rules_macro_rules.h"

#include <std/mem/obj_pool.h>

class HIRProcMacro;

class ASTCrate;
struct WireBoard;
class ASTModule;
class ASTPath;
class ExpandProcMacro;
class ExpandDecorator;

class ExpandRegistry {
    struct DecoratorEntry {
        const char* name;
        ExpandDecorator* handler;
        DecoratorEntry* next;
    };

    struct MacroEntry {
        const char* name;
        ExpandProcMacro* handler;
        MacroEntry* next;
    };

    stl::ObjPool* pool;
    DecoratorEntry* decorators = nullptr;
    MacroEntry* macros = nullptr;

    void addDecorator(const char* name, ExpandDecorator* handler);
    void addMacro(const char* name, ExpandProcMacro* handler);

public:
    explicit ExpandRegistry(stl::ObjPool* pool)
        : pool(pool)
    {
    }

    template <typename T, typename... A>
    void addDecorator(const char* name, A&&... args) {
        addDecorator(name, pool->make<T>(stl::forward<A>(args)...));
    }

    template <typename T, typename... A>
    void addMacro(const char* name, A&&... args) {
        addMacro(name, pool->make<T>(stl::forward<A>(args)...));
    }

    template <typename T, typename... A>
    T* make(A&&... args) {
        return pool->make<T>(stl::forward<A>(args)...);
    }

    stl::ObjPool* objectPool() const {
        return pool;
    }

    ExpandProcMacro* findMacro(const RcString& name) const;
    ExpandDecorator* findDecorator(const RcString& name) const;

    template <typename F>
    void eachDecorator(F f) const {
        for (auto* entry = decorators; entry; entry = entry->next) {
            f(entry->name, *entry->handler);
        }
    }

    template <typename F>
    void eachMacro(F f) const {
        for (auto* entry = macros; entry; entry = entry->next) {
            f(entry->name, *entry->handler);
        }
    }
};

#include "expand_common_tu.h"
MacroRef ExpandLookupMacro(const Span& miSpan, const WireBoard& wb, const ASTCrate& crate, LList<ASTModule*> modstack, const ASTAttributeName& path);
MacroRef ExpandLookupMacro(const Span& miSpan, const WireBoard& wb, const ASTCrate& crate, LList<ASTModule*> modstack, const ASTPath& path);

ExpandProcMacro* ExpandFindProcMacro(const WireBoard& wb, const RcString& name);
ExpandDecorator* ExpandFindDecorator(const WireBoard& wb, const RcString& name);
void ExpandInit(ExpandRegistry& registry);
void Expand(const WireBoard& wb, ASTCrate& crate);
ASTExprNode* ExpandParseAndExpandExprVal(const ASTCrate& crate, ASTModule& mod, TokenStream& lex);
ASTExprNode* ExpandBareExpr(const WireBoard& wb, const ASTCrate& crate, ASTModule& mod, ASTExprNode* node);
