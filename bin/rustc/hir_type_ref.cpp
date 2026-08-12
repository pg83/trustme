#include "hir_type_ref.h"

namespace HIR {

TrackHrbStack::PopOnDrop::PopOnDrop(): v(nullptr) {}
TrackHrbStack::PopOnDrop::PopOnDrop(std::vector<const HIR::GenericParams*>& v): v(&v) {}
TrackHrbStack::PopOnDrop::~PopOnDrop() {
    if (v) {
        assert(!v->empty());
        v->pop_back();
    }
}
TrackHrbStack::PopOnDrop::PopOnDrop(PopOnDrop&& x): v(x.v) { x.v = nullptr; }
TrackHrbStack::PopOnDrop TrackHrbStack::pushHrb(const HIR::GenericParams& params) const {
    hrbStack.push_back(&params);
    return PopOnDrop(hrbStack);
}
}
