#pragma once

#include "debug.h"

#include "rc_string.h"

struct HIRSimplePath;
class HIRGenericPath;
class HIRPath;
class HIRTypeData;
using HIRTypeRef = const HIRTypeData*;

extern RcString TransMangle(const HIRSimplePath& path);
extern RcString TransMangle(const HIRGenericPath& path);
extern RcString TransMangle(const HIRPath& path);
extern RcString TransMangle(const HIRTypeData* ty);
