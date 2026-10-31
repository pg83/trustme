//@ compile-fail: Failed to infer type
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
use std::mem::size_of;
trait Layout<X> { fn width(&self) -> usize; }
struct Wire;
impl<X> Layout<X> for Wire where [u8; size_of::<X>()]: Sized {
    fn width(&self) -> usize { size_of::<X>() }
}
fn ambiguous<T, U>() -> usize where [u8; size_of::<T>()]: Sized, [u8; size_of::<U>()]: Sized {
    Wire.width()
}
fn main() { let _ = ambiguous::<u16, u32>(); }
