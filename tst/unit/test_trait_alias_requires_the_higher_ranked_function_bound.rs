//@ compile-fail: trait
#![feature(trait_alias, trivial_bounds)]
trait Mark {}
impl Mark for fn(&'static u8) {}
trait Required = Sized where fn(&'static u8): Mark, for<'a> fn(&'a u8): Mark;
fn demand<T: Required>() {}
fn main() { demand::<u16>(); }
