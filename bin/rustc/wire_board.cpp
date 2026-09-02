#include "wire_board.h"

#include "trans_target.h"
#include "expand_common.h"
#include "mir_operations.h"
#include "trans_mangling.h"
#include "hir_conv_constant_evaluation.h"
#include "hir_typeck_helpers.h"
#include "hir_hir.h"

#include <std/mem/obj_pool.h>

using namespace stl;

WireBoard::WireBoard(ObjPool* pool)
    : pool(pool)
    , expandRegistry(pool->make<ExpandRegistry>(pool))
{
    TargetCreateLayoutContext(*this, *pool);
    MIRCreateOperationsContext(*this, *pool);
    CtfeCreateContext(*this, *pool);
    NextSolverCreateCrateCache(*this, *pool);
    HIRCreateMutableOwnerCache(*this, *pool);
    TransCreateManglingContext(*this, *pool);
}
