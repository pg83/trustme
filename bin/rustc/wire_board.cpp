#include "wire_board.h"
#include "expand_common.h"
#include "ident.h"
#include "macro_rules_macro_rules.h"

#include <std/mem/obj_pool.h>

WireBoard::WireBoard(stl::ObjPool* pool)
    : pool(pool)
    , hygiene(pool->make<HygieneContext>())
    , expandRegistry(pool->make<ExpandRegistry>(pool))
    , macroDefinitions(pool->make<MacroDefinitionContext>())
    , macroLog(pool->make<MacroLogContext>())
{
}
