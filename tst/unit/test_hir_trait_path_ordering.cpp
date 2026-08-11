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
    HIR::TraitPath make_trait_path(const char* name, bool apply_elision) {
        std::unique_ptr<HIR::GenericParams> hrtbs;
        if (apply_elision) {
            hrtbs = std::make_unique<HIR::GenericParams>();
        }

        std::vector<RcString> components;
        components.push_back(RcString::new_interned(name));
        HIR::GenericPath path(HIR::SimplePath(RcString::new_interned("test"), std::move(components)));
        HIR::TraitPath trait(std::move(hrtbs), std::move(path));
        trait.m_lifetime_elision = apply_elision;
        return trait;
    }
}

int main() {
    const auto fn_before_elision = make_trait_path("Fn", true);
    const auto fn_after_elision = make_trait_path("Fn", false);
    const auto unrelated = make_trait_path("Unrelated", false);

    assert(fn_before_elision == fn_after_elision);
    assert(fn_before_elision != unrelated);
    assert(fn_after_elision != unrelated);
}
