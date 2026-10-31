//@ compile-fail: Failed to find an impl
#![feature(specialization)]
#![allow(incomplete_features)]
trait Rank { type Output; }
impl Rank for u8 { default type Output = u16; }
fn require<T: Rank<Output = u16>>() {}
fn main() { require::<u8>(); }
