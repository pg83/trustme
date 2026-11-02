//@ compile-fail: trait
#![feature(trait_alias, trivial_bounds)]
struct Wrapper<F>(F);
trait Mark {}
impl Mark for Wrapper<fn(&'static u8)> {}
trait Required = Sized where Wrapper<fn(&'static u8)>: Mark, Wrapper<for<'a> fn(&'a u8)>: Mark;
fn demand<T: Required>() {}
fn main() { demand::<u16>(); }
