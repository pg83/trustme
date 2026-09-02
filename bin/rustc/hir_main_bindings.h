#pragma once

#include "output.h"

#include <std/sys/types.h>

#include <string>

class RcString;

class ASTCrate;
struct WireBoard;

class HIRCrate;
class HIRExprPtr;
class HIRTypeInterner;

namespace stl {
    class ObjPool;
}

void HIRDump(stl::ZeroCopyOutput& sink, HIRCrate& crate);
void HIRDumpExpr(stl::ZeroCopyOutput& sink, HIRExprPtr& expr);
void HIRSerialise(const std::string& filename, const HIRCrate& crate);

HIRCrate* HIRDeserialise(u32& id, stl::ObjPool* pool, HIRTypeInterner& types, const std::string& filename);
RcString HIRDeserialiseJustName(const std::string& filename);
