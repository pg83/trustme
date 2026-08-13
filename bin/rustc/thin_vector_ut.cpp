#include "thin_vector.h"

#include <std/tst/ut.h>

#include <vector>
#include <string>
#include <sstream>

using namespace stl;

namespace {
    // Instance-counting element: lets tests assert that every constructed value
    // is destroyed exactly once (no leaks, no double-frees).
    struct Counted {
        static int liveCount;
        int value;

        Counted()
            : value(0)
        {
            liveCount++;
        }
        explicit Counted(int v)
            : value(v)
        {
            liveCount++;
        }
        Counted(const Counted& o)
            : value(o.value)
        {
            liveCount++;
        }
        Counted(Counted&& o)
            : value(o.value)
        {
            o.value = -1;
            liveCount++;
        }
        Counted& operator=(const Counted& o) {
            value = o.value;
            return *this;
        }
        Counted& operator=(Counted&& o) {
            value = o.value;
            o.value = -1;
            return *this;
        }
        ~Counted() {
            liveCount--;
        }
    };
    int Counted::liveCount = 0;

    ::std::ostream& operator<<(::std::ostream& os, const Counted& c) {
        return os << c.value;
    }
    Ordering ord(const Counted& a, const Counted& b) {
        return ::ord(a.value, b.value);
    }
}

STD_TEST_SUITE(ThinVectorBasics) {
    STD_TEST(defaultIsEmpty) {
        ThinVector<int> v;
        STD_INSIST(v.empty());
        STD_INSIST(v.size() == 0);
        STD_INSIST(v.capacity() == 0);
        STD_INSIST(v.begin() == v.end());
        STD_INSIST(v.data() == nullptr);
    }

    STD_TEST(sizedConstructorDefaultInits) {
        // The sized constructor default-constructs each element (`new (p) T;`),
        // so trivial types are left indeterminate — use a type with a defined
        // default to observe that every slot was constructed exactly once.
        STD_INSIST(Counted::liveCount == 0);
        {
            ThinVector<Counted> v(4);
            STD_INSIST(!v.empty());
            STD_INSIST(v.size() == 4);
            STD_INSIST(v.capacity() >= 4);
            STD_INSIST(Counted::liveCount == 4);
            for (size_t i = 0; i < v.size(); i++) {
                STD_INSIST(v[i].value == 0);
            }
        }
        STD_INSIST(Counted::liveCount == 0);
    }

    STD_TEST(fromVectorAndRange) {
        std::vector<int> src{10, 20, 30};
        ThinVector<int> v(src);
        STD_INSIST(v.size() == 3);
        STD_INSIST(v[0] == 10 && v[1] == 20 && v[2] == 30);

        ThinVector<int> r(src.data(), src.data() + 2);
        STD_INSIST(r.size() == 2);
        STD_INSIST(r[0] == 10 && r[1] == 20);

        ThinVector<int> empty(src.data(), src.data());
        STD_INSIST(empty.empty());
    }

    STD_TEST(pushEmplacePop) {
        ThinVector<int> v;
        for (int i = 0; i < 10; i++) {
            v.push_back(i);
        }
        STD_INSIST(v.size() == 10);
        STD_INSIST(v.capacity() >= 10);
        STD_INSIST(v.front() == 0);
        STD_INSIST(v.back() == 9);
        for (int i = 0; i < 10; i++) {
            STD_INSIST(v[i] == i);
        }
        v.pop_back();
        STD_INSIST(v.size() == 9);
        STD_INSIST(v.back() == 8);

        ThinVector<std::string> s;
        s.emplace_back("hello");
        STD_INSIST(s.size() == 1);
        STD_INSIST(s[0] == "hello");
    }

    STD_TEST(reserveAndResize) {
        ThinVector<int> v;
        v.reserve(16);
        STD_INSIST(v.capacity() >= 16);
        STD_INSIST(v.size() == 0);
        // Reserving smaller must not shrink.
        v.reserve(4);
        STD_INSIST(v.capacity() >= 16);

        v.resize(5);
        STD_INSIST(v.size() == 5);
        for (size_t i = 0; i < 5; i++) {
            STD_INSIST(v[i] == 0);
        }
        v[2] = 99;
        v.resize(2);
        STD_INSIST(v.size() == 2);
        v.resize(4);
        STD_INSIST(v.size() == 4);
        STD_INSIST(v[2] == 0); // regrown slot is fresh, not the old 99
    }
}

STD_TEST_SUITE(ThinVectorSemantics) {
    STD_TEST(copyAndMoveConstruct) {
        ThinVector<int> a;
        a.push_back(1);
        a.push_back(2);
        a.push_back(3);

        ThinVector<int> b(a);
        STD_INSIST(b.size() == 3 && a.size() == 3);
        b[0] = 100;
        STD_INSIST(a[0] == 1); // deep copy, independent storage

        ThinVector<int> c(std::move(a));
        STD_INSIST(c.size() == 3);
        STD_INSIST(c[0] == 1 && c[2] == 3);
        STD_INSIST(a.empty()); // moved-from is empty
    }

    STD_TEST(copyAndMoveAssign) {
        ThinVector<int> a;
        a.push_back(7);
        a.push_back(8);

        ThinVector<int> b;
        b.push_back(1);
        b = a;
        STD_INSIST(b.size() == 2 && b[0] == 7 && b[1] == 8);
        b[0] = 0;
        STD_INSIST(a[0] == 7);

        ThinVector<int> c;
        c.push_back(42);
        c = std::move(a);
        STD_INSIST(c.size() == 2 && c[1] == 8);
        STD_INSIST(a.empty());
    }

    STD_TEST(destructorRunsForEveryElement) {
        STD_INSIST(Counted::liveCount == 0);
        {
            ThinVector<Counted> v;
            for (int i = 0; i < 20; i++) {
                v.push_back(Counted(i)); // growth reallocates; must not leak
            }
            STD_INSIST(v.size() == 20);
            STD_INSIST(Counted::liveCount == 20);

            ThinVector<Counted> copy(v);
            STD_INSIST(Counted::liveCount == 40);

            v.pop_back();
            STD_INSIST(Counted::liveCount == 39);

            v.resize(5); // drops 14
            STD_INSIST(Counted::liveCount == 25);
        }
        STD_INSIST(Counted::liveCount == 0); // all destroyed on scope exit
    }
}

STD_TEST_SUITE(ThinVectorAccessAndOrder) {
    STD_TEST(atThrowsOutOfRange) {
        ThinVector<int> v;
        v.push_back(5);
        STD_INSIST(v.at(0) == 5);
        bool threw = false;
        try {
            (void)v.at(1);
        } catch (const std::out_of_range&) {
            threw = true;
        }
        STD_INSIST(threw);
    }

    STD_TEST(frontBackThrowWhenEmpty) {
        ThinVector<int> v;
        bool threw = false;
        try {
            (void)v.front();
        } catch (const std::out_of_range&) {
            threw = true;
        }
        STD_INSIST(threw);
    }

    STD_TEST(iterationVisitsAllInOrder) {
        ThinVector<int> v;
        for (int i = 0; i < 5; i++) {
            v.push_back(i * i);
        }
        int expected = 0;
        int seen = 0;
        for (int x : v) {
            STD_INSIST(x == expected * expected);
            expected++;
            seen++;
        }
        STD_INSIST(seen == 5);
    }

    STD_TEST(ordComparesLexicographicallyThenLength) {
        ThinVector<int> a(std::vector<int>{1, 2, 3});
        ThinVector<int> b(std::vector<int>{1, 2, 3});
        ThinVector<int> c(std::vector<int>{1, 2, 4});
        ThinVector<int> shorter(std::vector<int>{1, 2});

        STD_INSIST(a.ord(b) == OrdEqual);
        STD_INSIST(a.ord(c) == OrdLess);
        STD_INSIST(c.ord(a) == OrdGreater);
        // Shorter prefix sorts before the longer list.
        STD_INSIST(shorter.ord(a) == OrdLess);
        STD_INSIST(a.ord(shorter) == OrdGreater);
    }

    STD_TEST(streamInsertion) {
        ThinVector<int> v(std::vector<int>{1, 2, 3});
        std::ostringstream os;
        os << v;
        STD_INSIST(os.str() == "1, 2, 3");

        ThinVector<int> empty;
        std::ostringstream os2;
        os2 << empty;
        STD_INSIST(os2.str().empty());
    }
}
