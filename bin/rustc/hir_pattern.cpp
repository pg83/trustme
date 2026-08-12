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
        if (x.m_mutable) {
            os << "mut ";
        }
        switch (x.m_type) {
            case PatternBinding::Type::Move:
                break;
            case PatternBinding::Type::Ref:
                os << "ref ";
                break;
            case PatternBinding::Type::MutRef:
                os << "ref mut ";
                break;
        }
        os << x.m_name << "/*" << x.m_slot << "*/" << " @ ";
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const Pattern& x) {
        for (const auto& pb : x.m_bindings) {
            os << pb;
        }
        if (x.m_implicit_deref_count > 0) {
            os << "&*" << x.m_implicit_deref_count;
        }
        TU_MATCH_HDRA( (x.m_data), {)
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
                if (e.extra_bind.is_valid()) {
                    os << e.extra_bind;
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
        for (const auto& binding : pattern.m_bindings) {
            slots.push_back(binding.m_slot);
        }

        TU_MATCHA(
            (pattern.m_data),
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
            (SplitSlice, for (const auto& subpattern : e.leading) { visit_pattern_declaration_slots(subpattern, slots); } if (e.extra_bind.is_valid()) { slots.push_back(e.extra_bind.m_slot); } for (const auto& subpattern : e.trailing) { visit_pattern_declaration_slots(subpattern, slots); }),
            (Or, assert(!e.empty()); visit_pattern_declaration_slots(e.front(), slots);)
        )
    }

    void visit_pattern_candidate_slots(const ::HIR::Pattern& pattern, bool use_last_alternative, ::std::vector<unsigned>& slots) {
        ::std::vector<const ::HIR::Pattern*> deferred_or_patterns;
        ::std::function<void(const ::HIR::Pattern&)> visit_immediate;
        visit_immediate = [&](const ::HIR::Pattern& current) {
            TU_MATCHA(
                (current.m_data),
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
                 if (e.extra_bind.is_valid()) { slots.push_back(e.extra_bind.m_slot); }
                 for (auto it = e.trailing.rbegin(); it != e.trailing.rend(); ++it) { visit_immediate(*it); }),
                (Or, assert(!e.empty()); deferred_or_patterns.push_back(&current);)
            )

            // HIR stores `outer @ inner @ pattern` bindings outermost first,
            // while rustc establishes the inner binding first.
            for (auto it = current.m_bindings.rbegin(); it != current.m_bindings.rend(); ++it) {
                slots.push_back(it->m_slot);
            }
        };

        visit_immediate(pattern);
        for (const auto* or_pattern : deferred_or_patterns) {
            const auto& alternatives = or_pattern->m_data.as_Or();
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
    ::std::vector<::HIR::Pattern> clone_pat_vec(const ::std::vector<::HIR::Pattern>& pats) {
        ::std::vector<::HIR::Pattern> rv;
        rv.reserve(pats.size());
        for (const auto& pat : pats) {
            rv.push_back(pat.clone());
        }
        return rv;
    }

    typedef ::std::vector<::std::pair<RcString, ::HIR::Pattern>> pat_fields_t;

    pat_fields_t clone_pat_fields(const pat_fields_t& pats) {
        pat_fields_t rv;
        rv.reserve(pats.size());
        for (const auto& field : pats) {
            rv.push_back(::std::make_pair(field.first, field.second.clone()));
        }
        return rv;
    }

    ::HIR::Pattern::Value clone_patval(const ::HIR::Pattern::Value& val) {
        TU_MATCH(::HIR::Pattern::Value, (val), (e), (Integer, return ::HIR::Pattern::Value::make_Integer(e);), (Float, return ::HIR::Pattern::Value::make_Float(e);), (String, return ::HIR::Pattern::Value::make_String(e);), (ByteString, return ::HIR::Pattern::Value(e);), (Named, return ::HIR::Pattern::Value::make_Named({e.path.clone(), e.binding});))
        throw "";
    }
} // namespace

namespace {
    ::HIR::Pattern::Data clone_pattern_data(const ::HIR::Pattern::Data& m_data) {
    TU_MATCH_HDRA( (m_data), {)
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
                return ::HIR::Pattern::Data::make_Tuple({clone_pat_vec(e.sub_patterns)});
            }
            TU_ARMA(SplitTuple, e) {
                return ::HIR::Pattern::Data::make_SplitTuple({clone_pat_vec(e.leading), clone_pat_vec(e.trailing), e.total_size});
            }
            TU_ARMA(PathValue, e) {
                return ::HIR::Pattern::Data::make_PathValue({e.path.clone(), e.binding.clone()});
            }
            TU_ARMA(PathTuple, e) {
                return ::HIR::Pattern::Data::make_PathTuple({e.path.clone(), e.binding.clone(), clone_pat_vec(e.leading), e.is_split, clone_pat_vec(e.trailing), e.total_size});
            }
            TU_ARMA(PathNamed, e) {
                return ::HIR::Pattern::Data::make_PathNamed({e.path.clone(), e.binding.clone(), clone_pat_fields(e.sub_patterns), e.is_exhaustive});
            }

            TU_ARMA(Value, e) {
                return ::HIR::Pattern::Data::make_Value({clone_patval(e.val)});
            }
            TU_ARMA(Range, e) {
                return ::HIR::Pattern::Data::make_Range({box$(clone_patval(*e.start)), box$(clone_patval(*e.end)), e.is_inclusive});
            }

            TU_ARMA(Slice, e) {
                return ::HIR::Pattern::Data::make_Slice({clone_pat_vec(e.sub_patterns)});
            }
            TU_ARMA(SplitSlice, e) {
                return ::HIR::Pattern::Data::make_SplitSlice({clone_pat_vec(e.leading), e.extra_bind, clone_pat_vec(e.trailing)});
            }
            TU_ARMA(Or, e)
            return clone_pat_vec(e);
    }

    throw "";
    }
}

::HIR::Pattern HIR::Pattern::clone() const {
    auto rv = Pattern(m_bindings, clone_pattern_data(m_data));
    rv.m_implicit_deref_count = m_implicit_deref_count;
    return rv;
}

namespace HIR {

PatternBinding::PatternBinding()
    : m_mutable(false)
    , m_type(Type::Move)
    , m_name("")
    , m_slot(0)
    , m_implicit_deref_count(0) {
}
PatternBinding::PatternBinding(bool mut, Type type, RcString name, unsigned int slot)
    : m_mutable(mut)
    , m_type(type)
    , m_name(mv$(name))
    , m_slot(slot)
    , m_implicit_deref_count(0) {
}
Pattern::Pattern() {
}
Pattern::Pattern(std::vector<PatternBinding> pbs, Data d)
    : m_bindings(mv$(pbs))
    , m_data(mv$(d)) {
}
Pattern::Pattern(PatternBinding pb, Data d)
    : m_data(mv$(d)) {
    if (pb.is_valid()) {
        m_bindings.push_back(std::move(pb));
    }
}
}
