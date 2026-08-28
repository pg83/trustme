#pragma once

#include <std/sys/types.h>
#include <std/lib/visitor.h>

namespace stl {
    class ObjPool;

    struct PollFD;
    struct Pollable;

    struct PollerIface {
        virtual void arm(PollFD pfd) = 0;

        virtual void disarm(int fd) = 0;

        virtual void waitImpl(VisitorFace& v, u32 timeoutUs) = 0;

        void waitBase(VisitorFace&& v, u64 deadlineUs);

        template <typename V>
        void wait(V v, u64 deadlineUs) {
            waitBase(
                makeVisitor([v](void* ptr) {
                v((PollFD*)ptr);
            }),
                deadlineUs
            );
        }

        static PollerIface* create(ObjPool* pool);
        static PollerIface* createMultishot(ObjPool* pool, PollerIface* slave);
    };

    struct WaitablePoller: public PollerIface {
        virtual int fd() = 0;

        static WaitablePoller* create(ObjPool* pool);
        static WaitablePoller* create(ObjPool* pool, Pollable* reactor);
    };
}
