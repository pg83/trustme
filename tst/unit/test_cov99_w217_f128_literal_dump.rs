//@ crate-type: lib
//@ compile-flags: -Zstop-after=expand -Zdump-ast
//@ output-contains: _1_ast.rs _f128
#![feature(f128)]

pub fn wide() -> f128 {
    7f128
}
