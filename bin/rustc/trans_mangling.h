#pragma once

#include <string>
#include "debug.h"

    struct HIRSimplePath;
    class HIRGenericPath;
    class HIRPath;
    class HIRTypeData;
    using HIRTypeRef = const HIRTypeData*;

extern ::FmtLambda TransMangle(const HIRSimplePath& path);
extern ::FmtLambda TransMangle(const HIRGenericPath& path);
extern ::FmtLambda TransMangle(const HIRPath& path);
extern ::FmtLambda TransMangle(const HIRTypeData* ty);
