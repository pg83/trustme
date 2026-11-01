//@ compile-fail: Failed to find an impl
#![feature(trait_alias, trivial_bounds)]
trait Mark {}
impl Mark for fn(u8) -> u16 {}
trait Required = Sized where fn(u8) -> u16: Mark, unsafe fn(u8) -> u16: Mark;
fn demand<T: Required>() {}
fn main() { demand::<u16>(); }
