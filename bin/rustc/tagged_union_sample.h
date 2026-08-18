#pragma once

// Context header for the tu_gen.py sample unions: the client header includes
// everything the payload types need, then includes the generated header.
// This is the contract every .tu consumer follows — the generated header
// itself includes nothing.
#include <cstdint>
#include <memory>
#include <string>

// Instance-counting payload: the unit tests use it to pin down exactly when
// generated code runs constructors and destructors.
struct SampleCounted {
    static int liveCount;
    int value;

    SampleCounted()
        : value(0)
    {
        liveCount++;
    }
    explicit SampleCounted(int v)
        : value(v)
    {
        liveCount++;
    }
    SampleCounted(const SampleCounted& o)
        : value(o.value)
    {
        liveCount++;
    }
    SampleCounted(SampleCounted&& o) noexcept
        : value(o.value)
    {
        o.value = -1;
        liveCount++;
    }
    ~SampleCounted() {
        liveCount--;
    }
};

// Only declared here: SampleTree is generated with allow_incomplete, so its
// header only needs the name.  The full definition follows the include and
// holds the union by value — direct recursion.
struct SampleTreeNode;

#include "tagged_union_sample_tu.h"

struct SampleTreeNode {
    int value = 0;
    SampleTree left;
    SampleTree right;

    SampleTreeNode clone() const;
};
