#pragma once

#include <vector>
#include <memory>
#include <string>
#include "tagged_union.h"
#include "ident.h"
#include "ast_path.h"
#include "ast_macro.h"
#include "floats.h"

namespace AST {

    using ::std::move;
    using ::std::unique_ptr;
    class MacroInvocation;

    class PatternBinding {
    public:
        enum class Type {
            MOVE,
            REF,
            MUTREF,
        };
        Ident mName;
        Type mType;
        bool isMutable;
        unsigned int slot;

        PatternBinding();

        PatternBinding(Ident name, Type ty, bool ismut);

        PatternBinding(PatternBinding&& x) = default;
        PatternBinding(const PatternBinding& x) = default;
        PatternBinding& operator=(PatternBinding&& x) = default;

        bool isValid() const {
            return mName.name != "";
        }
    };

    struct StructPatternEntry;

    class Pattern {
    public:
        TAGGED_UNION(
            Value,
            Invalid,
            (Invalid, struct {}),
            (Integer,
             struct {
                 enum eCoreType type;
                 U128 value; // Signed numbers are encoded as 2's complement
             }),
            (Float,
             struct {
                 enum eCoreType type;
                 FloatValue value;
             }),
            (String, ::std::string),
            (ByteString, struct { ::std::string v; }),
            (Named, Path)
        );

        struct TuplePat {
            ::std::vector<Pattern> start;
            bool hasWildcard;
            ::std::vector<Pattern> end;
        };

        TAGGED_UNION(
            Data,
            Any,
            (MaybeBind, struct { Ident name; }),
            (Macro, struct { unique_ptr<::AST::MacroInvocation> inv; }),
            (Any, struct {}),
            (Box, struct { unique_ptr<Pattern> sub; }),
            (Ref,
             struct {
                 bool mut;
                 unique_ptr<Pattern> sub;
             }),
            (Value,
             struct {
                 Value start;
                 Value end;
             }),
            (ValueLeftInc,
             struct {
                 Value start;
                 Value end;
             }),
            (Tuple, TuplePat),
            (StructTuple,
             struct {
                 Path path;
                 TuplePat tupPat;
             }),
            (Struct,
             struct {
                 Path path;
                 ::std::vector<StructPatternEntry> subPatterns;
                 bool isExhaustive;
             }),
            (Slice, struct { ::std::vector<Pattern> subPats; }),
            (SplitSlice,
             struct {
                 ::std::vector<Pattern> leading;
                 PatternBinding extraBind;
                 ::std::vector<Pattern> trailing;
             }),
            (Or, std::vector<Pattern>)
        );

    private:
        Span mSpan;
        std::vector<PatternBinding> mBindings;
        Data mData;

    public:
        virtual ~Pattern();

        Pattern();

        Pattern(Pattern&&) = default;
        Pattern& operator=(Pattern&&) = default;

        Pattern(Span sp, Data dat);;

        struct TagMaybeBind {};

        Pattern(TagMaybeBind, Span sp, Ident name);

        struct TagMacro {};

        Pattern(TagMacro, Span sp, unique_ptr<::AST::MacroInvocation> inv);

        struct TagBind {};

        Pattern(TagBind, Span sp, Ident name, PatternBinding::Type ty = PatternBinding::Type::MOVE, bool isMut = false);

        struct TagBox {};

        Pattern(TagBox, Span sp, Pattern sub);

        struct TagValue {};

        Pattern(TagValue, Span sp, Value val, Value end = Value());

        struct TagReference {};

        Pattern(TagReference, Span sp, bool isMutable, Pattern subPattern);

        struct TagTuple {};

        Pattern(TagTuple, Span sp, ::std::vector<Pattern> pats);

        Pattern(TagTuple, Span sp, TuplePat pat);

        struct TagNamedTuple {};

        Pattern(TagNamedTuple, Span sp, Path path, ::std::vector<Pattern> pats);

        Pattern(TagNamedTuple, Span sp, Path path, TuplePat pat = TuplePat{{}, false, {}});

        struct TagStruct {};

        Pattern(TagStruct, Span sp, Path path, ::std::vector<StructPatternEntry> subPatterns, bool isExhaustive);

        const Span& span() const {
            return mSpan;
        }

        Pattern clone() const;

        // Accessors
        std::vector<PatternBinding>& bindings() {
            return mBindings;
        }

        const std::vector<PatternBinding>& bindings() const {
            return mBindings;
        }

        Data& data() {
            return mData;
        }

        const Data& data() const {
            return mData;
        }

        Path& path() {
            return mData.as_StructTuple().path;
        }

        const Path& path() const {
            return mData.as_StructTuple().path;
        }

        friend ::std::ostream& operator<<(::std::ostream& os, const Pattern& pat);
    };

    struct StructPatternEntry {
        AttributeList attrs;
        RcString name;
        Pattern pat;
    };

    extern ::std::ostream& operator<<(::std::ostream& os, const Pattern::Value& val);
    extern ::std::ostream& operator<<(::std::ostream& os, const Pattern::TuplePat& val);

};
