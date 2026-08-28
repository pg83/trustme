#include "tagged_union_sample.h"

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
