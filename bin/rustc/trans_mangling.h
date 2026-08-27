#pragma once

#include "rc_string.h"

namespace stl {
    class ObjPool;
}

struct WireBoard;
struct HIRSimplePath;
class HIRGenericPath;
class HIRPath;
class HIRTypeData;
using HIRTypeRef = const HIRTypeData*;

class ManglingContext;

extern ManglingContext* TransCreateManglingContext(stl::ObjPool& pool);
extern RcString TransMangle(const WireBoard& wb, const HIRSimplePath& path);
extern RcString TransMangle(const WireBoard& wb, const HIRGenericPath& path);
extern RcString TransMangle(const WireBoard& wb, const HIRPath& path);
extern RcString TransMangle(const WireBoard& wb, const HIRTypeData* ty);
extern RcString TransMangleValue(const WireBoard& wb, const HIRSimplePath& path);
extern RcString TransMangleValue(const WireBoard& wb, const HIRGenericPath& path);
extern RcString TransMangleValue(const WireBoard& wb, const HIRPath& path);
extern RcString TransMangleTypeId(const WireBoard& wb, const HIRTypeData* ty);
