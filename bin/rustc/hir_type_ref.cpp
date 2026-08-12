#include "hir_type_ref.h"

const HIRTypeData* HIRResolvePlaceholdersNop::getType(const Span&, const HIRTypeData* ty) const {
    return ty;
}

const HIRConstGeneric& HIRResolvePlaceholdersNop::getVal(const Span&, const HIRConstGeneric& v) const {
    return v;
}

