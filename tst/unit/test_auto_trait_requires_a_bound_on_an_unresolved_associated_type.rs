//@ compile-fail: error:
#![feature(auto_traits)]

auto trait Portable {}
trait Source { type Item: ?Sized; }
fn require<T: ?Sized + Portable>() {}
fn inspect<T: Source>() { require::<T::Item>(); }

fn main() {}
