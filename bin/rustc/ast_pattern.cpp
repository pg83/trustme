#include "ast_pattern.h"

#include "common.h"
#include "ast_ast.h"
#include "ast_expr.h" // A guard pattern holds an expression

::std::ostream& operator<<(::std::ostream& os, const ASTPattern::Value& val) {
    TU_MATCH(
        ASTPattern::Value,
        (val),
        (e),
        (Invalid, os << "/*BAD PAT VAL*/";),
        (Integer,
         switch (e.type) {
             case CORETYPE_BOOL:
                 os << (e.value != U128(0) ? "true" : "false");
                 break;
             case CORETYPE_F32:
             case CORETYPE_F64:
                 BUG(Span(), "Hit F32/f64 in printing pattern literal");
                 break;
             default:
                 os << e.value;
                 break;
         }),
        (Float,
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
                 BUG(Span(), "Hit integer in printing pattern literal");
                 break;
         }),
        (String, os << "\"" << e << "\"";),
        (ByteString, os << "b\"" << e.v << "\"";),
        (Named, os << e;)
    )
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const ASTPattern::TuplePat& val) {
    if (val.hasWildcard) {
        os << val.start;
        os << ".., ";
        os << val.end;
    } else {
        os << val.start;
        assert(val.end.size() == 0);
    }
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const ASTPatternBinding& pb) {
    if (pb.isMutable) {
        os << "mut ";
    }
    switch (pb.type) {
        case ASTPatternBinding::Type::MOVE:
            break;
        case ASTPatternBinding::Type::REF:
            os << "ref ";
            break;
        case ASTPatternBinding::Type::MUTREF:
            os << "ref mut ";
            break;
    }
    os << pb.name;
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const ASTPattern& pat) {
    for (const auto& pb : pat.bindings_) {
        os << pb << " @ ";
    }
    TU_MATCH_HDRA( (pat.data_), {)
    TU_ARMA(MaybeBind, ent) {
            os << ent.name << "?";
        }
        TU_ARMA(Macro, ent) {
            os << *ent.inv;
        }
        TU_ARMA(Any, ent) {
            os << "_";
        }
        TU_ARMA(Box, ent) {
            os << "box " << *ent.sub;
        }
        TU_ARMA(Guard, ent) {
            os << "(" << *ent.sub << " if " << *ent.cond << ")";
        }
        TU_ARMA(Deref, ent) {
            os << "deref!(" << *ent.sub << ")";
        }
        TU_ARMA(Ref, ent) {
            os << "&" << (ent.mut ? "mut " : "") << *ent.sub;
        }
        TU_ARMA(Value, ent) {
            // A range may have no start (`..=10`), and `..` is the rest pattern,
            // which has neither.
            if (!ent.start.is_Invalid()) {
                os << ent.start;
            }
            if (!ent.end.is_Invalid()) {
                os << " ..= " << ent.end;
            }
        }
        TU_ARMA(ValueLeftInc, ent) {
            if (ent.start.is_Invalid() && ent.end.is_Invalid()) {
                os << "..";
            } else {
                os << ent.start << " .. " << ent.end;
            }
        }
        TU_ARMA(Tuple, ent) {
            os << "(" << ent << ")";
        }
        TU_ARMA(StructTuple, ent) {
            os << ent.path << " (" << ent.tupPat << ")";
        }
        TU_ARMA(Struct, ent) {
            os << ent.path << " {";
            for (const auto& e : ent.subPatterns) {
                os << e.attrs;
                os << e.name << ": " << e.pat;
                os << ",";
            }
            os << "}";
            if (ent.isExhaustive) {
                os << "..";
            }
        }
        TU_ARMA(Slice, ent) {
            os << "[";
            os << ent.subPats;
            os << "]";
        }
        TU_ARMA(SplitSlice, ent) {
            os << "[";
            bool needsComma = false;
            if (ent.leading.size()) {
                os << ent.leading;
                needsComma = true;
            }

            if (needsComma) {
                os << ", ";
            }
            if (ent.extraBind.isValid()) {
                os << ent.extraBind;
            }
            os << "..";
            needsComma = true;

            if (ent.trailing.size()) {
                if (needsComma) {
                    os << ", ";
                }
                os << ent.trailing;
            }
            os << "]";
        }
        TU_ARMA(Or, ent) {
            os << "(";
            for (const auto& e : ent) {
                os << (&e == &ent.front() ? "" : " | ") << e;
            }
            os << ")";
        }
    }
    return os;
}

ASTPattern::~ASTPattern() {
}

ASTPattern::ASTPattern(TagStruct, Span sp, ASTPath path, ::std::vector<ASTStructPatternEntry> subPatterns, bool isExhaustive)
    : span_(mv$(sp))
    , data_(Data::make_Struct({::std::move(path), ::std::move(subPatterns), isExhaustive}))
{
}

ASTPattern ASTPattern::clone() const {
    ASTPattern rv;
    rv.span_ = span_;
    for (const auto& pb : bindings_) {
        rv.bindings_.push_back(pb);
    }

    struct H {
        static ::std::unique_ptr<ASTPattern> cloneSp(const ::std::unique_ptr<ASTPattern>& p) {
            return ::std::make_unique<ASTPattern>(p->clone());
        }

        static ::std::vector<ASTPattern> cloneList(const ::std::vector<ASTPattern>& list) {
            ::std::vector<ASTPattern> rv;
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
            TU_MATCH(ASTPattern::Value, (v), (e), (Invalid, return Value(e);), (Integer, return Value(e);), (Float, return Value(e);), (String, return Value(e);), (ByteString, return Value(e);), (Named, return Value::make_Named(ASTPath(e));))
            throw "";
        }
    };

    TU_MATCH_HDRA( (data_), {)
    TU_ARMA(Any, e) {
            rv.data_ = Data::make_Any(e);
        }
        TU_ARMA(MaybeBind, e) {
            rv.data_ = Data::make_MaybeBind(e);
        }
        TU_ARMA(Macro, e) {
            rv.data_ = Data::make_Macro({::std::make_unique<ASTMacroInvocation>(e.inv->clone())});
        }
        TU_ARMA(Box, e) {
            rv.data_ = Data::make_Box({H::cloneSp(e.sub)});
        }
        TU_ARMA(Guard, e) {
            rv.data_ = Data::make_Guard({H::cloneSp(e.sub), e.cond->clone()});
        }
        TU_ARMA(Deref, e) {
            rv.data_ = Data::make_Deref({H::cloneSp(e.sub)});
        }
        TU_ARMA(Ref, e) {
            rv.data_ = Data::make_Ref({e.mut, H::cloneSp(e.sub)});
        }
        TU_ARMA(Value, e) {
            rv.data_ = Data::make_Value({H::cloneVal(e.start), H::cloneVal(e.end)});
        }
        TU_ARMA(ValueLeftInc, e) {
            rv.data_ = Data::make_ValueLeftInc({H::cloneVal(e.start), H::cloneVal(e.end)});
        }
        TU_ARMA(Tuple, e) {
            rv.data_ = Data::make_Tuple(H::cloneTup(e));
        }
        TU_ARMA(StructTuple, e) {
            rv.data_ = Data::make_StructTuple({ASTPath(e.path), H::cloneTup(e.tupPat)});
        }
        TU_ARMA(Struct, e) {
            ::std::vector<ASTStructPatternEntry> sps;
            for (const auto& sp : e.subPatterns) {
                sps.push_back(ASTStructPatternEntry{sp.attrs.clone(), sp.name, sp.pat.clone()});
            }
            rv.data_ = Data::make_Struct({ASTPath(e.path), mv$(sps)});
        }
        TU_ARMA(Slice, e) {
            rv.data_ = Data::make_Slice({H::cloneList(e.subPats)});
        }
        TU_ARMA(SplitSlice, e) {
            rv.data_ = Data::make_SplitSlice({H::cloneList(e.leading), e.extraBind, H::cloneList(e.trailing), e.extraRest});
        }
        TU_ARMA(Or, e) {
            rv.data_ = Data::make_Or(H::cloneList(e));
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
    : name(::std::move(name))
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
    , data_(Data::make_Value({::std::move(val), ::std::move(end)}))
{
}

ASTPattern::ASTPattern(TagReference, Span sp, bool isMutable, ASTPattern subPattern)
    : span_(mv$(sp))
    , data_(Data::make_Ref(/*Data::Data_Ref */ {isMutable, unique_ptr<ASTPattern>(new ASTPattern(::std::move(subPattern)))}))
{
}

ASTPattern::ASTPattern(TagTuple, Span sp, ::std::vector<ASTPattern> pats)
    : span_(mv$(sp))
    , data_(Data::make_Tuple(TuplePat{mv$(pats), false, {}}))
{
}

ASTPattern::ASTPattern(TagTuple, Span sp, TuplePat pat)
    : span_(mv$(sp))
    , data_(Data::make_Tuple(mv$(pat)))
{
}

ASTPattern::ASTPattern(TagNamedTuple, Span sp, ASTPath path, ::std::vector<ASTPattern> pats)
    : span_(mv$(sp))
    , data_(Data::make_StructTuple({mv$(path), TuplePat{mv$(pats), false, {}}}))
{
}

ASTPattern::ASTPattern(TagNamedTuple, Span sp, ASTPath path, TuplePat pat)
    : span_(mv$(sp))
    , data_(Data::make_StructTuple({::std::move(path), ::std::move(pat)}))
{
}
namespace {
    Ordering ordPatternValue(const ASTPattern::Value& a, const ASTPattern::Value& b) {
        auto rv = ::ord(static_cast<unsigned>(a.tag()), static_cast<unsigned>(b.tag()));
        if (rv != OrdEqual) return rv;
        TU_MATCH(ASTPattern::Value, (a, b), (ae, be),
            (Invalid, return OrdEqual;),
            (Integer, rv = ::ord(static_cast<unsigned>(ae.type), static_cast<unsigned>(be.type)); if (rv != OrdEqual) return rv; return ::ord(ae.value, be.value);),
            (Float, rv = ::ord(static_cast<unsigned>(ae.type), static_cast<unsigned>(be.type)); if (rv != OrdEqual) return rv; rv = ::ord(ae.value.bitsHi(), be.value.bitsHi()); if (rv != OrdEqual) return rv; return ::ord(ae.value.bitsLo(), be.value.bitsLo());),
            (String, return ::ord(ae, be);),
            (ByteString, return ::ord(ae.v, be.v);),
            (Named, return ae.ord(be);)
        )
        throw "";
    }
}

Ordering ord(const ASTPattern& a, const ASTPattern& b) {
    auto rv = ::ord(static_cast<unsigned>(a.data().tag()), static_cast<unsigned>(b.data().tag()));
    if (rv != OrdEqual) return rv;
    TU_MATCH(ASTPattern::Data, (a.data(), b.data()), (ae, be),
        (Value, rv = ordPatternValue(ae.start, be.start); if (rv != OrdEqual) return rv; return ordPatternValue(ae.end, be.end);),
        (ValueLeftInc, rv = ordPatternValue(ae.start, be.start); if (rv != OrdEqual) return rv; return ordPatternValue(ae.end, be.end);),
        (Or, rv = ::ord(ae.size(), be.size()); if (rv != OrdEqual) return rv; for (size_t i = 0; i < ae.size(); i++) { rv = ::ord(ae[i], be[i]); if (rv != OrdEqual) return rv; } return OrdEqual;),
        (MaybeBind, return ::ord(ae.name.name, be.name.name);),
        (Macro, throw CompileErrorBugCheck("ord on unexpanded pattern macro");),
        (Any, return OrdEqual;),
        (Box, return ::ord(*ae.sub, *be.sub);),
        (Guard, throw CompileErrorBugCheck("ord on a guard pattern");),
        (Deref, return ::ord(*ae.sub, *be.sub);),
        (Ref, rv = ::ord(ae.mut, be.mut); if (rv != OrdEqual) return rv; return ::ord(*ae.sub, *be.sub);),
        (Tuple, throw CompileErrorBugCheck("ord on unsupported tuple pattern type");),
        (StructTuple, throw CompileErrorBugCheck("ord on unsupported tuple-struct pattern type");),
        (Struct, throw CompileErrorBugCheck("ord on unsupported struct pattern type");),
        (Slice, throw CompileErrorBugCheck("ord on unsupported slice pattern type");),
        (SplitSlice, throw CompileErrorBugCheck("ord on unsupported split-slice pattern type");)
    )
    throw "";
}
