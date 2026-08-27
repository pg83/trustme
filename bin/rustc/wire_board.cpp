#include "wire_board.h"
#include "expand_common.h"
#include "ident.h"
#include "hir_expand_main_bindings.h"
#include "hir_conv_constant_evaluation.h"
#include "macro_rules_macro_rules.h"
#include "mir_operations.h"
#include "parse_context.h"
#include "proc_macro_context.h"
#include "trans_target.h"

#include <std/mem/obj_pool.h>

WireBoard::WireBoard(stl::ObjPool* pool)
    : pool(pool)
    , hygiene(pool->make<HygieneContext>())
    , expandRegistry(pool->make<ExpandRegistry>(pool))
    , hirExpand(pool->make<HIRExpandContext>())
    , macroDefinitions(pool->make<MacroDefinitionContext>())
    , macroLog(pool->make<MacroLogContext>())
    , parser(pool->make<ParseContext>())
    , procMacros(pool->make<ProcMacroContext>())
    , targetLayouts(TargetCreateLayoutContext(*pool))
    , mirOperations(MIRCreateOperationsContext(*pool))
    , ctfe(CtfeCreateContext(*pool))
{
}
