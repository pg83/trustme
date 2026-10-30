//@ compile-fail: CoerceUnsized
#![feature(coerce_unsized)]
use std::ops::CoerceUnsized;
fn require<S: CoerceUnsized<D>, D>() {}
fn main() { require::<*mut u16, *const u16>(); }
