#pragma once

#include "ident.h"
#include "floats.h"
#include "ast_path.h"
#include "ast_macro.h"
#include "tagged_union.h"

#include <memory>
#include <string>
#include <vector>

using ::std::move;
using ::std::unique_ptr;
class ASTMacroInvocation;

class ASTPatternBinding {
public:
    enum class Type {
        MOVE,
        REF,
        MUTREF,
    };
    Ident name;
    Type type;
    bool isMutable;
    unsigned int slot;

    ASTPatternBinding();

    ASTPatternBinding(Ident name, Type ty, bool ismut);

    ASTPatternBinding(ASTPatternBinding&& x) = default;
    ASTPatternBinding(const ASTPatternBinding& x) = default;
    ASTPatternBinding& operator=(ASTPatternBinding&& x) = default;

    bool isValid() const {
        return name.name != "";
    }
};

struct ASTStructPatternEntry;

class ASTPattern {
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
        (Named, ASTPath)
    );

    struct TuplePat {
        ::std::vector<ASTPattern> start;
        bool hasWildcard;
        ::std::vector<ASTPattern> end;
    };

    TAGGED_UNION(
        Data,
        Any,
        (MaybeBind, struct { Ident name; }),
        (Macro, struct { unique_ptr<ASTMacroInvocation> inv; }),
        (Any, struct {}),
        (Box, struct { unique_ptr<ASTPattern> sub; }),
        (Deref, struct { unique_ptr<ASTPattern> sub; }),
        (Ref,
         struct {
             bool mut;
             unique_ptr<ASTPattern> sub;
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
             ASTPath path;
             TuplePat tupPat;
         }),
        (Struct,
         struct {
             ASTPath path;
             ::std::vector<ASTStructPatternEntry> subPatterns;
             bool isExhaustive;
         }),
        (Slice, struct { ::std::vector<ASTPattern> subPats; }),
        (SplitSlice,
         struct {
             ::std::vector<ASTPattern> leading;
             ASTPatternBinding extraBind;
             ::std::vector<ASTPattern> trailing;
             /// A second `..`, which parses but means nothing. Only code that is
             /// kept has to be rejected, so the diagnostic waits for lowering.
             bool extraRest = false;
         }),
        (Or, std::vector<ASTPattern>)
    );

private:
    Span span_;
    std::vector<ASTPatternBinding> bindings_;
    Data data_;

public:
    virtual ~ASTPattern();

    ASTPattern();

    ASTPattern(ASTPattern&&) = default;
    ASTPattern& operator=(ASTPattern&&) = default;

    ASTPattern(Span sp, Data dat);
    ;

    struct TagMaybeBind {};

    ASTPattern(TagMaybeBind, Span sp, Ident name);

    struct TagMacro {};

    ASTPattern(TagMacro, Span sp, unique_ptr<ASTMacroInvocation> inv);

    struct TagBind {};

    ASTPattern(TagBind, Span sp, Ident name, ASTPatternBinding::Type ty = ASTPatternBinding::Type::MOVE, bool isMut = false);

    struct TagBox {};

    ASTPattern(TagBox, Span sp, ASTPattern sub);

    struct TagDeref {};

    ASTPattern(TagDeref, Span sp, ASTPattern sub);

    struct TagValue {};

    ASTPattern(TagValue, Span sp, Value val, Value end = Value());

    struct TagReference {};

    ASTPattern(TagReference, Span sp, bool isMutable, ASTPattern subPattern);

    struct TagTuple {};

    ASTPattern(TagTuple, Span sp, ::std::vector<ASTPattern> pats);

    ASTPattern(TagTuple, Span sp, TuplePat pat);

    struct TagNamedTuple {};

    ASTPattern(TagNamedTuple, Span sp, ASTPath path, ::std::vector<ASTPattern> pats);

    ASTPattern(TagNamedTuple, Span sp, ASTPath path, TuplePat pat = TuplePat{{}, false, {}});

    struct TagStruct {};

    ASTPattern(TagStruct, Span sp, ASTPath path, ::std::vector<ASTStructPatternEntry> subPatterns, bool isExhaustive);

    const Span& span() const {
        return span_;
    }

    ASTPattern clone() const;

    // Accessors
    std::vector<ASTPatternBinding>& bindings() {
        return bindings_;
    }

    const std::vector<ASTPatternBinding>& bindings() const {
        return bindings_;
    }

    Data& data() {
        return data_;
    }

    const Data& data() const {
        return data_;
    }

    ASTPath& path() {
        return data_.as_StructTuple().path;
    }

    const ASTPath& path() const {
        return data_.as_StructTuple().path;
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const ASTPattern& pat);
};

struct ASTStructPatternEntry {
    ASTAttributeList attrs;
    RcString name;
    ASTPattern pat;
};

extern ::std::ostream& operator<<(::std::ostream& os, const ASTPattern::Value& val);
extern ::std::ostream& operator<<(::std::ostream& os, const ASTPattern::TuplePat& val);
extern Ordering ord(const ASTPattern& a, const ASTPattern& b);
