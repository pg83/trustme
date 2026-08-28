#pragma once

#include "ident.h"
#include "common.h"

class ASTLifetimeRef {
public:
    static const u16 BINDING_STATIC = 0xFFFF;
    static const u16 BINDING_UNSPECIFIED = 0xFFFE;
    static const u16 BINDING_INFER = 0xFFFD;
    static const u16 BINDING_UNBOUND = 0xFFFC;

private:
    Ident name_;
    u16 binding_;

    ASTLifetimeRef(Ident name, u32 binding);

public:
    ASTLifetimeRef();

    ASTLifetimeRef(Ident name);

    static ASTLifetimeRef newStatic() {
        return ASTLifetimeRef("static", BINDING_STATIC);
    }

    static ASTLifetimeRef newInfer() {
        return ASTLifetimeRef("_", BINDING_INFER);
    }

    void setBinding(u16 b);

    bool isUnbound() const {
        return binding_ == BINDING_UNBOUND;
    }

    bool isInfer() const {
        return binding_ == BINDING_INFER;
    }

    const Ident& name() const {
        return name_;
    }

    u16 binding() const {
        return binding_;
    }

    Ordering ord(const ASTLifetimeRef& x) const {
        return ::ord(name_.name, x.name_.name);
    }

    bool operator==(const ASTLifetimeRef& x) const {
        return ord(x) == OrdEqual;
    }

    bool operator!=(const ASTLifetimeRef& x) const {
        return ord(x) != OrdEqual;
    }

    bool operator<(const ASTLifetimeRef& x) const {
        return ord(x) == OrdLess;
    };

    friend std::ostream& operator<<(std::ostream& os, const ASTLifetimeRef& x);
};
