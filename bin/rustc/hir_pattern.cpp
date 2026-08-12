#include "hir_pattern.h"

#include <cassert>
#include <functional>

namespace HIR {
    ::std::ostream& operator<<(::std::ostream& os, const Pattern::Value& x) {
        TU_MATCH(
            Pattern::Value,
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

    ::std::ostream& operator<<(::std::ostream& os, const PatternBinding& x) {
        if (x.isMutable) {
            os << "mut ";
        }
        switch (x.mType) {
            case PatternBinding::Type::Move:
                break;
            case PatternBinding::Type::Ref:
                os << "ref ";
                break;
            case PatternBinding::Type::MutRef:
                os << "ref mut ";
                break;
        }
        os << x.mName << "/*" << x.slot << "*/" << " @ ";
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const Pattern& x) {
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
            TU_ARMA(Ref, e) {
                switch (e.type) {
                    case BorrowType::Shared:
                        os << "&";
                        break;
                    case BorrowType::Unique:
                        os << "&mut ";
                        break;
                    case BorrowType::Owned:
                        os << "&move ";
                        break;
                }
                os << *e.sub;
            }
            TU_ARMA(Tuple, e) {
                os << "(";
                for (const auto& s : e.sub_patterns) {
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
                if (e.is_split) {
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
                for (const auto& ns : e.sub_patterns) {
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
                os << " .." << (e.is_inclusive ? "=" : "") << " ";
                if (e.end) {
                    os << *e.end;
                }
            }

            TU_ARMA(Slice, e) {
                os << "[";
                for (const auto& s : e.sub_patterns) {
                    os << s << ", ";
                }
                os << "]";
            }
            TU_ARMA(SplitSlice, e) {
                os << "[ ";
                for (const auto& s : e.leading) {
                    os << s << ", ";
                }
                if (e.extraBind.is_valid()) {
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
} // namespace HIR

namespace {
    void visit_pattern_declaration_slots(const ::HIR::Pattern& pattern, ::std::vector<unsigned>& slots) {
        for (const auto& binding : pattern.mBindings) {
            slots.push_back(binding.slot);
        }

        TU_MATCHA(
            (pattern.mData),
            (e),
            (Any, ),
            (Box, visit_pattern_declaration_slots(*e.sub, slots);),
            (Ref, visit_pattern_declaration_slots(*e.sub, slots);),
            (Tuple, for (const auto& subpattern : e.sub_patterns) { visit_pattern_declaration_slots(subpattern, slots); }),
            (SplitTuple, for (const auto& subpattern : e.leading) visit_pattern_declaration_slots(subpattern, slots); for (const auto& subpattern : e.trailing) visit_pattern_declaration_slots(subpattern, slots);),
            (PathValue, ),
            (PathTuple, for (const auto& subpattern : e.leading) visit_pattern_declaration_slots(subpattern, slots); for (const auto& subpattern : e.trailing) visit_pattern_declaration_slots(subpattern, slots);),
            (PathNamed, for (const auto& field : e.sub_patterns) { visit_pattern_declaration_slots(field.second, slots); }),
            (Value, ),
            (Range, ),
            (Slice, for (const auto& subpattern : e.sub_patterns) { visit_pattern_declaration_slots(subpattern, slots); }),
            (SplitSlice, for (const auto& subpattern : e.leading) { visit_pattern_declaration_slots(subpattern, slots); } if (e.extraBind.is_valid()) { slots.push_back(e.extraBind.slot); } for (const auto& subpattern : e.trailing) { visit_pattern_declaration_slots(subpattern, slots); }),
            (Or, assert(!e.empty()); visit_pattern_declaration_slots(e.front(), slots);)
        )
    }

    void visit_pattern_candidate_slots(const ::HIR::Pattern& pattern, bool use_last_alternative, ::std::vector<unsigned>& slots) {
        ::std::vector<const ::HIR::Pattern*> deferredOrPatterns;
        ::std::function<void(const ::HIR::Pattern&)> visit_immediate;
        visit_immediate = [&](const ::HIR::Pattern& current) {
            TU_MATCHA(
                (current.mData),
                (e),
                (Any, ),
                (Box, visit_immediate(*e.sub);),
                (Ref, visit_immediate(*e.sub);),
                (Tuple, for (const auto& subpattern : e.sub_patterns) { visit_immediate(subpattern); }),
                (SplitTuple, for (const auto& subpattern : e.leading) visit_immediate(subpattern); for (const auto& subpattern : e.trailing) visit_immediate(subpattern);),
                (PathValue, ),
                (PathTuple, for (const auto& subpattern : e.leading) visit_immediate(subpattern); for (const auto& subpattern : e.trailing) visit_immediate(subpattern);),
                (PathNamed, for (const auto& field : e.sub_patterns) { visit_immediate(field.second); }),
                (Value, ),
                (Range, ),
                (Slice, for (const auto& subpattern : e.sub_patterns) { visit_immediate(subpattern); }),
                (SplitSlice,
                 for (const auto& subpattern : e.leading) { visit_immediate(subpattern); }
                 if (e.extraBind.is_valid()) { slots.push_back(e.extraBind.slot); }
                 for (auto it = e.trailing.rbegin(); it != e.trailing.rend(); ++it) { visit_immediate(*it); }),
                (Or, assert(!e.empty()); deferredOrPatterns.push_back(&current);)
            )

            // HIR stores `outer @ inner @ pattern` bindings outermost first,
            // while rustc establishes the inner binding first.
            for (auto it = current.mBindings.rbegin(); it != current.mBindings.rend(); ++it) {
                slots.push_back(it->slot);
            }
        };

        visit_immediate(pattern);
        for (const auto* or_pattern : deferredOrPatterns) {
            const auto& alternatives = or_pattern->mData.as_Or();
            visit_pattern_candidate_slots(
                use_last_alternative ? alternatives.back() : alternatives.front(),
                use_last_alternative,
                slots
            );
        }
    }
}

std::vector<unsigned> HIR::pattern_binding_slots(const Pattern& pattern, PatternBindingOrder order) {
    std::vector<unsigned> slots;
    switch (order) {
        case PatternBindingOrder::Declaration:
            visit_pattern_declaration_slots(pattern, slots);
            break;
        case PatternBindingOrder::FirstCandidate:
            visit_pattern_candidate_slots(pattern, false, slots);
            break;
        case PatternBindingOrder::LastCandidate:
            visit_pattern_candidate_slots(pattern, true, slots);
            break;
    }
    return slots;
}

namespace {
    ::std::vector<::HIR::Pattern> clonePatVec(const ::std::vector<::HIR::Pattern>& pats) {
        ::std::vector<::HIR::Pattern> rv;
        rv.reserve(pats.size());
        for (const auto& pat : pats) {
            rv.push_back(pat.clone());
        }
        return rv;
    }

    typedef ::std::vector<::std::pair<RcString, ::HIR::Pattern>> pat_fields_t;

    pat_fields_t clonePatFields(const pat_fields_t& pats) {
        pat_fields_t rv;
        rv.reserve(pats.size());
        for (const auto& field : pats) {
            rv.push_back(::std::make_pair(field.first, field.second.clone()));
        }
        return rv;
    }

    ::HIR::Pattern::Value clonePatval(const ::HIR::Pattern::Value& val) {
        TU_MATCH(::HIR::Pattern::Value, (val), (e), (Integer, return ::HIR::Pattern::Value::make_Integer(e);), (Float, return ::HIR::Pattern::Value::make_Float(e);), (String, return ::HIR::Pattern::Value::make_String(e);), (ByteString, return ::HIR::Pattern::Value(e);), (Named, return ::HIR::Pattern::Value::make_Named({e.path.clone(), e.binding});))
        throw "";
    }
} // namespace

namespace {
    ::HIR::Pattern::Data clonePatternData(const ::HIR::Pattern::Data& mData) {
    TU_MATCH_HDRA( (mData), {)
    TU_ARMA(Any, e) {
                return ::HIR::Pattern::Data::make_Any({});
            }
            TU_ARMA(Box, e) {
                return ::HIR::Pattern::Data::make_Box({box$(e.sub->clone())});
            }
            TU_ARMA(Ref, e) {
                return ::HIR::Pattern::Data::make_Ref({e.type, box$(e.sub->clone())});
            }
            TU_ARMA(Tuple, e) {
                return ::HIR::Pattern::Data::make_Tuple({clonePatVec(e.sub_patterns)});
            }
            TU_ARMA(SplitTuple, e) {
                return ::HIR::Pattern::Data::make_SplitTuple({clonePatVec(e.leading), clonePatVec(e.trailing), e.total_size});
            }
            TU_ARMA(PathValue, e) {
                return ::HIR::Pattern::Data::make_PathValue({e.path.clone(), e.binding.clone()});
            }
            TU_ARMA(PathTuple, e) {
                return ::HIR::Pattern::Data::make_PathTuple({e.path.clone(), e.binding.clone(), clonePatVec(e.leading), e.is_split, clonePatVec(e.trailing), e.total_size});
            }
            TU_ARMA(PathNamed, e) {
                return ::HIR::Pattern::Data::make_PathNamed({e.path.clone(), e.binding.clone(), clonePatFields(e.sub_patterns), e.is_exhaustive});
            }

            TU_ARMA(Value, e) {
                return ::HIR::Pattern::Data::make_Value({clonePatval(e.val)});
            }
            TU_ARMA(Range, e) {
                return ::HIR::Pattern::Data::make_Range({box$(clonePatval(*e.start)), box$(clonePatval(*e.end)), e.is_inclusive});
            }

            TU_ARMA(Slice, e) {
                return ::HIR::Pattern::Data::make_Slice({clonePatVec(e.sub_patterns)});
            }
            TU_ARMA(SplitSlice, e) {
                return ::HIR::Pattern::Data::make_SplitSlice({clonePatVec(e.leading), e.extraBind, clonePatVec(e.trailing)});
            }
            TU_ARMA(Or, e)
            return clonePatVec(e);
    }

    throw "";
    }
}

::HIR::Pattern HIR::Pattern::clone() const {
    auto rv = Pattern(mBindings, clonePatternData(mData));
    rv.implicitDerefCount = implicitDerefCount;
    return rv;
}

namespace HIR {

PatternBinding::PatternBinding()
    : isMutable(false)
    , mType(Type::Move)
    , mName("")
    , slot(0)
    , implicitDerefCount(0) {
}
PatternBinding::PatternBinding(bool mut, Type type, RcString name, unsigned int slot)
    : isMutable(mut)
    , mType(type)
    , mName(mv$(name))
    , slot(slot)
    , implicitDerefCount(0) {
}
Pattern::Pattern() {
}
Pattern::Pattern(std::vector<PatternBinding> pbs, Data d)
    : mBindings(mv$(pbs))
    , mData(mv$(d)) {
}
Pattern::Pattern(PatternBinding pb, Data d)
    : mData(mv$(d)) {
    if (pb.is_valid()) {
        mBindings.push_back(std::move(pb));
    }
}
}
