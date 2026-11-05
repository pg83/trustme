//@ compile-fail: error:
#![feature(auto_traits, extern_types)]

auto trait Portable {}
extern "C" { type Foreign; }
fn require<T: ?Sized + Portable>() {}

fn main() { require::<Foreign>(); }
