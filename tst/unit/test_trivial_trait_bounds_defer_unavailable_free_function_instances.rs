//@ crate-type: lib
#![feature(trivial_bounds)]
pub trait Action { fn act(&self) -> u32; }
pub struct Missing;
fn required<T: Action>(value: &T) -> u32 { value.act() }
pub fn deferred<U>(value: &Missing) -> u32 where Missing: Action { required(value) }
