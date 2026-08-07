/*
 * MRustC - Rust Compiler
 * - By John Hodge (Mutabah/thePowersGang)
 *
 * hir/main_bindings.hpp
 * - Functions in hir/ used by main
 */
#pragma once

#include <iostream>
#include <string>

class RcString;

namespace AST {
    class Crate;
}

namespace HIR {
    class Crate;
}

namespace stl {
    class ObjPool;
}

extern void HIR_Dump(::std::ostream& sink, const ::HIR::Crate& crate);
extern ::HIR::Crate* LowerHIR_FromAST(stl::ObjPool* pool, ::AST::Crate& crate);
extern void HIR_Serialise(const ::std::string& filename, const ::HIR::Crate& crate);

extern ::HIR::Crate* HIR_Deserialise(stl::ObjPool* pool, const ::std::string& filename);
extern RcString HIR_Deserialise_JustName(const ::std::string& filename);
