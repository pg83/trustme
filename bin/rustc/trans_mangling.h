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

extern ::FmtLambda TransMangle(const ::HIR::SimplePath& path);
extern ::FmtLambda TransMangle(const ::HIR::GenericPath& path);
extern ::FmtLambda TransMangle(const ::HIR::Path& path);
extern ::FmtLambda TransMangle(const ::HIR::TypeData* ty);
