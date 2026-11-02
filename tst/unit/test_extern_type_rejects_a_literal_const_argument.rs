//@ compile-fail: generic arguments are not allowed
#![feature(extern_types)]
extern "C" { type Foreign; }
fn consume(_: *const Foreign<3>) {}
fn main() {}
