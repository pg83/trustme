//@ compile-fail: Return-type notation does not support equality constraints
#![feature(return_type_notation)]
trait Factory { fn create() -> impl Copy; }
fn require<T: Factory<create(..) = u8>>() {}
fn main() {}
