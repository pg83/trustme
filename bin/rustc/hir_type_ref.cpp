#include "hir_type_ref.h"

#include "output.h"

using namespace stl;

const HIRTypeData* HIRResolvePlaceholdersNop::getType(const Span&, const HIRTypeData* ty) const {
    return ty;
}

const HIRConstGeneric& HIRResolvePlaceholdersNop::getVal(const Span&, const HIRConstGeneric& v) const {
    return v;
}

namespace stl {
    template <>
    void output<ZeroCopyOutput, HIRCompare>(ZeroCopyOutput& os, HIRCompare x) {
        switch (x) {
            case HIRCompare::Equal:
                os << StringView("Equal");
                break;
            case HIRCompare::Fuzzy:
                os << StringView("Fuzzy");
                break;
            case HIRCompare::Unequal:
                os << StringView("Unequal");
                break;
        }
    }
}
