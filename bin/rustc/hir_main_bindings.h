#pragma once

#include "output.h"

#include <std/sys/types.h>
#include <std/lib/vector.h>

#include <string>

class RcString;

class ASTCrate;
struct WireBoard;

class HIRCrate;
class HIRExprPtr;
struct HIRTypeInterner;

struct HIRSerialiseWriter;

namespace stl {
    class ObjPool;
    class StringView;
}

void HIRDump(stl::ZeroCopyOutput& sink, HIRCrate& crate);
void HIRDumpExpr(stl::ZeroCopyOutput& sink, HIRExprPtr& expr);
void HIRSerialise(const std::string& filename, const HIRCrate& crate);
HIRSerialiseWriter* HIRSerialiseBegin(stl::ObjPool& pool, stl::StringView filename, const HIRCrate& crate);
void HIRSerialiseFinish(HIRSerialiseWriter& out, const stl::Vector<u64>& exportedGenericInstances);

HIRCrate* HIRDeserialise(u32& id, stl::ObjPool* pool, HIRTypeInterner& types, const std::string& filename);
RcString HIRDeserialiseJustName(const std::string& filename);
