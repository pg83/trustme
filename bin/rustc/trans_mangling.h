#pragma once

#include "rc_string.h"

namespace stl {
    class ObjPool;
}

struct WireBoard;
struct HIRSimplePath;
class HIRGenericPath;
class HIRPath;
class HIRType;

void TransCreateManglingContext(WireBoard& wb, stl::ObjPool& pool);
RcString TransMangle(const WireBoard& wb, const HIRSimplePath& path);
RcString TransMangle(const WireBoard& wb, const HIRGenericPath& path);
RcString TransMangle(const WireBoard& wb, const HIRPath& path);
RcString TransMangle(const WireBoard& wb, const HIRType* ty);
RcString TransMangleValue(const WireBoard& wb, const HIRSimplePath& path);
RcString TransMangleValue(const WireBoard& wb, const HIRGenericPath& path);
RcString TransMangleValue(const WireBoard& wb, const HIRPath& path);
RcString TransMangleTypeId(const WireBoard& wb, const HIRType* ty);
