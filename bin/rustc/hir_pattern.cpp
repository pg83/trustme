#include "hir_pattern.h"

#include "output.h"

#include <std/lib/vector.h>

using namespace stl;

namespace {
    void visitPatternDeclarationSlots(const HIRPattern& pattern, Vector<unsigned>& slots) {
        for (const auto& binding : pattern.bindings) {
            slots.pushBack(binding.slot);
        }

        switch (pattern.data.tag()) {
            case HIRPatternData::TAG_Any: {
                break;
            }
            case HIRPatternData::TAG_Box: {
                auto& e = pattern.data.as_Box();
                visitPatternDeclarationSlots(*e.sub, slots);
                break;
            }
            case HIRPatternData::TAG_Deref: {
                auto& e = pattern.data.as_Deref();
                visitPatternDeclarationSlots(*e.sub, slots);
                break;
            }
            case HIRPatternData::TAG_Ref: {
                auto& e = pattern.data.as_Ref();
                visitPatternDeclarationSlots(*e.sub, slots);
                break;
            }
            case HIRPatternData::TAG_Tuple: {
                auto& e = pattern.data.as_Tuple();
                for (const auto& subpattern : e.subPatterns) {
                    visitPatternDeclarationSlots(subpattern, slots);
                }
                break;
            }
            case HIRPatternData::TAG_SplitTuple: {
                auto& e = pattern.data.as_SplitTuple();
                for (const auto& subpattern : e.leading) {
                    visitPatternDeclarationSlots(subpattern, slots);
                }
                for (const auto& subpattern : e.trailing) {
                    visitPatternDeclarationSlots(subpattern, slots);
                }
                break;
            }
            case HIRPatternData::TAG_PathValue: {
                break;
            }
            case HIRPatternData::TAG_PathTuple: {
                auto& e = pattern.data.as_PathTuple();
                for (const auto& subpattern : e.leading) {
                    visitPatternDeclarationSlots(subpattern, slots);
                }
                for (const auto& subpattern : e.trailing) {
                    visitPatternDeclarationSlots(subpattern, slots);
                }
                break;
            }
            case HIRPatternData::TAG_PathNamed: {
                auto& e = pattern.data.as_PathNamed();
                for (const auto& field : e.subPatterns) {
                    visitPatternDeclarationSlots(field.second, slots);
                }
                break;
            }
            case HIRPatternData::TAG_Value: {
                break;
            }
            case HIRPatternData::TAG_Range: {
                break;
            }
            case HIRPatternData::TAG_Slice: {
                auto& e = pattern.data.as_Slice();
                for (const auto& subpattern : e.subPatterns) {
                    visitPatternDeclarationSlots(subpattern, slots);
                }
                break;
            }
            case HIRPatternData::TAG_SplitSlice: {
                auto& e = pattern.data.as_SplitSlice();
                for (const auto& subpattern : e.leading) {
                    visitPatternDeclarationSlots(subpattern, slots);
                }
                if (e.extraBind.isValid()) {
                    slots.pushBack(e.extraBind.slot);
                }
                for (const auto& subpattern : e.trailing) {
                    visitPatternDeclarationSlots(subpattern, slots);
                }
                break;
            }
            case HIRPatternData::TAG_Or: {
                auto& e = pattern.data.as_Or();
                BUG_ASSERT(!e.empty());
                visitPatternDeclarationSlots(e.front(), slots);
                break;
            }
        }
    }

    void visitPatternCandidateSlots(const HIRPattern& pattern, bool useLastAlternative, Vector<unsigned>& slots) {
        Vector<const HIRPattern*> deferredOrPatterns;
        auto visitImmediate = [&](this auto& visitImmediate, const HIRPattern& current) -> void {
            switch (current.data.tag()) {
                case HIRPatternData::TAG_Any: {
                    break;
                }
                case HIRPatternData::TAG_Box: {
                    auto& e = current.data.as_Box();
                    visitImmediate(*e.sub);
                    break;
                }
                case HIRPatternData::TAG_Deref: {
                    auto& e = current.data.as_Deref();
                    visitImmediate(*e.sub);
                    break;
                }
                case HIRPatternData::TAG_Ref: {
                    auto& e = current.data.as_Ref();
                    visitImmediate(*e.sub);
                    break;
                }
                case HIRPatternData::TAG_Tuple: {
                    auto& e = current.data.as_Tuple();
                    for (const auto& subpattern : e.subPatterns) {
                        visitImmediate(subpattern);
                    }
                    break;
                }
                case HIRPatternData::TAG_SplitTuple: {
                    auto& e = current.data.as_SplitTuple();
                    for (const auto& subpattern : e.leading) {
                        visitImmediate(subpattern);
                    }
                    for (const auto& subpattern : e.trailing) {
                        visitImmediate(subpattern);
                    }
                    break;
                }
                case HIRPatternData::TAG_PathValue: {
                    break;
                }
                case HIRPatternData::TAG_PathTuple: {
                    auto& e = current.data.as_PathTuple();
                    for (const auto& subpattern : e.leading) {
                        visitImmediate(subpattern);
                    }
                    for (const auto& subpattern : e.trailing) {
                        visitImmediate(subpattern);
                    }
                    break;
                }
                case HIRPatternData::TAG_PathNamed: {
                    auto& e = current.data.as_PathNamed();
                    for (const auto& field : e.subPatterns) {
                        visitImmediate(field.second);
                    }
                    break;
                }
                case HIRPatternData::TAG_Value: {
                    break;
                }
                case HIRPatternData::TAG_Range: {
                    break;
                }
                case HIRPatternData::TAG_Slice: {
                    auto& e = current.data.as_Slice();
                    for (const auto& subpattern : e.subPatterns) {
                        visitImmediate(subpattern);
                    }
                    break;
                }
                case HIRPatternData::TAG_SplitSlice: {
                    auto& e = current.data.as_SplitSlice();
                    for (const auto& subpattern : e.leading) {
                        visitImmediate(subpattern);
                    }
                    if (e.extraBind.isValid()) {
                        slots.pushBack(e.extraBind.slot);
                    }
                    for (auto it = e.trailing.rbegin(); it != e.trailing.rend(); ++it) {
                        visitImmediate(*it);
                    }
                    break;
                }
                case HIRPatternData::TAG_Or: {
                    BUG_ASSERT(!current.data.as_Or().empty());
                    deferredOrPatterns.pushBack(&current);
                    break;
                }
            }

            for (auto it = current.bindings.rbegin(); it != current.bindings.rend(); ++it) {
                slots.pushBack(it->slot);
            }
        };

        visitImmediate(pattern);
        for (const auto* orPattern : deferredOrPatterns) {
            const auto& alternatives = orPattern->data.as_Or();
            visitPatternCandidateSlots(useLastAlternative ? alternatives.back() : alternatives.front(), useLastAlternative, slots);
        }
    }

    std::vector<HIRPattern> clonePatVec(const std::vector<HIRPattern>& pats) {
        std::vector<HIRPattern> rv;
        rv.reserve(pats.size());
        for (const auto& pat : pats) {
            rv.push_back(pat.clone());
        }
        return rv;
    }

    typedef std::vector<std::pair<RcString, HIRPattern>> patFieldsT;

    patFieldsT clonePatFields(const patFieldsT& pats) {
        patFieldsT rv;
        rv.reserve(pats.size());
        for (const auto& field : pats) {
            rv.push_back(std::make_pair(field.first, field.second.clone()));
        }
        return rv;
    }

    HIRPattern::Value clonePatval(const HIRPattern::Value& val) {
        switch (val.tag()) {
            case HIRPattern::Value::TAG_Integer: {
                auto& e = val.as_Integer();
                return HIRPattern::Value::make_Integer(e);
            }
            case HIRPattern::Value::TAG_Float: {
                auto& e = val.as_Float();
                return HIRPattern::Value::make_Float(e);
            }
            case HIRPattern::Value::TAG_String: {
                auto& e = val.as_String();
                return HIRPattern::Value::make_String(e);
            }
            case HIRPattern::Value::TAG_ByteString: {
                auto& e = val.as_ByteString();
                return HIRPattern::Value(e);
            }
            case HIRPattern::Value::TAG_Named: {
                auto& e = val.as_Named();
                return HIRPattern::Value::make_Named({e.path.clone(), e.binding});
            }
        }
        UNREACHABLE();
    }

    HIRPattern::Data clonePatternData(const HIRPattern::Data& data) {
        switch (data.tag()) {
            case HIRPatternData::TAG_Any: {
                return HIRPattern::Data::make_Any({});
            }
            case HIRPatternData::TAG_Box: {
                auto& e = data.as_Box();
                return HIRPattern::Data::make_Box({box$(e.sub->clone())});
            }
            case HIRPatternData::TAG_Deref: {
                auto& e = data.as_Deref();
                return HIRPattern::Data::make_Deref({e.kind, e.targetType, box$(e.sub->clone())});
            }
            case HIRPatternData::TAG_Ref: {
                auto& e = data.as_Ref();
                return HIRPattern::Data::make_Ref({e.type, e.isSkipped, box$(e.sub->clone())});
            }
            case HIRPatternData::TAG_Tuple: {
                auto& e = data.as_Tuple();
                return HIRPattern::Data::make_Tuple({clonePatVec(e.subPatterns)});
            }
            case HIRPatternData::TAG_SplitTuple: {
                auto& e = data.as_SplitTuple();
                return HIRPattern::Data::make_SplitTuple({clonePatVec(e.leading), clonePatVec(e.trailing), e.totalSize});
            }
            case HIRPatternData::TAG_PathValue: {
                auto& e = data.as_PathValue();
                return HIRPattern::Data::make_PathValue({e.path.clone(), e.binding.clone()});
            }
            case HIRPatternData::TAG_PathTuple: {
                auto& e = data.as_PathTuple();
                return HIRPattern::Data::make_PathTuple({e.path.clone(), e.binding.clone(), clonePatVec(e.leading), e.isSplit, clonePatVec(e.trailing), e.totalSize});
            }
            case HIRPatternData::TAG_PathNamed: {
                auto& e = data.as_PathNamed();
                return HIRPattern::Data::make_PathNamed({e.path.clone(), e.binding.clone(), clonePatFields(e.subPatterns), e.isExhaustive});
            }
            case HIRPatternData::TAG_Value: {
                auto& e = data.as_Value();
                return HIRPattern::Data::make_Value({clonePatval(e.val)});
            }
            case HIRPatternData::TAG_Range: {
                auto& e = data.as_Range();
                return HIRPattern::Data::make_Range({box$(clonePatval(*e.start)), box$(clonePatval(*e.end)), e.isInclusive});
            }
            case HIRPatternData::TAG_Slice: {
                auto& e = data.as_Slice();
                return HIRPattern::Data::make_Slice({clonePatVec(e.subPatterns)});
            }
            case HIRPatternData::TAG_SplitSlice: {
                auto& e = data.as_SplitSlice();
                return HIRPattern::Data::make_SplitSlice({clonePatVec(e.leading), e.extraBind, clonePatVec(e.trailing)});
            }
            case HIRPatternData::TAG_Or: {
                auto& e = data.as_Or();
                return clonePatVec(e);
            }
        }

        UNREACHABLE();
    }
}

Vector<unsigned> patternBindingSlots(const HIRPattern& pattern, HIRPatternBindingOrder order) {
    Vector<unsigned> slots;
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

HIRPattern HIRPattern::clone() const {
    auto rv = HIRPattern(bindings, clonePatternData(data));
    rv.implicitDerefCount = implicitDerefCount;
    return rv;
}

HIRPatternBinding::HIRPatternBinding()
    : isMutable(false)
    , type(Type::Move)
    , name("")
    , slot(0)
    , implicitDerefCount(0)
{
}

HIRPatternBinding::HIRPatternBinding(bool mut, Type type, RcString name, unsigned int slot)
    : isMutable(mut)
    , type(type)
    , name(mv$(name))
    , slot(slot)
    , implicitDerefCount(0)
{
}

HIRPattern::HIRPattern() {
}

HIRPattern::HIRPattern(std::vector<HIRPatternBinding> pbs, Data d)
    : bindings(mv$(pbs))
    , data(mv$(d))
{
}

HIRPattern::HIRPattern(HIRPatternBinding pb, Data d)
    : data(mv$(d))
{
    if (pb.isValid()) {
        bindings.push_back(std::move(pb));
    }
}

template <>
void stl::output<ZeroCopyOutput, HIRPatternBinding>(ZeroCopyOutput& os, HIRPatternBinding x) {
    if (x.isMutable) {
        os << StringView("mut ");
    }
    switch (x.type) {
        case HIRPatternBinding::Type::Move:
            break;
        case HIRPatternBinding::Type::Ref:
            os << StringView("ref ");
            break;
        case HIRPatternBinding::Type::MutRef:
            os << StringView("ref mut ");
            break;
    }
    os << x.name << StringView("/*") << x.slot << StringView("*/") << StringView(" @ ");
    return;
}

template <>
void stl::output<ZeroCopyOutput, HIRPattern>(ZeroCopyOutput& os, const HIRPattern& x) {
    for (const auto& pb : x.bindings) {
        os << pb;
    }
    if (x.implicitDerefCount > 0) {
        os << StringView("&*") << x.implicitDerefCount;
    }
    switch (x.data.tag()) {
        case HIRPatternData::TAG_Any: {
            os << StringView("_");
            break;
        }
        case HIRPatternData::TAG_Box: {
            auto& e = x.data.as_Box();
            os << StringView("box ") << *e.sub;
            break;
        }
        case HIRPatternData::TAG_Deref: {
            auto& e = x.data.as_Deref();
            os << StringView("deref!(") << *e.sub << StringView(")");
            break;
        }
        case HIRPatternData::TAG_Ref: {
            auto& e = x.data.as_Ref();
            switch (e.type) {
                case HIRBorrowType::Shared:
                    os << StringView("&");
                    break;
                case HIRBorrowType::Unique:
                    os << StringView("&mut ");
                    break;
                case HIRBorrowType::Owned:
                    os << StringView("&move ");
                    break;
            }
            os << *e.sub;
            break;
        }
        case HIRPatternData::TAG_Tuple: {
            auto& e = x.data.as_Tuple();
            os << StringView("(");
            for (const auto& s : e.subPatterns) {
                os << s << StringView(", ");
            }
            os << StringView(")");
            break;
        }
        case HIRPatternData::TAG_SplitTuple: {
            auto& e = x.data.as_SplitTuple();
            os << StringView("(");
            for (const auto& s : e.leading) {
                os << s << StringView(", ");
            }
            os << StringView(".., ");
            for (const auto& s : e.trailing) {
                os << s << StringView(", ");
            }
            os << StringView(")");
            break;
        }
        case HIRPatternData::TAG_PathValue: {
            auto& e = x.data.as_PathValue();
            os << e.path;
            break;
        }
        case HIRPatternData::TAG_PathTuple: {
            auto& e = x.data.as_PathTuple();
            os << e.path;
            os << StringView("(");
            for (const auto& s : e.leading) {
                os << s << StringView(", ");
            }
            if (e.isSplit) {
                os << StringView("..");
                for (const auto& s : e.trailing) {
                    os << StringView(", ") << s;
                }
            }
            os << StringView(")");
            break;
        }
        case HIRPatternData::TAG_PathNamed: {
            auto& e = x.data.as_PathNamed();
            os << e.path;
            os << StringView("{ ");
            for (const auto& ns : e.subPatterns) {
                os << ns.first << StringView(": ") << ns.second << StringView(", ");
            }
            os << StringView("}");
            break;
        }
        case HIRPatternData::TAG_Value: {
            auto& e = x.data.as_Value();
            os << e.val;
            break;
        }
        case HIRPatternData::TAG_Range: {
            auto& e = x.data.as_Range();
            if (e.start) {
                os << *e.start;
            }
            os << StringView(" ..") << (e.isInclusive ? "=" : "") << StringView(" ");
            if (e.end) {
                os << *e.end;
            }
            break;
        }
        case HIRPatternData::TAG_Slice: {
            auto& e = x.data.as_Slice();
            os << StringView("[");
            for (const auto& s : e.subPatterns) {
                os << s << StringView(", ");
            }
            os << StringView("]");
            break;
        }
        case HIRPatternData::TAG_SplitSlice: {
            auto& e = x.data.as_SplitSlice();
            os << StringView("[ ");
            for (const auto& s : e.leading) {
                os << s << StringView(", ");
            }
            if (e.extraBind.isValid()) {
                os << e.extraBind;
            }
            os << StringView("..");
            for (const auto& s : e.trailing) {
                os << StringView(", ") << s;
            }
            os << StringView(" ]");
            break;
        }
        case HIRPatternData::TAG_Or: {
            auto& e = x.data.as_Or();
            os << StringView("(");
            for (size_t i = 0; i < e.size(); i++) {
                if (i != 0) {
                    os << StringView("|");
                }
                os << e[i];
            }
            os << StringView(")");
            break;
        }
    }
    return;
}

template <>
void stl::output<ZeroCopyOutput, std::vector<HIRPattern>>(ZeroCopyOutput& out, const std::vector<HIRPattern>& values) {
    outCont(out, values);
}

template <>
void stl::output<ZeroCopyOutput, std::pair<HIRPattern, HIRTypeRef>>(ZeroCopyOutput& out, const std::pair<HIRPattern, HIRTypeRef>& value) {
    out << StringView("(") << value.first << StringView(", ") << value.second << StringView(")");
}

template <>
void stl::output<ZeroCopyOutput, std::vector<std::pair<HIRPattern, HIRTypeRef>>>(ZeroCopyOutput& out, const std::vector<std::pair<HIRPattern, HIRTypeRef>>& values) {
    outCont(out, values);
}
