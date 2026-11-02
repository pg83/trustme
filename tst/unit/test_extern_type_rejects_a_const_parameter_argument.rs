//@ compile-fail: generic arguments are not allowed
#![feature(extern_types)]
extern "C" { type Foreign; }
fn consume<const N: usize>(_: *const Foreign<N>) {}
fn main() {}
