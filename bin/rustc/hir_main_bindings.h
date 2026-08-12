#pragma once

#include <iostream>
#include <string>

class RcString;

namespace AST {
    class Crate;
}

namespace HIR {
    class Crate;
    class TypeInterner;
}

namespace stl {
    class ObjPool;
}

extern void HIRDump(::std::ostream& sink, const ::HIR::Crate& crate);
extern ::HIR::Crate* LowerHIRFromAST(stl::ObjPool* pool, ::AST::Crate& crate);
extern void HIRSerialise(const ::std::string& filename, const ::HIR::Crate& crate);

extern ::HIR::Crate* HIRDeserialise(stl::ObjPool* pool, ::HIR::TypeInterner& types, const ::std::string& filename);
extern RcString HIRDeserialiseJustName(const ::std::string& filename);
