//@ aux-build: opaque_codegen_shapes.rs
//@ input: test_opaque_return_codegen_shapes.rs
//@ compile-only
//@ compile-flags: -Ccodegen-type=monomir -Cemit-cpp-only -Zmir-opt-level=0
//@ output-contains: .mir ::local
//@ output-contains: .mir ::sequence
include!("test_opaque_return_codegen_shapes.rs");
