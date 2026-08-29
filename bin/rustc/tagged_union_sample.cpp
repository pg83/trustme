#include "tagged_union_sample.h"

#include "output.h"

using namespace stl;

int SampleCounted::liveCount = 0;

int SampleValue::extraProbe() const {
    return static_cast<int>(tag()) * 100 + flags;
}

SampleTreeNode SampleTreeNode::clone() const {
    SampleTreeNode out;
    out.value = value;
    out.left = left.clone();
    out.right = right.clone();
    return out;
}

template <>
void stl::output<ZeroCopyOutput, SampleCounted>(ZeroCopyOutput& out, const SampleCounted& value) {
    out << StringView("SampleCounted(value = ") << value.value << StringView(")");
}
