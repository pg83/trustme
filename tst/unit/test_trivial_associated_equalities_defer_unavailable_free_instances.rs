//@ crate-type: lib
#![feature(trivial_bounds)]
pub trait Family { type Item; }
impl Family for u8 { type Item = u16; }
fn required<T: Family<Item = u32>>() -> u32 { 37 }
pub fn deferred<U>() -> u32 where u8: Family<Item = u32> { required::<u8>() }
