#pragma once

#include "common.h"
#include "ident.h"

namespace AST {
    class LifetimeRef {
    public:
        // NOTE: These (the first three) must match HIR::LifetimeRef's versions
        static const uint16_t BINDING_STATIC = 0xFFFF;      // 'static
        static const uint16_t BINDING_UNSPECIFIED = 0xFFFE; // <unspec>
        static const uint16_t BINDING_INFER = 0xFFFD;       // '_
        static const uint16_t BINDING_UNBOUND = 0xFFFC;

    private:
        Ident mName;
        uint16_t mBinding;

        LifetimeRef(Ident name, uint32_t binding);

    public:
        LifetimeRef();

        LifetimeRef(Ident name);

        static LifetimeRef new_static() {
            return LifetimeRef("static", BINDING_STATIC);
        }

        static LifetimeRef newInfer() {
            return LifetimeRef("_", BINDING_INFER);
        }

        void set_binding(uint16_t b);

        bool isUnbound() const {
            return mBinding == BINDING_UNBOUND;
        }

        bool isInfer() const {
            return mBinding == BINDING_INFER;
        }

        const Ident& name() const {
            return mName;
        }

        uint16_t binding() const {
            return mBinding;
        }

        Ordering ord(const LifetimeRef& x) const {
            return ::ord(mName.name, x.mName.name);
        }

        bool operator==(const LifetimeRef& x) const {
            return ord(x) == OrdEqual;
        }

        bool operator!=(const LifetimeRef& x) const {
            return ord(x) != OrdEqual;
        }

        bool operator<(const LifetimeRef& x) const {
            return ord(x) == OrdLess;
        };

        friend ::std::ostream& operator<<(::std::ostream& os, const LifetimeRef& x);
    };
}
