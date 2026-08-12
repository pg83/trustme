#pragma once

#include <memory>
#include <vector>
#include "int128.h"
#include "floats.h"
#include "tagged_union.h"
#include "hir_path.h"
#include "hir_type.h"

namespace HIR {

    class Struct;
    class Union;
    class Enum;
    class Constant;

    struct PatternBinding {
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

        PatternBinding();

        PatternBinding(bool mut, Type type, RcString name, unsigned int slot);

        friend ::std::ostream& operator<<(::std::ostream& os, const PatternBinding& x);
    };

    enum class PatternBindingOrder {
        Declaration,
        FirstCandidate,
        LastCandidate,
    };

    struct Pattern {
        TAGGED_UNION(
            Value,
            String,
            (Integer,
             struct {
                 ::HIR::CoreType type; // Str == _
                 U128 value;           // Signed numbers are encoded as 2's complement
             }),
            (Float,
             struct {
                 ::HIR::CoreType type; // Str == _
                 FloatValue value;
             }),
            (String, ::std::string),
            (ByteString, struct { ::std::string v; }),
            (Named, struct {
                Path path;
                const ::HIR::Constant* binding;
            })
        );
        friend ::std::ostream& operator<<(::std::ostream& os, const Pattern::Value& x);

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
             (Struct, const Struct*),
             (Union, const Union*),
             (Enum,
              struct {
                  const Enum* ptr;
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
            (Box, struct { ::std::unique_ptr<Pattern> sub; }),
            (Ref,
             struct {
                 ::HIR::BorrowType type;
                 ::std::unique_ptr<Pattern> sub;
             }),
            (Tuple, struct { ::std::vector<Pattern> subPatterns; }),
            (SplitTuple,
             struct {
                 ::std::vector<Pattern> leading;
                 ::std::vector<Pattern> trailing;
                 unsigned int totalSize;
             }),
            // Maybe refutable
            // - Can be converted into `Value`, or resolved to be an enum/struct value
            (PathValue,
             struct {
                 ::HIR::Path path;
                 PathBinding binding;
             }),
            // - Tuple-like enum/struct value
            (PathTuple,
             struct {
                 ::HIR::Path path;
                 PathBinding binding;
                 ::std::vector<Pattern> leading;
                 bool isSplit;
                 ::std::vector<Pattern> trailing;
                 // Cache making MIR gen easier for split patterns
                 unsigned int totalSize;
             }),
            // - Struct-like enum/struct value
            (PathNamed,
             struct {
                 ::HIR::Path path;
                 PathBinding binding;

                 ::std::vector<::std::pair<RcString, Pattern>> subPatterns;
                 bool isExhaustive;

                 bool isWildcard() const {
                     return subPatterns.empty() && !isExhaustive;
                 }
             }),
            // Split/or patterns
            (Or, std::vector<Pattern>),
            // Always refutable
            (Value, struct { Value val; }),
            (Range,
             struct {
                 std::unique_ptr<Value> start;
                 std::unique_ptr<Value> end;
                 bool isInclusive;
             }),
            (Slice, struct { ::std::vector<Pattern> subPatterns; }),
            (SplitSlice, struct {
                ::std::vector<Pattern> leading;
                PatternBinding extraBind;
                ::std::vector<Pattern> trailing;
            })
        );

        std::vector<PatternBinding> mBindings;
        Data mData;
        unsigned implicitDerefCount = 0;

        Pattern();

        Pattern(std::vector<PatternBinding> pbs, Data d);

        Pattern(PatternBinding pb, Data d);

        Pattern(const Pattern&) = delete;
        Pattern(Pattern&&) = default;
        Pattern& operator=(const Pattern&) = delete;
        Pattern& operator=(Pattern&&) = default;

        Pattern clone() const;

        friend ::std::ostream& operator<<(::std::ostream& os, const Pattern& x);
    };

    std::vector<unsigned> patternBindingSlots(const Pattern& pattern, PatternBindingOrder order);

}
