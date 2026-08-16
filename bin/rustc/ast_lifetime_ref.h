#pragma once

#include "ident.h"
#include "common.h"

class ASTLifetimeRef {
public:
    // NOTE: These (the first three) must match HIR::LifetimeRef's versions
    static const uint16_t BINDING_STATIC = 0xFFFF;      // 'static
    static const uint16_t BINDING_UNSPECIFIED = 0xFFFE; // <unspec>
    static const uint16_t BINDING_INFER = 0xFFFD;       // '_
    static const uint16_t BINDING_UNBOUND = 0xFFFC;

private:
    Ident name_;
    uint16_t binding_;

    ASTLifetimeRef(Ident name, uint32_t binding);

public:
    ASTLifetimeRef();

    ASTLifetimeRef(Ident name);

    static ASTLifetimeRef newStatic() {
        return ASTLifetimeRef("static", BINDING_STATIC);
    }

    static ASTLifetimeRef newInfer() {
        return ASTLifetimeRef("_", BINDING_INFER);
    }

    void setBinding(uint16_t b);

    bool isUnbound() const {
        return binding_ == BINDING_UNBOUND;
    }

    bool isInfer() const {
        return binding_ == BINDING_INFER;
    }

    const Ident& name() const {
        return name_;
    }

    uint16_t binding() const {
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

    friend ::std::ostream& operator<<(::std::ostream& os, const ASTLifetimeRef& x);
};
