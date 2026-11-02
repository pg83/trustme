//@ compile-fail: generic
#![feature(extern_types)]
extern "C" { type Foreign; }
fn consume(_: *const Foreign<{ 4 + 5 }>) {}
fn main() {}
