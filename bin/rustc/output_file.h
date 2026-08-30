#pragma once

#include "output.h"

#include <std/mem/obj_pool.h>

stl::ZeroCopyOutput* outputFile(stl::ObjPool& pool, stl::StringView path);
