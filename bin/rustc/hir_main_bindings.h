#pragma once

#include <std/sys/types.h>

#include <string>
#include <iostream>

class RcString;

class ASTCrate;
struct WireBoard;

class HIRCrate;
class HIRTypeInterner;

namespace stl {
    class ObjPool;
}

void HIRDump(std::ostream& sink, const HIRCrate& crate);
HIRCrate* LowerHIRFromAST(const WireBoard& wb, stl::ObjPool* pool, ASTCrate& crate);
void HIRSerialise(const std::string& filename, const HIRCrate& crate);

HIRCrate* HIRDeserialise(u32& id, stl::ObjPool* pool, HIRTypeInterner& types, const std::string& filename);
RcString HIRDeserialiseJustName(const std::string& filename);
