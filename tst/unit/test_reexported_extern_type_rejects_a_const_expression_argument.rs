//@ compile-fail: generic
#![feature(extern_types)]
mod source { extern "C" { pub type Foreign; } }
use source::Foreign as Imported;
fn consume(_: *const Imported<{ 5 + 6 }>) {}
fn main() {}
