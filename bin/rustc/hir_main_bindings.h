#pragma once

#include <string>
#include <iostream>

class RcString;

class ASTCrate;

namespace HIR {
    class Crate;
    class TypeInterner;
}

namespace stl {
    class ObjPool;
}

extern void HIRDump(::std::ostream& sink, const ::HIR::Crate& crate);
extern ::HIR::Crate* LowerHIRFromAST(stl::ObjPool* pool, ASTCrate& crate);
extern void HIRSerialise(const ::std::string& filename, const ::HIR::Crate& crate);

extern ::HIR::Crate* HIRDeserialise(stl::ObjPool* pool, ::HIR::TypeInterner& types, const ::std::string& filename);
extern RcString HIRDeserialiseJustName(const ::std::string& filename);
