#include "hir_path.h"
#include "hir_generic_params.h"

#include <memory>
#include <vector>
#include <cassert>
#include <cstdlib>
#include <utility>

// This unit links hir_path.cpp without the rest of the compiler. Its path
// parameters are empty, so neither encoded constants nor their ordering can be
// reached; provide the two otherwise-unresolved out-of-line members explicitly.
HIREncodedLiteralPtr::~HIREncodedLiteralPtr() = default;

Ordering HIRConstGeneric::ord(const HIRConstGeneric&) const {
    std::abort();
}

namespace {
    HIRTraitPath makeTraitPath(const char* name, bool applyElision) {
        std::unique_ptr<HIRGenericParams> hrtbs;
        if (applyElision) {
            hrtbs = std::make_unique<HIRGenericParams>();
        }

        std::vector<RcString> components;
        components.push_back(RcString::newInterned(name));
        HIRGenericPath path(HIRSimplePath(RcString::newInterned("test"), std::move(components)));
        HIRTraitPath trait(std::move(hrtbs), std::move(path));
        trait.lifetimeElision = applyElision;
        return trait;
    }
}

int main() {
    const auto fnBeforeElision = makeTraitPath("Fn", true);
    const auto fnAfterElision = makeTraitPath("Fn", false);
    const auto unrelated = makeTraitPath("Unrelated", false);

    assert(fnBeforeElision == fnAfterElision);
    assert(fnBeforeElision != unrelated);
    assert(fnAfterElision != unrelated);
}
