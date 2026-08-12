#include "hir_generic_params.h"
#include "hir_path.h"

#include <cassert>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

// This unit links hir_path.cpp without the rest of the compiler. Its path
// parameters are empty, so neither encoded constants nor their ordering can be
// reached; provide the two otherwise-unresolved out-of-line members explicitly.
HIR::EncodedLiteralPtr::~EncodedLiteralPtr() = default;

Ordering HIR::ConstGeneric::ord(const HIR::ConstGeneric&) const {
    std::abort();
}

namespace {
    HIR::TraitPath make_trait_path(const char* name, bool applyElision) {
        std::unique_ptr<HIR::GenericParams> hrtbs;
        if (applyElision) {
            hrtbs = std::make_unique<HIR::GenericParams>();
        }

        std::vector<RcString> components;
        components.push_back(RcString::new_interned(name));
        HIR::GenericPath path(HIR::SimplePath(RcString::new_interned("test"), std::move(components)));
        HIR::TraitPath trait(std::move(hrtbs), std::move(path));
        trait.lifetimeElision = applyElision;
        return trait;
    }
}

int main() {
    const auto fnBeforeElision = make_trait_path("Fn", true);
    const auto fnAfterElision = make_trait_path("Fn", false);
    const auto unrelated = make_trait_path("Unrelated", false);

    assert(fnBeforeElision == fnAfterElision);
    assert(fnBeforeElision != unrelated);
    assert(fnAfterElision != unrelated);
}
