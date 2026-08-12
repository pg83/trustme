#include "hir_type_ref.h"

HIRTrackHrbStack::PopOnDrop::PopOnDrop()
    : v(nullptr)
{
}

HIRTrackHrbStack::PopOnDrop::PopOnDrop(std::vector<const HIRGenericParams*>& v)
    : v(&v)
{
}

HIRTrackHrbStack::PopOnDrop::~PopOnDrop() {
    if (v) {
        assert(!v->empty());
        v->pop_back();
    }
}

HIRTrackHrbStack::PopOnDrop::PopOnDrop(PopOnDrop&& x)
    : v(x.v)
{
    x.v = nullptr;
}

HIRTrackHrbStack::PopOnDrop HIRTrackHrbStack::pushHrb(const HIRGenericParams& params) const {
    hrbStack.push_back(&params);
    return PopOnDrop(hrbStack);
}

const HIRTypeData* HIRResolvePlaceholdersNop::getType(const Span&, const HIRTypeData* ty) const {
    return ty;
}

const HIRConstGeneric& HIRResolvePlaceholdersNop::getVal(const Span&, const HIRConstGeneric& v) const {
    return v;
}

