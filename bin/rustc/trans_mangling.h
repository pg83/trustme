#pragma once
#include <string>
#include "debug.h"

namespace HIR {
    struct SimplePath;
    class GenericPath;
    class Path;
    class TypeData;
    using TypeRef = const TypeData*;
}

extern ::FmtLambda Trans_Mangle(const ::HIR::SimplePath& path);
extern ::FmtLambda Trans_Mangle(const ::HIR::GenericPath& path);
extern ::FmtLambda Trans_Mangle(const ::HIR::Path& path);
extern ::FmtLambda Trans_Mangle(const ::HIR::TypeData* ty);
