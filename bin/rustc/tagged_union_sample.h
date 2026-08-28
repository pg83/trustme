#pragma once

#include <std/sys/types.h>

#include <memory>
#include <string>

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
        : value(o.value) {
        o.value = -1;
        liveCount++;
    }

    ~SampleCounted() {
        liveCount--;
    }
};

struct SampleTreeNode;

#include "tagged_union_sample_tu.h"

struct SampleTreeNode {
    int value = 0;
    SampleTree left;
    SampleTree right;

    SampleTreeNode clone() const;
};
