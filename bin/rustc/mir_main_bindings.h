#pragma once

#include "output.h"
#include "hir_hir.h"

struct WireBoard;

class TransList;

void MIRDump(stl::ZeroCopyOutput& sink, HIRCrate& crate);
void MIRDumpFcn(stl::ZeroCopyOutput& sink, const MIRFunction& fcn, unsigned int il = 0);
