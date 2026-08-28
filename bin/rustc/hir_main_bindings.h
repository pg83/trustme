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

extern void HIRDump(std::ostream& sink, const HIRCrate& crate);
extern HIRCrate* LowerHIRFromAST(const WireBoard& wb, stl::ObjPool* pool, ASTCrate& crate);
extern void HIRSerialise(const std::string& filename, const HIRCrate& crate);

extern HIRCrate* HIRDeserialise(u32& id, stl::ObjPool* pool, HIRTypeInterner& types, const std::string& filename);
extern RcString HIRDeserialiseJustName(const std::string& filename);
