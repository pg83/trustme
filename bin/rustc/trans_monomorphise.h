#pragma once

#include "mir_mir_ptr.h"
#include "trans_trans_list.h"

namespace HIR {
    class Crate;
}

extern MIRFunctionPointer TransMonomorphise(const ::StaticTraitResolve& crate, const TransParams& params, const MIRFunctionPointer& tpl);
