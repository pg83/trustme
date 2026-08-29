#include "ast_pattern.h"
#include "output.h"

#include "common.h"
#include "ast_ast.h"
#include "ast_expr.h"









using namespace stl;

ASTPattern::~ASTPattern() {
}

bool PatternContainsNever(const ASTPattern& pat) {
    struct H {
        static bool any(const std::vector<ASTPattern>& list) {
            for (const auto& p : list) {
                if (PatternContainsNever(p)) {
                    return true;
                }
            }
            return false;
        }

        static bool tup(const ASTPattern::TuplePat& p) {
            return any(p.start) || any(p.end);
        }
    };

    switch (pat.data().tag()) {
        default:
            return false;
        case ASTPatternData::TAG_Never: {
            return true;
        }
        case ASTPatternData::TAG_Box: {
            auto& e = pat.data().as_Box();
            return PatternContainsNever(*e.sub);
        }
        case ASTPatternData::TAG_Deref: {
            auto& e = pat.data().as_Deref();
            return PatternContainsNever(*e.sub);
        }
        case ASTPatternData::TAG_Ref: {
            auto& e = pat.data().as_Ref();
            return PatternContainsNever(*e.sub);
        }
        case ASTPatternData::TAG_Guard: {
            auto& e = pat.data().as_Guard();
            return PatternContainsNever(*e.sub);
        }
        case ASTPatternData::TAG_Tuple: {
            auto& e = pat.data().as_Tuple();
            return H::tup(e);
        }
        case ASTPatternData::TAG_StructTuple: {
            auto& e = pat.data().as_StructTuple();
            return H::tup(e.tupPat);
        }
        case ASTPatternData::TAG_Struct: {
            auto& e = pat.data().as_Struct();
            for (const auto& sp : e.subPatterns) {
                if (PatternContainsNever(sp.pat)) {
                    return true;
                }
            }
            return false;
        }
        case ASTPatternData::TAG_Slice: {
            auto& e = pat.data().as_Slice();
            return H::any(e.subPats);
        }
        case ASTPatternData::TAG_SplitSlice: {
            auto& e = pat.data().as_SplitSlice();
            return H::any(e.leading) || H::any(e.trailing);
        }
        case ASTPatternData::TAG_Or: {
            auto& e = pat.data().as_Or();
            return H::any(e);
        }
    }
    return false;
}

ASTPattern::ASTPattern(TagStruct, Span sp, ASTPath path, std::vector<ASTStructPatternEntry> subPatterns, bool isExhaustive)
    : span_(mv$(sp))
    , data_(Data::make_Struct({std::move(path), std::move(subPatterns), isExhaustive}))
{
}

ASTPattern ASTPattern::clone() const {
    ASTPattern rv;
    rv.span_ = span_;
    for (const auto& pb : bindings_) {
        rv.bindings_.push_back(pb);
    }

    struct H {
        static std::unique_ptr<ASTPattern> cloneSp(const std::unique_ptr<ASTPattern>& p) {
            return std::make_unique<ASTPattern>(p->clone());
        }

        static std::vector<ASTPattern> cloneList(const std::vector<ASTPattern>& list) {
            std::vector<ASTPattern> rv;
            rv.reserve(list.size());
            for (const auto& p : list) {
                rv.push_back(p.clone());
            }
            return rv;
        }

        static TuplePat cloneTup(const TuplePat& p) {
            return TuplePat{H::cloneList(p.start), p.hasWildcard, H::cloneList(p.end)};
        }

        static ASTPattern::Value cloneVal(const ASTPattern::Value& v) {
            switch (v.tag()) {
                case ASTPattern::Value::TAG_Invalid: {
                    auto& e = v.as_Invalid();
                    return Value(e);
                }
                case ASTPattern::Value::TAG_Integer: {
                    auto& e = v.as_Integer();
                    return Value(e);
                }
                case ASTPattern::Value::TAG_Float: {
                    auto& e = v.as_Float();
                    return Value(e);
                }
                case ASTPattern::Value::TAG_String: {
                    auto& e = v.as_String();
                    return Value(e);
                }
                case ASTPattern::Value::TAG_ByteString: {
                    auto& e = v.as_ByteString();
                    return Value(e);
                }
                case ASTPattern::Value::TAG_Named: {
                    auto& e = v.as_Named();
                    return Value::make_Named(ASTPath(e));
                }
            }
            UNREACHABLE();
        }
    };

    switch (data_.tag()) {
        case ASTPatternData::TAG_Any: {
            auto& e = data_.as_Any();
            rv.data_ = Data::make_Any(e);
            break;
        }
        case ASTPatternData::TAG_Never: {
            auto& e = data_.as_Never();
            rv.data_ = Data::make_Never(e);
            break;
        }
        case ASTPatternData::TAG_MaybeBind: {
            auto& e = data_.as_MaybeBind();
            rv.data_ = Data::make_MaybeBind(e);
            break;
        }
        case ASTPatternData::TAG_Macro: {
            auto& e = data_.as_Macro();
            rv.data_ = Data::make_Macro({std::make_unique<ASTMacroInvocation>(e.inv->clone())});
            break;
        }
        case ASTPatternData::TAG_Box: {
            auto& e = data_.as_Box();
            rv.data_ = Data::make_Box({H::cloneSp(e.sub)});
            break;
        }
        case ASTPatternData::TAG_Guard: {
            auto& e = data_.as_Guard();
            rv.data_ = Data::make_Guard({H::cloneSp(e.sub), e.cond->clone()});
            break;
        }
        case ASTPatternData::TAG_Deref: {
            auto& e = data_.as_Deref();
            rv.data_ = Data::make_Deref({H::cloneSp(e.sub)});
            break;
        }
        case ASTPatternData::TAG_Ref: {
            auto& e = data_.as_Ref();
            rv.data_ = Data::make_Ref({e.mut, H::cloneSp(e.sub)});
            break;
        }
        case ASTPatternData::TAG_Value: {
            auto& e = data_.as_Value();
            rv.data_ = Data::make_Value({H::cloneVal(e.start), H::cloneVal(e.end)});
            break;
        }
        case ASTPatternData::TAG_ValueLeftInc: {
            auto& e = data_.as_ValueLeftInc();
            rv.data_ = Data::make_ValueLeftInc({H::cloneVal(e.start), H::cloneVal(e.end)});
            break;
        }
        case ASTPatternData::TAG_Tuple: {
            auto& e = data_.as_Tuple();
            rv.data_ = Data::make_Tuple(H::cloneTup(e));
            break;
        }
        case ASTPatternData::TAG_StructTuple: {
            auto& e = data_.as_StructTuple();
            rv.data_ = Data::make_StructTuple({ASTPath(e.path), H::cloneTup(e.tupPat)});
            break;
        }
        case ASTPatternData::TAG_Struct: {
            auto& e = data_.as_Struct();
            std::vector<ASTStructPatternEntry> sps;
            for (const auto& sp : e.subPatterns) {
                sps.push_back(ASTStructPatternEntry{sp.attrs.clone(), sp.name, sp.pat.clone()});
            }
            rv.data_ = Data::make_Struct({ASTPath(e.path), mv$(sps)});
            break;
        }
        case ASTPatternData::TAG_Slice: {
            auto& e = data_.as_Slice();
            rv.data_ = Data::make_Slice({H::cloneList(e.subPats)});
            break;
        }
        case ASTPatternData::TAG_SplitSlice: {
            auto& e = data_.as_SplitSlice();
            rv.data_ = Data::make_SplitSlice({H::cloneList(e.leading), e.extraBind, H::cloneList(e.trailing), e.extraRest});
            break;
        }
        case ASTPatternData::TAG_Or: {
            auto& e = data_.as_Or();
            rv.data_ = Data::make_Or(H::cloneList(e));
            break;
        }
    }

    return rv;
}

ASTPatternBinding::ASTPatternBinding()
    : name({}, "")
    , type(Type::MOVE)
    , isMutable(false)
    , slot(~0u)
{
}

ASTPatternBinding::ASTPatternBinding(Ident name, Type ty, bool ismut)
    : name(std::move(name))
    , type(ty)
    , isMutable(ismut)
    , slot(~0u)
{
}

ASTPattern::ASTPattern() {
}

ASTPattern::ASTPattern(Span sp, Data dat)
    : span_(mv$(sp))
    , data_(mv$(dat))
{
}

ASTPattern::ASTPattern(TagMaybeBind, Span sp, Ident name)
    : span_(mv$(sp))
    , data_(Data::make_MaybeBind({mv$(name)}))
{
}

ASTPattern::ASTPattern(TagMacro, Span sp, unique_ptr<ASTMacroInvocation> inv)
    : span_(mv$(sp))
    , data_(Data::make_Macro({mv$(inv)}))
{
}

ASTPattern::ASTPattern(TagBind, Span sp, Ident name, ASTPatternBinding::Type ty, bool isMut)
    : span_(mv$(sp))
{
    bindings_.push_back(ASTPatternBinding(mv$(name), ty, isMut));
}

ASTPattern::ASTPattern(TagBox, Span sp, ASTPattern sub)
    : span_(mv$(sp))
    , data_(Data::make_Box({unique_ptr<ASTPattern>(new ASTPattern(mv$(sub)))}))
{
}

ASTPattern::ASTPattern(TagDeref, Span sp, ASTPattern sub)
    : span_(mv$(sp))
    , data_(Data::make_Deref({unique_ptr<ASTPattern>(new ASTPattern(mv$(sub)))}))
{
}

ASTPattern::ASTPattern(TagValue, Span sp, Value val, Value end)
    : span_(mv$(sp))
    , data_(Data::make_Value({std::move(val), std::move(end)}))
{
}

ASTPattern::ASTPattern(TagReference, Span sp, bool isMutable, ASTPattern subPattern)
    : span_(mv$(sp))
    , data_(Data::make_Ref(/*Data::Data_Ref */ {isMutable, unique_ptr<ASTPattern>(new ASTPattern(std::move(subPattern)))}))
{
}

ASTPattern::ASTPattern(TagTuple, Span sp, std::vector<ASTPattern> pats)
    : span_(mv$(sp))
    , data_(Data::make_Tuple(TuplePat{mv$(pats), false, {}}))
{
}

ASTPattern::ASTPattern(TagTuple, Span sp, TuplePat pat)
    : span_(mv$(sp))
    , data_(Data::make_Tuple(mv$(pat)))
{
}

ASTPattern::ASTPattern(TagNamedTuple, Span sp, ASTPath path, std::vector<ASTPattern> pats)
    : span_(mv$(sp))
    , data_(Data::make_StructTuple({mv$(path), TuplePat{mv$(pats), false, {}}}))
{
}

ASTPattern::ASTPattern(TagNamedTuple, Span sp, ASTPath path, TuplePat pat)
    : span_(mv$(sp))
    , data_(Data::make_StructTuple({std::move(path), std::move(pat)}))
{
}

namespace {
    Ordering ordPatternValue(const ASTPattern::Value& a, const ASTPattern::Value& b) {
        auto rv = ::ord(static_cast<unsigned>(a.tag()), static_cast<unsigned>(b.tag()));
        if (rv != OrdEqual) {
            return rv;
        }
        switch (a.tag()) {
            case ASTPattern::Value::TAG_Invalid: {
                return OrdEqual;
            }
            case ASTPattern::Value::TAG_Integer: {
                auto& ae = a.as_Integer();
                auto& be = b.as_Integer();
                rv = ::ord(static_cast<unsigned>(ae.type), static_cast<unsigned>(be.type));
                if (rv != OrdEqual) {
                    return rv;
                }
                return ::ord(ae.value, be.value);
                break;
            }
            case ASTPattern::Value::TAG_Float: {
                auto& ae = a.as_Float();
                auto& be = b.as_Float();
                rv = ::ord(static_cast<unsigned>(ae.type), static_cast<unsigned>(be.type));
                if (rv != OrdEqual) {
                    return rv;
                }
                rv = ::ord(ae.value.bitsHi(), be.value.bitsHi());
                if (rv != OrdEqual) {
                    return rv;
                }
                return ::ord(ae.value.bitsLo(), be.value.bitsLo());
                break;
            }
            case ASTPattern::Value::TAG_String: {
                auto& ae = a.as_String();
                auto& be = b.as_String();
                return ::ord(ae, be);
            }
            case ASTPattern::Value::TAG_ByteString: {
                auto& ae = a.as_ByteString();
                auto& be = b.as_ByteString();
                return ::ord(ae.v, be.v);
            }
            case ASTPattern::Value::TAG_Named: {
                auto& ae = a.as_Named();
                auto& be = b.as_Named();
                return ae.ord(be);
            }
        }
        UNREACHABLE();
    }
}

Ordering ord(const ASTPattern& a, const ASTPattern& b) {
    auto rv = ::ord(static_cast<unsigned>(a.data().tag()), static_cast<unsigned>(b.data().tag()));
    if (rv != OrdEqual) {
        return rv;
    }
    switch (a.data().tag()) {
        case ASTPattern::Data::TAG_Value: {
            auto& ae = a.data().as_Value();
            auto& be = b.data().as_Value();
            rv = ordPatternValue(ae.start, be.start);
            if (rv != OrdEqual) {
                return rv;
            }
            return ordPatternValue(ae.end, be.end);
            break;
        }
        case ASTPattern::Data::TAG_ValueLeftInc: {
            auto& ae = a.data().as_ValueLeftInc();
            auto& be = b.data().as_ValueLeftInc();
            rv = ordPatternValue(ae.start, be.start);
            if (rv != OrdEqual) {
                return rv;
            }
            return ordPatternValue(ae.end, be.end);
            break;
        }
        case ASTPattern::Data::TAG_Or: {
            auto& ae = a.data().as_Or();
            auto& be = b.data().as_Or();
            rv = ::ord(ae.size(), be.size());
            if (rv != OrdEqual) {
                return rv;
            }
            for (size_t i = 0; i < ae.size(); i++) {
                rv = ::ord(ae[i], be[i]);
                if (rv != OrdEqual) {
                    return rv;
                }
            }
            return OrdEqual;
            break;
        }
        case ASTPattern::Data::TAG_MaybeBind: {
            auto& ae = a.data().as_MaybeBind();
            auto& be = b.data().as_MaybeBind();
            return ::ord(ae.name.name, be.name.name);
        }
        case ASTPattern::Data::TAG_Macro: {
            compileErrorBugCheck("ord on unexpanded pattern macro");
        }
        case ASTPattern::Data::TAG_Any: {
            return OrdEqual;
        }
        case ASTPattern::Data::TAG_Never: {
            return OrdEqual;
        }
        case ASTPattern::Data::TAG_Box: {
            auto& ae = a.data().as_Box();
            auto& be = b.data().as_Box();
            return ::ord(*ae.sub, *be.sub);
        }
        case ASTPattern::Data::TAG_Guard: {
            compileErrorBugCheck("ord on a guard pattern");
        }
        case ASTPattern::Data::TAG_Deref: {
            auto& ae = a.data().as_Deref();
            auto& be = b.data().as_Deref();
            return ::ord(*ae.sub, *be.sub);
        }
        case ASTPattern::Data::TAG_Ref: {
            auto& ae = a.data().as_Ref();
            auto& be = b.data().as_Ref();
            rv = ::ord(ae.mut, be.mut);
            if (rv != OrdEqual) {
                return rv;
            }
            return ::ord(*ae.sub, *be.sub);
            break;
        }
        case ASTPattern::Data::TAG_Tuple: {
            compileErrorBugCheck("ord on unsupported tuple pattern type");
        }
        case ASTPattern::Data::TAG_StructTuple: {
            compileErrorBugCheck("ord on unsupported tuple-struct pattern type");
        }
        case ASTPattern::Data::TAG_Struct: {
            compileErrorBugCheck("ord on unsupported struct pattern type");
        }
        case ASTPattern::Data::TAG_Slice: {
            compileErrorBugCheck("ord on unsupported slice pattern type");
        }
        case ASTPattern::Data::TAG_SplitSlice: {
            compileErrorBugCheck("ord on unsupported split-slice pattern type");
        }
    }
    UNREACHABLE();
}

namespace stl {
template <>
void output<ZeroCopyOutput, ASTPattern::Value>(ZeroCopyOutput& os, const ASTPattern::Value& val) {
    switch (val.tag()) {
        case ASTPattern::Value::TAG_Invalid: {
            os << StringView("/*BAD PAT VAL*/");
            break;
        }
        case ASTPattern::Value::TAG_Integer: {
            auto& e = val.as_Integer();
            switch (e.type) {
                case CORETYPE_BOOL:
                    os << (e.value != U128(0) ? "true" : "false");
                    break;
                case CORETYPE_F32:
                case CORETYPE_F64:
                    BUG(Span(), StringView("Hit F32/f64 in printing pattern literal"));
                    break;
                default:
                    os << e.value;
                    break;
            }
            break;
        }
        case ASTPattern::Value::TAG_Float: {
            auto& e = val.as_Float();
            switch (e.type) {
                case CORETYPE_BOOL:
                    os << (e.value != FloatValue() ? "true" : "false");
                    break;
                case CORETYPE_ANY:
                case CORETYPE_F32:
                case CORETYPE_F64:
                    os << e.value;
                    break;
                default:
                    BUG(Span(), StringView("Hit integer in printing pattern literal"));
                    break;
            }
            break;
        }
        case ASTPattern::Value::TAG_String: {
            auto& e = val.as_String();
            os << StringView("\"") << e << StringView("\"");
            break;
        }
        case ASTPattern::Value::TAG_ByteString: {
            auto& e = val.as_ByteString();
            os << StringView("b\"") << e.v << StringView("\"");
            break;
        }
        case ASTPattern::Value::TAG_Named: {
            auto& e = val.as_Named();
            os << e;
            break;
        }
    }
    return;
}

template <>
void output<ZeroCopyOutput, ASTPattern::TuplePat>(ZeroCopyOutput& os, const ASTPattern::TuplePat& val) {
    if (val.hasWildcard) {
        os << val.start;
        os << StringView(".., ");
        os << val.end;
    } else {
        os << val.start;
        BUG_ASSERT(val.end.size() == 0);
    }
    return;
}

template <>
void output<ZeroCopyOutput, ASTPatternBinding>(ZeroCopyOutput& os, ASTPatternBinding pb) {
    if (pb.isMutable) {
        os << StringView("mut ");
    }
    switch (pb.type) {
        case ASTPatternBinding::Type::MOVE:
            break;
        case ASTPatternBinding::Type::REF:
            os << StringView("ref ");
            break;
        case ASTPatternBinding::Type::MUTREF:
            os << StringView("ref mut ");
            break;
    }
    os << pb.name;
    return;
}

template <>
void output<ZeroCopyOutput, ASTPattern>(ZeroCopyOutput& os, const ASTPattern& pat) {
    for (const auto& pb : pat.bindings()) {
        os << pb << StringView(" @ ");
    }
    switch (pat.data().tag()) {
        case ASTPatternData::TAG_MaybeBind: {
            auto& ent = pat.data().as_MaybeBind();
            os << ent.name << StringView("?");
            break;
        }
        case ASTPatternData::TAG_Macro: {
            auto& ent = pat.data().as_Macro();
            os << *ent.inv;
            break;
        }
        case ASTPatternData::TAG_Any: {
            os << StringView("_");
            break;
        }
        case ASTPatternData::TAG_Never: {
            os << StringView("!");
            break;
        }
        case ASTPatternData::TAG_Box: {
            auto& ent = pat.data().as_Box();
            os << StringView("box ") << *ent.sub;
            break;
        }
        case ASTPatternData::TAG_Guard: {
            auto& ent = pat.data().as_Guard();
            os << StringView("(") << *ent.sub << StringView(" if ") << *ent.cond << StringView(")");
            break;
        }
        case ASTPatternData::TAG_Deref: {
            auto& ent = pat.data().as_Deref();
            os << StringView("deref!(") << *ent.sub << StringView(")");
            break;
        }
        case ASTPatternData::TAG_Ref: {
            auto& ent = pat.data().as_Ref();
            os << StringView("&") << (ent.mut ? "mut " : "") << *ent.sub;
            break;
        }
        case ASTPatternData::TAG_Value: {
            auto& ent = pat.data().as_Value();
            if (!ent.start.is_Invalid()) {
                os << ent.start;
            }
            if (!ent.end.is_Invalid()) {
                os << StringView(" ..= ") << ent.end;
            }
            break;
        }
        case ASTPatternData::TAG_ValueLeftInc: {
            auto& ent = pat.data().as_ValueLeftInc();
            if (ent.start.is_Invalid() && ent.end.is_Invalid()) {
                os << StringView("..");
            } else {
                os << ent.start << StringView(" .. ") << ent.end;
            }
            break;
        }
        case ASTPatternData::TAG_Tuple: {
            auto& ent = pat.data().as_Tuple();
            os << StringView("(") << ent << StringView(")");
            break;
        }
        case ASTPatternData::TAG_StructTuple: {
            auto& ent = pat.data().as_StructTuple();
            os << ent.path << StringView(" (") << ent.tupPat << StringView(")");
            break;
        }
        case ASTPatternData::TAG_Struct: {
            auto& ent = pat.data().as_Struct();
            os << ent.path << StringView(" {");
            for (const auto& e : ent.subPatterns) {
                os << e.attrs;
                os << e.name << StringView(": ") << e.pat;
                os << StringView(",");
            }
            os << StringView("}");
            if (ent.isExhaustive) {
                os << StringView("..");
            }
            break;
        }
        case ASTPatternData::TAG_Slice: {
            auto& ent = pat.data().as_Slice();
            os << StringView("[");
            os << ent.subPats;
            os << StringView("]");
            break;
        }
        case ASTPatternData::TAG_SplitSlice: {
            auto& ent = pat.data().as_SplitSlice();
            os << StringView("[");
            bool needsComma = false;
            if (ent.leading.size()) {
                os << ent.leading;
                needsComma = true;
            }

            if (needsComma) {
                os << StringView(", ");
            }
            if (ent.extraBind.isValid()) {
                os << ent.extraBind;
            }
            os << StringView("..");
            needsComma = true;

            if (ent.trailing.size()) {
                if (needsComma) {
                    os << StringView(", ");
                }
                os << ent.trailing;
            }
            os << StringView("]");
            break;
        }
        case ASTPatternData::TAG_Or: {
            auto& ent = pat.data().as_Or();
            os << StringView("(");
            for (const auto& e : ent) {
                os << (&e == &ent.front() ? "" : " | ") << e;
            }
            os << StringView(")");
            break;
        }
    }
    return;
}

template <>
void output<ZeroCopyOutput, std::vector<ASTPattern>>(ZeroCopyOutput& out, const std::vector<ASTPattern>& values) {
    outCont(out, values);
}

}
