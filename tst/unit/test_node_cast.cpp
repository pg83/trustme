#include "common.h"

#include <cassert>

namespace {
    struct Node {
        virtual ~Node() = default;
        virtual unsigned int node_kind() const = 0;
    };

    struct First: Node {
        static constexpr unsigned int kind = 1;
        unsigned int node_kind() const override { return kind; }
    };

    struct Second: Node {
        static constexpr unsigned int kind = 2;
        unsigned int node_kind() const override { return kind; }
    };
}

int main() {
    First first;
    Node* node = &first;
    const Node* const_node = &first;

    assert(cast<First>(node) == &first);
    assert(cast<Second>(node) == nullptr);
    assert(cast<const First>(const_node) == &first);
    assert(cast<const Second>(const_node) == nullptr);
    assert(cast<First>(static_cast<Node*>(nullptr)) == nullptr);
}
