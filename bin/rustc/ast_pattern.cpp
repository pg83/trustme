#include "ast_pattern.h"
#include "common.h"
#include "ast_ast.h"

namespace AST {

    ::std::ostream& operator<<(::std::ostream& os, const Pattern::Value& val) {
        TU_MATCH(
            Pattern::Value,
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

    ::std::ostream& operator<<(::std::ostream& os, const Pattern::TuplePat& val) {
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

    ::std::ostream& operator<<(::std::ostream& os, const PatternBinding& pb) {
        if (pb.isMutable) {
            os << "mut ";
        }
        switch (pb.mType) {
            case PatternBinding::Type::MOVE:
                break;
            case PatternBinding::Type::REF:
                os << "ref ";
                break;
            case PatternBinding::Type::MUTREF:
                os << "ref mut ";
                break;
        }
        os << pb.mName;
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const Pattern& pat) {
        for (const auto& pb : pat.mBindings) {
            os << pb << " @ ";
        }
    TU_MATCH_HDRA( (pat.mData), {)
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
            TU_ARMA(Ref, ent) {
                os << "&" << (ent.mut ? "mut " : "") << *ent.sub;
            }
            TU_ARMA(Value, ent) {
                os << ent.start;
                if (!ent.end.is_Invalid()) {
                    os << " ..= " << ent.end;
                }
            }
            TU_ARMA(ValueLeftInc, ent) {
                os << ent.start << " .. " << ent.end;
            }
            TU_ARMA(Tuple, ent) {
                os << "(" << ent << ")";
            }
            TU_ARMA(StructTuple, ent) {
                os << ent.path << " (" << ent.tup_pat << ")";
            }
            TU_ARMA(Struct, ent) {
                os << ent.path << " {";
                for (const auto& e : ent.sub_patterns) {
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
                os << ent.sub_pats;
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

    Pattern::~Pattern() {
    }

    AST::Pattern::Pattern(TagStruct, Span sp, Path path, ::std::vector<StructPatternEntry> sub_patterns, bool isExhaustive)
        : mSpan(mv$(sp))
        , mData(Data::make_Struct({::std::move(path), ::std::move(sub_patterns), isExhaustive}))
    {
    }

    AST::Pattern AST::Pattern::clone() const {
        AST::Pattern rv;
        rv.mSpan = mSpan;
        for (const auto& pb : mBindings) {
            rv.mBindings.push_back(pb);
        }

        struct H {
            static ::std::unique_ptr<Pattern> cloneSp(const ::std::unique_ptr<Pattern>& p) {
                return ::std::make_unique<Pattern>(p->clone());
            }

            static ::std::vector<Pattern> cloneList(const ::std::vector<Pattern>& list) {
                ::std::vector<Pattern> rv;
                rv.reserve(list.size());
                for (const auto& p : list) {
                    rv.push_back(p.clone());
                }
                return rv;
            }

            static TuplePat cloneTup(const TuplePat& p) {
                return TuplePat{H::cloneList(p.start), p.hasWildcard, H::cloneList(p.end)};
            }

            static AST::Pattern::Value cloneVal(const AST::Pattern::Value& v) {
                TU_MATCH(::AST::Pattern::Value, (v), (e), (Invalid, return Value(e);), (Integer, return Value(e);), (Float, return Value(e);), (String, return Value(e);), (ByteString, return Value(e);), (Named, return Value::make_Named(AST::Path(e));))
                throw "";
            }
        };

    TU_MATCH_HDRA( (mData), {)
    TU_ARMA(Any, e) {
                rv.mData = Data::make_Any(e);
            }
            TU_ARMA(MaybeBind, e) {
                rv.mData = Data::make_MaybeBind(e);
            }
            TU_ARMA(Macro, e) {
                rv.mData = Data::make_Macro({::std::make_unique<AST::MacroInvocation>(e.inv->clone())});
            }
            TU_ARMA(Box, e) {
                rv.mData = Data::make_Box({H::cloneSp(e.sub)});
            }
            TU_ARMA(Ref, e) {
                rv.mData = Data::make_Ref({e.mut, H::cloneSp(e.sub)});
            }
            TU_ARMA(Value, e) {
                rv.mData = Data::make_Value({H::cloneVal(e.start), H::cloneVal(e.end)});
            }
            TU_ARMA(ValueLeftInc, e) {
                rv.mData = Data::make_ValueLeftInc({H::cloneVal(e.start), H::cloneVal(e.end)});
            }
            TU_ARMA(Tuple, e) {
                rv.mData = Data::make_Tuple(H::cloneTup(e));
            }
            TU_ARMA(StructTuple, e) {
                rv.mData = Data::make_StructTuple({::AST::Path(e.path), H::cloneTup(e.tup_pat)});
            }
            TU_ARMA(Struct, e) {
                ::std::vector<AST::StructPatternEntry> sps;
                for (const auto& sp : e.sub_patterns) {
                    sps.push_back(AST::StructPatternEntry{sp.attrs.clone(), sp.name, sp.pat.clone()});
                }
                rv.mData = Data::make_Struct({::AST::Path(e.path), mv$(sps)});
            }
            TU_ARMA(Slice, e) {
                rv.mData = Data::make_Slice({H::cloneList(e.sub_pats)});
            }
            TU_ARMA(SplitSlice, e) {
                rv.mData = Data::make_SplitSlice({H::cloneList(e.leading), e.extraBind, H::cloneList(e.trailing)});
            }
            TU_ARMA(Or, e) {
                rv.mData = Data::make_Or(H::cloneList(e));
            }
    }

    return rv;
    }

} // namespace AST

namespace AST {

PatternBinding::PatternBinding()
    : mName({}, "")
    , mType(Type::MOVE)
    , isMutable(false)
    , slot(~0u) {
}
PatternBinding::PatternBinding(Ident name, Type ty, bool ismut)
    : mName(::std::move(name))
    , mType(ty)
    , isMutable(ismut)
    , slot(~0u) {
}
Pattern::Pattern() {
}
Pattern::Pattern(Span sp, Data dat)
: mSpan(mv$(sp))
, mData(mv$(dat)) {}
Pattern::Pattern(TagMaybeBind, Span sp, Ident name)
    : mSpan(mv$(sp))
    , mData(Data::make_MaybeBind({mv$(name)})) {
}
Pattern::Pattern(TagMacro, Span sp, unique_ptr<::AST::MacroInvocation> inv)
    : mSpan(mv$(sp))
    , mData(Data::make_Macro({mv$(inv)})) {
}
Pattern::Pattern(TagBind, Span sp, Ident name, PatternBinding::Type ty, bool is_mut)
    : mSpan(mv$(sp)) {
    mBindings.push_back(PatternBinding(mv$(name), ty, is_mut));
}
Pattern::Pattern(TagBox, Span sp, Pattern sub)
    : mSpan(mv$(sp))
    , mData(Data::make_Box({unique_ptr<Pattern>(new Pattern(mv$(sub)))})) {
}
Pattern::Pattern(TagValue, Span sp, Value val, Value end)
    : mSpan(mv$(sp))
    , mData(Data::make_Value({::std::move(val), ::std::move(end)})) {
}
Pattern::Pattern(TagReference, Span sp, bool is_mutable, Pattern sub_pattern)
    : mSpan(mv$(sp))
    , mData(Data::make_Ref(/*Data::Data_Ref */ {is_mutable, unique_ptr<Pattern>(new Pattern(::std::move(sub_pattern)))})) {
}
Pattern::Pattern(TagTuple, Span sp, ::std::vector<Pattern> pats)
    : mSpan(mv$(sp))
    , mData(Data::make_Tuple(TuplePat{mv$(pats), false, {}})) {
}
Pattern::Pattern(TagTuple, Span sp, TuplePat pat)
    : mSpan(mv$(sp))
    , mData(Data::make_Tuple(mv$(pat))) {
}
Pattern::Pattern(TagNamedTuple, Span sp, Path path, ::std::vector<Pattern> pats)
    : mSpan(mv$(sp))
    , mData(Data::make_StructTuple({mv$(path), TuplePat{mv$(pats), false, {}}})) {
}
Pattern::Pattern(TagNamedTuple, Span sp, Path path, TuplePat pat)
    : mSpan(mv$(sp))
    , mData(Data::make_StructTuple({::std::move(path), ::std::move(pat)})) {
}
}
