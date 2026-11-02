//@ compile-fail: Incorrect number of parameters
#![feature(extern_types)]
extern "C" { type Foreign; }
fn consume(_: *const Foreign<u16>) {}
fn main() {}
