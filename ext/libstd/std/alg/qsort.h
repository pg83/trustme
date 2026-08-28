#pragma once

#include "xchg.h"

#include <std/rng/pcg.h>
#include <std/lib/vector.h>
#include <std/typ/support.h>

namespace stl::QSP {
    template <typename I, typename C>
    struct Context {
        struct WorkItem {
            I b;
            I e;
        };

        C& f;
        Vector<WorkItem> w;
        PCG32 r;

        Context(C& _f, void* seed) noexcept
            : f(_f)
            , r(seed) {
        }

        void insertionSort(I b, I e) {
            for (auto i = b + 1; i != e; ++i) {
                for (auto j = i; j != b && f(*j, *(j - 1)); --j) {
                    xchg(*j, *(j - 1));
                }
            }
        }

        auto chooseRandom(I b, I e) noexcept {
            return b + r.uniformBiased(e - b);
        }

        auto partitionHoare(I b, I e) {
            for (auto p = e;; ++b) {
                while (b != e && f(*b, *p)) {
                    ++b;
                }

                while (b != e && f(*p, *--e)) {
                }

                if (b == e) {
                    return b;
                }

                xchg(*b, *e);
            }
        }

        bool alreadySorted(I b, I e) {
            for (++b; b != e; ++b) {
                if (f(*b, *(b - 1))) {
                    return false;
                }
            }

            return true;
        }

        void qSortStep(I b, I e) {
            if (e - b < 16) {
                return;
            }

            if (alreadySorted(b, e)) {
                return;
            }

            auto l = e - 1;

            xchg(*chooseRandom(b, e), *l);

            auto p = partitionHoare(b, l);

            xchg(*p, *l);

            w.pushBack(WorkItem{p + 1, e});
            w.pushBack(WorkItem{b, p});
        }

        void qSortLoop() {
            while (!w.empty()) {
                auto item = w.popBack();

                qSortStep(item.b, item.e);
            }
        }

        void qSort(I b, I e) {
            if (b != e) {
                qSortStep(b, e);
                qSortLoop();
                insertionSort(b, e);
            }
        }
    };
}

namespace stl {
    template <typename I, typename C>
    void quickSort(I b, I e, C&& f) {
        if (b != e) {
            QSP::Context<I, C>(f, (void*)&*b).qSort(b, e);
        }
    }

    template <typename I>
    void quickSort(I b, I e) {
        return quickSort(b, e, [](const auto& x, const auto& y) {
            return x < y;
        });
    }

    template <typename R, typename C>
    void quickSort(R&& r, C&& f) {
        return quickSort(r.begin(), r.end(), stl::forward<C>(f));
    }

    template <typename R>
    void quickSort(R&& r) {
        return quickSort(r.begin(), r.end());
    }
}
