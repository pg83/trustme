#![feature(explicit_tail_calls)]
#![allow(incomplete_features)]
trait StaticStep { fn step(value: u32) -> u32; }
impl<T> StaticStep for T { fn step(value: u32) -> u32 { value + 3 } }
#[inline(never)]
fn relay<T: StaticStep>(value: u32) -> u32 { become T::step(value) }
fn marker<T: StaticStep>(_: T) -> u32 { relay::<T>(34) }
fn answer() -> u32 { 37 }
struct Token(u32);
fn main() { assert_eq!((marker(answer), marker(Token)), (37, 37)); }
