#include "hir_pattern.h"

#include <cassert>
#include <functional>

::std::ostream& operator<<(::std::ostream& os, const HIRPattern::Value& x) {
    TU_MATCH(
        HIRPattern::Value,
        (x),
        (e),
        (Integer,
         // TODO: Print with type (and signed-ness)
         os << e.value;),
        (Float,
         // TODO: Print with type
         os << e.value;),
        (String, os << "\"" << e << "\"";),
        (ByteString, os << "b\"" << e.v << "\"";),
        (Named, os << e.path;)
    )
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const HIRPatternBinding& x) {
    if (x.isMutable) {
        os << "mut ";
    }
    switch (x.mType) {
        case HIRPatternBinding::Type::Move:
            break;
        case HIRPatternBinding::Type::Ref:
            os << "ref ";
            break;
        case HIRPatternBinding::Type::MutRef:
            os << "ref mut ";
            break;
    }
    os << x.mName << "/*" << x.slot << "*/" << " @ ";
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const HIRPattern& x) {
    for (const auto& pb : x.mBindings) {
        os << pb;
    }
    if (x.implicitDerefCount > 0) {
        os << "&*" << x.implicitDerefCount;
    }
        TU_MATCH_HDRA( (x.mData), {)
        TU_ARMA(Any, e) {
            os << "_";
        }
        TU_ARMA(Box, e) {
            os << "box " << *e.sub;
        }
        TU_ARMA(Deref, e) {
            os << "deref!(" << *e.sub << ")";
        }
        TU_ARMA(Ref, e) {
            switch (e.type) {
                case HIRBorrowType::Shared:
                    os << "&";
                    break;
                case HIRBorrowType::Unique:
                    os << "&mut ";
                    break;
                case HIRBorrowType::Owned:
                    os << "&move ";
                    break;
            }
            os << *e.sub;
        }
        TU_ARMA(Tuple, e) {
            os << "(";
            for (const auto& s : e.subPatterns) {
                os << s << ", ";
            }
            os << ")";
        }
        TU_ARMA(SplitTuple, e) {
            os << "(";
            for (const auto& s : e.leading) {
                os << s << ", ";
            }
            os << ".., ";
            for (const auto& s : e.trailing) {
                os << s << ", ";
            }
            os << ")";
        }
        TU_ARMA(PathValue, e) {
            os << e.path;
        }
        TU_ARMA(PathTuple, e) {
            os << e.path;
            os << "(";
            for (const auto& s : e.leading) {
                os << s << ", ";
            }
            if (e.isSplit) {
                os << "..";
                for (const auto& s : e.trailing) {
                    os << ", " << s;
                }
            }
            os << ")";
        }
        TU_ARMA(PathNamed, e) {
            os << e.path;
            os << "{ ";
            for (const auto& ns : e.subPatterns) {
                os << ns.first << ": " << ns.second << ", ";
            }
            os << "}";
        }

        TU_ARMA(Value, e) {
            os << e.val;
        }
        TU_ARMA(Range, e) {
            if (e.start) {
                os << *e.start;
            }
            os << " .." << (e.isInclusive ? "=" : "") << " ";
            if (e.end) {
                os << *e.end;
            }
        }

        TU_ARMA(Slice, e) {
            os << "[";
            for (const auto& s : e.subPatterns) {
                os << s << ", ";
            }
            os << "]";
        }
        TU_ARMA(SplitSlice, e) {
            os << "[ ";
            for (const auto& s : e.leading) {
                os << s << ", ";
            }
            if (e.extraBind.isValid()) {
                os << e.extraBind;
            }
            os << "..";
            for (const auto& s : e.trailing) {
                os << ", " << s;
            }
            os << " ]";
        }
        TU_ARMA(Or, e) {
            os << "(";
            for (size_t i = 0; i < e.size(); i++) {
                if (i != 0) {
                    os << "|";
                }
                os << e[i];
            }
            os << ")";
        }
        }
        return os;
}

namespace {
    void visitPatternDeclarationSlots(const HIRPattern& pattern, ::std::vector<unsigned>& slots) {
        for (const auto& binding : pattern.mBindings) {
            slots.push_back(binding.slot);
        }

        TU_MATCHA((pattern.mData), (e), (Any, ), (Box, visitPatternDeclarationSlots(*e.sub, slots);), (Deref, visitPatternDeclarationSlots(*e.sub, slots);), (Ref, visitPatternDeclarationSlots(*e.sub, slots);), (Tuple, for (const auto& subpattern : e.subPatterns) { visitPatternDeclarationSlots(subpattern, slots); }), (SplitTuple, for (const auto& subpattern : e.leading) visitPatternDeclarationSlots(subpattern, slots); for (const auto& subpattern : e.trailing) visitPatternDeclarationSlots(subpattern, slots);), (PathValue, ), (PathTuple, for (const auto& subpattern : e.leading) visitPatternDeclarationSlots(subpattern, slots); for (const auto& subpattern : e.trailing) visitPatternDeclarationSlots(subpattern, slots);), (PathNamed, for (const auto& field : e.subPatterns) { visitPatternDeclarationSlots(field.second, slots); }), (Value, ), (Range, ), (Slice, for (const auto& subpattern : e.subPatterns) { visitPatternDeclarationSlots(subpattern, slots); }), (SplitSlice, for (const auto& subpattern : e.leading) { visitPatternDeclarationSlots(subpattern, slots); } if (e.extraBind.isValid()) { slots.push_back(e.extraBind.slot); } for (const auto& subpattern : e.trailing) { visitPatternDeclarationSlots(subpattern, slots); }), (Or, assert(!e.empty()); visitPatternDeclarationSlots(e.front(), slots);))
    }

    void visitPatternCandidateSlots(const HIRPattern& pattern, bool useLastAlternative, ::std::vector<unsigned>& slots) {
        ::std::vector<const HIRPattern*> deferredOrPatterns;
        ::std::function<void(const HIRPattern&)> visitImmediate;
        visitImmediate = [&](const HIRPattern& current) {
            TU_MATCHA((current.mData), (e), (Any, ), (Box, visitImmediate(*e.sub);), (Deref, visitImmediate(*e.sub);), (Ref, visitImmediate(*e.sub);), (Tuple, for (const auto& subpattern : e.subPatterns) { visitImmediate(subpattern); }), (SplitTuple, for (const auto& subpattern : e.leading) visitImmediate(subpattern); for (const auto& subpattern : e.trailing) visitImmediate(subpattern);), (PathValue, ), (PathTuple, for (const auto& subpattern : e.leading) visitImmediate(subpattern); for (const auto& subpattern : e.trailing) visitImmediate(subpattern);), (PathNamed, for (const auto& field : e.subPatterns) { visitImmediate(field.second); }), (Value, ), (Range, ), (Slice, for (const auto& subpattern : e.subPatterns) { visitImmediate(subpattern); }), (SplitSlice, for (const auto& subpattern : e.leading) { visitImmediate(subpattern); } if (e.extraBind.isValid()) { slots.push_back(e.extraBind.slot); } for (auto it = e.trailing.rbegin(); it != e.trailing.rend(); ++it) { visitImmediate(*it); }), (Or, assert(!e.empty()); deferredOrPatterns.push_back(&current);))

            // HIR stores `outer @ inner @ pattern` bindings outermost first,
            // while rustc establishes the inner binding first.
            for (auto it = current.mBindings.rbegin(); it != current.mBindings.rend(); ++it) {
                slots.push_back(it->slot);
            }
        };

        visitImmediate(pattern);
        for (const auto* orPattern : deferredOrPatterns) {
            const auto& alternatives = orPattern->mData.as_Or();
            visitPatternCandidateSlots(useLastAlternative ? alternatives.back() : alternatives.front(), useLastAlternative, slots);
        }
    }
}

std::vector<unsigned> patternBindingSlots(const HIRPattern& pattern, HIRPatternBindingOrder order) {
    std::vector<unsigned> slots;
    switch (order) {
        case HIRPatternBindingOrder::Declaration:
            visitPatternDeclarationSlots(pattern, slots);
            break;
        case HIRPatternBindingOrder::FirstCandidate:
            visitPatternCandidateSlots(pattern, false, slots);
            break;
        case HIRPatternBindingOrder::LastCandidate:
            visitPatternCandidateSlots(pattern, true, slots);
            break;
    }
    return slots;
}

namespace {
    ::std::vector<HIRPattern> clonePatVec(const ::std::vector<HIRPattern>& pats) {
        ::std::vector<HIRPattern> rv;
        rv.reserve(pats.size());
        for (const auto& pat : pats) {
            rv.push_back(pat.clone());
        }
        return rv;
    }

    typedef ::std::vector<::std::pair<RcString, HIRPattern>> patFieldsT;

    patFieldsT clonePatFields(const patFieldsT& pats) {
        patFieldsT rv;
        rv.reserve(pats.size());
        for (const auto& field : pats) {
            rv.push_back(::std::make_pair(field.first, field.second.clone()));
        }
        return rv;
    }

    HIRPattern::Value clonePatval(const HIRPattern::Value& val) {
        TU_MATCH(HIRPattern::Value, (val), (e), (Integer, return HIRPattern::Value::make_Integer(e);), (Float, return HIRPattern::Value::make_Float(e);), (String, return HIRPattern::Value::make_String(e);), (ByteString, return HIRPattern::Value(e);), (Named, return HIRPattern::Value::make_Named({e.path.clone(), e.binding});))
        throw "";
    }
} // namespace

namespace {
    HIRPattern::Data clonePatternData(const HIRPattern::Data& mData) {
    TU_MATCH_HDRA( (mData), {)
    TU_ARMA(Any, e) {
                return HIRPattern::Data::make_Any({});
            }
            TU_ARMA(Box, e) {
                return HIRPattern::Data::make_Box({box$(e.sub->clone())});
            }
            TU_ARMA(Deref, e) {
                return HIRPattern::Data::make_Deref({e.kind, e.targetType, box$(e.sub->clone())});
            }
            TU_ARMA(Ref, e) {
                return HIRPattern::Data::make_Ref({e.type, box$(e.sub->clone())});
            }
            TU_ARMA(Tuple, e) {
                return HIRPattern::Data::make_Tuple({clonePatVec(e.subPatterns)});
            }
            TU_ARMA(SplitTuple, e) {
                return HIRPattern::Data::make_SplitTuple({clonePatVec(e.leading), clonePatVec(e.trailing), e.totalSize});
            }
            TU_ARMA(PathValue, e) {
                return HIRPattern::Data::make_PathValue({e.path.clone(), e.binding.clone()});
            }
            TU_ARMA(PathTuple, e) {
                return HIRPattern::Data::make_PathTuple({e.path.clone(), e.binding.clone(), clonePatVec(e.leading), e.isSplit, clonePatVec(e.trailing), e.totalSize});
            }
            TU_ARMA(PathNamed, e) {
                return HIRPattern::Data::make_PathNamed({e.path.clone(), e.binding.clone(), clonePatFields(e.subPatterns), e.isExhaustive});
            }

            TU_ARMA(Value, e) {
                return HIRPattern::Data::make_Value({clonePatval(e.val)});
            }
            TU_ARMA(Range, e) {
                return HIRPattern::Data::make_Range({box$(clonePatval(*e.start)), box$(clonePatval(*e.end)), e.isInclusive});
            }

            TU_ARMA(Slice, e) {
                return HIRPattern::Data::make_Slice({clonePatVec(e.subPatterns)});
            }
            TU_ARMA(SplitSlice, e) {
                return HIRPattern::Data::make_SplitSlice({clonePatVec(e.leading), e.extraBind, clonePatVec(e.trailing)});
            }
            TU_ARMA(Or, e)
            return clonePatVec(e);
    }

    throw "";
    }
}

HIRPattern HIRPattern::clone() const {
    auto rv = HIRPattern(mBindings, clonePatternData(mData));
    rv.implicitDerefCount = implicitDerefCount;
    return rv;
}

HIRPatternBinding::HIRPatternBinding()
    : isMutable(false)
    , mType(Type::Move)
    , mName("")
    , slot(0)
    , implicitDerefCount(0)
{
}

HIRPatternBinding::HIRPatternBinding(bool mut, Type type, RcString name, unsigned int slot)
    : isMutable(mut)
    , mType(type)
    , mName(mv$(name))
    , slot(slot)
    , implicitDerefCount(0)
{
}

HIRPattern::HIRPattern() {
}

HIRPattern::HIRPattern(std::vector<HIRPatternBinding> pbs, Data d)
    : mBindings(mv$(pbs))
    , mData(mv$(d))
{
}

HIRPattern::HIRPattern(HIRPatternBinding pb, Data d)
    : mData(mv$(d))
{
    if (pb.isValid()) {
        mBindings.push_back(std::move(pb));
    }
}
