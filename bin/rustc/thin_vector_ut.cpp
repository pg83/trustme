#include "output.h"
#include "thin_vector.h"

#include <std/tst/ut.h>
#include <std/str/builder.h>

#include <string>
#include <vector>

using namespace stl;

template <>
void stl::output<ZeroCopyOutput, ThinVector<int>>(ZeroCopyOutput& out, const ThinVector<int>& value) {
    bool first = true;
    for (auto item : value) {
        if (!first) {
            out << StringView(", ");
        }
        first = false;
        out << item;
    }
}

namespace {
    struct Counted {
        static int liveCount;
        int value;

        Counted();
        explicit Counted(int v);
        Counted(const Counted& o);
        Counted(Counted&& o);
        Counted& operator=(const Counted& o);
        Counted& operator=(Counted&& o);
        ~Counted();
    };

    int Counted::liveCount = 0;

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
        STD_INSIST(v[2] == 0);
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
        STD_INSIST(a[0] == 1);

        ThinVector<int> c(std::move(a));
        STD_INSIST(c.size() == 3);
        STD_INSIST(c[0] == 1 && c[2] == 3);
        STD_INSIST(a.empty());
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
                v.push_back(Counted(i));
            }
            STD_INSIST(v.size() == 20);
            STD_INSIST(Counted::liveCount == 20);

            ThinVector<Counted> copy(v);
            STD_INSIST(Counted::liveCount == 40);

            v.pop_back();
            STD_INSIST(Counted::liveCount == 39);

            v.resize(5);
            STD_INSIST(Counted::liveCount == 25);
        }
        STD_INSIST(Counted::liveCount == 0);
    }
}

STD_TEST_SUITE(ThinVectorAccessAndOrder) {
    STD_TEST(atInBounds) {
        ThinVector<int> v;
        v.push_back(5);
        v.push_back(7);
        STD_INSIST(v.at(0) == 5);
        STD_INSIST(v.at(1) == 7);
        STD_INSIST(v.front() == 5);
        STD_INSIST(v.back() == 7);
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
        STD_INSIST(shorter.ord(a) == OrdLess);
        STD_INSIST(a.ord(shorter) == OrdGreater);
    }

    STD_TEST(streamInsertion) {
        ThinVector<int> v(std::vector<int>{1, 2, 3});
        StringBuilder os;
        os << v;
        STD_INSIST(std::string(static_cast<const char*>(os.data()), os.length()) == "1, 2, 3");

        ThinVector<int> empty;
        StringBuilder os2;
        os2 << empty;
        STD_INSIST(os2.empty());
    }
}

Counted::Counted()
    : value(0)
{
    liveCount++;
}

Counted::Counted(int v)
    : value(v)
{
    liveCount++;
}

Counted::Counted(const Counted& o)
    : value(o.value)
{
    liveCount++;
}

Counted::Counted(Counted&& o)
    : value(o.value)
{
    o.value = -1;
    liveCount++;
}

auto Counted::operator=(const Counted& o) -> Counted& {
    value = o.value;
    return *this;
}

auto Counted::operator=(Counted&& o) -> Counted& {
    value = o.value;
    o.value = -1;
    return *this;
}

Counted::~Counted() {
    liveCount--;
}

template <>
void stl::output<ZeroCopyOutput, Counted>(ZeroCopyOutput& os, const Counted& c) {
    os << c.value;
    return;
}
