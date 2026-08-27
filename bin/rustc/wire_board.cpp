#include "wire_board.h"
#include "ident.h"

#include <std/mem/obj_pool.h>

WireBoard::WireBoard(stl::ObjPool* pool)
    : pool(pool)
    , hygiene(pool->make<HygieneContext>())
{
}
