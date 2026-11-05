//@ compile-fail: error:
#![feature(auto_traits)]

auto trait Portable {}
fn require<T: ?Sized + Portable>() {}
fn inspect<T: ?Sized>() { require::<T>(); }

fn main() {}
