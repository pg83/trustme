#include "common.h"

#include <cassert>

namespace {
    struct Node {
        virtual ~Node() = default;
        virtual unsigned int nodeKind() const = 0;
    };

    struct First: Node {
        static constexpr unsigned int kind = 1;
        unsigned int nodeKind() const override { return kind; }
    };

    struct Second: Node {
        static constexpr unsigned int kind = 2;
        unsigned int nodeKind() const override { return kind; }
    };
}

int main() {
    First first;
    Node* node = &first;
    const Node* constNode = &first;

    assert(cast<First>(node) == &first);
    assert(cast<Second>(node) == nullptr);
    assert(cast<const First>(constNode) == &first);
    assert(cast<const Second>(constNode) == nullptr);
    assert(cast<First>(static_cast<Node*>(nullptr)) == nullptr);
}
