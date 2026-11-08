//@ compile-fail: Generics on extern type
#![feature(extern_types)]
extern "C" { type Foreign<T>; }
fn main() {}
