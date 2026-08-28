#pragma once

#include "floats.h"
#include "int128.h"
#include "hir_path.h"
#include "hir_type.h"

#include <memory>
#include <vector>

class HIRStruct;
class HIRUnion;
class HIREnum;
class HIRConstant;

struct HIRPatternBinding {
    enum class Type {
        Move,
        Ref,
        MutRef,
    };

    bool isMutable;
    Type type;
    RcString name;
    unsigned int slot;

    unsigned implicitDerefCount = 0;

    bool isValid() const {
        return name != "";
    }

    HIRPatternBinding();

    HIRPatternBinding(bool mut, Type type, RcString name, unsigned int slot);

    friend ::std::ostream& operator<<(::std::ostream& os, const HIRPatternBinding& x);
};

enum class HIRPatternBindingOrder {
    Declaration,
    FirstCandidate,
    LastCandidate,
};

enum class HIRPatternDerefKind {
    Unknown,
    Box,
    Shared,
    Unique,
};

struct HIRPattern;

#include "hir_pattern_value_tu.h"

::std::ostream& operator<<(::std::ostream& os, const HIRPatternValue& x);

struct HIRPatternPathNamedData {
    HIRPath path;
    HIRPatternPathBinding binding;

    ::std::vector<::std::pair<RcString, HIRPattern>> subPatterns;
    bool isExhaustive;

    bool isWildcard() const;
};

#include "hir_pattern_tu.h"

struct HIRPattern {
    using DerefKind = HIRPatternDerefKind;
    using Value = HIRPatternValue;
    using PathBinding = HIRPatternPathBinding;
    using Data = HIRPatternData;

    enum class GlobPos {
        None,
        Start,
        End,
    };

    std::vector<HIRPatternBinding> bindings;
    Data data;
    unsigned implicitDerefCount = 0;

    HIRPattern();

    HIRPattern(std::vector<HIRPatternBinding> pbs, Data d);

    HIRPattern(HIRPatternBinding pb, Data d);

    HIRPattern(const HIRPattern&) = delete;
    HIRPattern(HIRPattern&&) = default;
    HIRPattern& operator=(const HIRPattern&) = delete;
    HIRPattern& operator=(HIRPattern&&) = default;

    HIRPattern clone() const;

    friend ::std::ostream& operator<<(::std::ostream& os, const HIRPattern& x);
};

inline bool HIRPatternPathNamedData::isWildcard() const {
    return subPatterns.empty() && !isExhaustive;
}

std::vector<unsigned> patternBindingSlots(const HIRPattern& pattern, HIRPatternBindingOrder order);
