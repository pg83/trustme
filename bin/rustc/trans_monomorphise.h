#pragma once

#include "mir_mir_ptr.h"
#include "trans_trans_list.h"

namespace HIR {
    class Crate;
}

extern ::MIR::FunctionPointer Trans_Monomorphise(const ::StaticTraitResolve& crate, const Trans_Params& params, const ::MIR::FunctionPointer& tpl);
