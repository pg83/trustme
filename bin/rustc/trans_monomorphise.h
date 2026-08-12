#pragma once

#include "mir_mir_ptr.h"
#include "trans_trans_list.h"

namespace HIR {
    class Crate;
}

extern ::MIR::FunctionPointer TransMonomorphise(const ::StaticTraitResolve& crate, const TransParams& params, const ::MIR::FunctionPointer& tpl);
