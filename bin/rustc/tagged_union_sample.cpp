#include "tagged_union_sample.h"

int SampleCounted::liveCount = 0;

// Body for the `extra` declaration the .tu adds to SampleValue: extra members
// are declared in the generated class, implemented by hand next to the spec.
int SampleValue::extraProbe() const {
    return static_cast<int>(tag()) * 100 + flags;
}

// The house clone() convention the generated SampleTree::clone() recurses
// through.
SampleTreeNode SampleTreeNode::clone() const {
    SampleTreeNode out;
    out.value = value;
    out.left = left.clone();
    out.right = right.clone();
    return out;
}
