//@ compile-fail: associated item constraints are not allowed here
#![feature(associated_const_equality)]
struct Holder<T>(T);
#[derive(Clone)]
struct Invalid { value: Holder<COUNT = 3> }
fn main() {}
