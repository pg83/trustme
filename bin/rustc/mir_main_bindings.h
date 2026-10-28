#pragma once

#include "output.h"
#include "hir_hir.h"

struct WireBoard;

class TransList;

void MIRDump(stl::ZeroCopyOutput& sink, HIRCrate& crate);
