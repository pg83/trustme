//@ compile-fail: Unexpected token
#![feature(decl_macro)]
macro broken = ();
fn main() {}
