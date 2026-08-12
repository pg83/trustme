#pragma once

#include <string>
#include <iostream>

class RcString;

class ASTCrate;

    class HIRCrate;
    class HIRTypeInterner;

namespace stl {
    class ObjPool;
}

extern void HIRDump(::std::ostream& sink, const HIRCrate& crate);
extern HIRCrate* LowerHIRFromAST(stl::ObjPool* pool, ASTCrate& crate);
extern void HIRSerialise(const ::std::string& filename, const HIRCrate& crate);

extern HIRCrate* HIRDeserialise(stl::ObjPool* pool, HIRTypeInterner& types, const ::std::string& filename);
extern RcString HIRDeserialiseJustName(const ::std::string& filename);
