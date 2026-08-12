#pragma once

#include "floats.h"
#include "int128.h"
#include "hir_path.h"
#include "hir_type.h"
#include "tagged_union.h"

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
    Type mType;
    RcString mName;
    unsigned int slot;

    unsigned implicitDerefCount = 0;

    bool isValid() const {
        return mName != "";
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

struct HIRPattern {
    TAGGED_UNION(
        Value,
        String,
        (Integer,
         struct {
             HIRCoreType type; // Str == _
             U128 value;       // Signed numbers are encoded as 2's complement
         }),
        (Float,
         struct {
             HIRCoreType type; // Str == _
             FloatValue value;
         }),
        (String, ::std::string),
        (ByteString, struct { ::std::string v; }),
        (Named, struct {
            HIRPath path;
            const HIRConstant* binding;
        })
    );
    friend ::std::ostream& operator<<(::std::ostream& os, const HIRPattern::Value& x);

    enum class GlobPos {
        None,
        Start,
        End,
    };

    TAGGED_UNION_EX(
        PathBinding,
        (),
        Unbound,
        ((Unbound, struct {}),
         (Struct, const HIRStruct*),
         (Union, const HIRUnion*),
         (Enum,
          struct {
              const HIREnum* ptr;
              unsigned varIdx;
          })),
        (),
        (),
        (PathBinding clone() const {
                TU_MATCH_HDRA( (*this), {)
                TU_ARMA(Unbound, e) return e;
            TU_ARMA(Struct, e) return e;
            TU_ARMA(Union, e) return e;
            TU_ARMA(Enum, e) return e;
                }
                abort();
        };)
    );

    TAGGED_UNION(
        Data,
        Any,
        // Irrefutable / destructuring
        (Any, struct {}),
        (Box, struct { ::std::unique_ptr<HIRPattern> sub; }),
        (Ref,
         struct {
             HIRBorrowType type;
             ::std::unique_ptr<HIRPattern> sub;
         }),
        (Tuple, struct { ::std::vector<HIRPattern> subPatterns; }),
        (SplitTuple,
         struct {
             ::std::vector<HIRPattern> leading;
             ::std::vector<HIRPattern> trailing;
             unsigned int totalSize;
         }),
        // Maybe refutable
        // - Can be converted into `Value`, or resolved to be an enum/struct value
        (PathValue,
         struct {
             HIRPath path;
             PathBinding binding;
         }),
        // - Tuple-like enum/struct value
        (PathTuple,
         struct {
             HIRPath path;
             PathBinding binding;
             ::std::vector<HIRPattern> leading;
             bool isSplit;
             ::std::vector<HIRPattern> trailing;
             // Cache making MIR gen easier for split patterns
             unsigned int totalSize;
         }),
        // - Struct-like enum/struct value
        (PathNamed,
         struct {
             HIRPath path;
             PathBinding binding;

             ::std::vector<::std::pair<RcString, HIRPattern>> subPatterns;
             bool isExhaustive;

             bool isWildcard() const {
                 return subPatterns.empty() && !isExhaustive;
             }
         }),
        // Split/or patterns
        (Or, std::vector<HIRPattern>),
        // Always refutable
        (Value, struct { Value val; }),
        (Range,
         struct {
             std::unique_ptr<Value> start;
             std::unique_ptr<Value> end;
             bool isInclusive;
         }),
        (Slice, struct { ::std::vector<HIRPattern> subPatterns; }),
        (SplitSlice, struct {
            ::std::vector<HIRPattern> leading;
            HIRPatternBinding extraBind;
            ::std::vector<HIRPattern> trailing;
        })
    );

    std::vector<HIRPatternBinding> mBindings;
    Data mData;
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

std::vector<unsigned> patternBindingSlots(const HIRPattern& pattern, HIRPatternBindingOrder order);
