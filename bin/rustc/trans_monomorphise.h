#pragma once

#include "mir_mir_ptr.h"
#include "trans_trans_list.h"

class HIRCrate;

MIRFunctionPointer TransMonomorphise(const ::StaticTraitResolve& crate, const TransParams& params, const MIRFunctionPointer& tpl);
void TransMonomorphiseList(const WireBoard& wb, HIRCrate& crate, TransList& list, unsigned mirOptLevel);
