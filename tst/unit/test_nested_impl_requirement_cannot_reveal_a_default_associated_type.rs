//@ compile-fail: Failed to find an impl
#![feature(specialization)]
#![allow(incomplete_features)]
trait Source { type Value; }
impl Source for u8 { default type Value = u16; }
trait Accepted {}
impl<T: Source<Value = u16>> Accepted for T {}
fn require<T: Accepted>() {}
fn main() { require::<u8>(); }
