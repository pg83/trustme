#include "tagged_union_sample.h"

int SampleCounted::liveCount = 0;

// Body for the `extra` declaration the .tu adds to SampleValue: extra members
// are declared in the generated class, implemented by hand next to the spec.
int SampleValue::extraProbe() const {
    return static_cast<int>(tag()) * 100 + flags;
}
