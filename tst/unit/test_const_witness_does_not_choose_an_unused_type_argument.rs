//@ compile-fail: Failed to infer type
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
use std::mem::size_of;
trait Layout<T, const N: usize> { fn width(&self) -> usize; fn relay(&self) -> usize; }
struct Wire;
impl<T, const N: usize> Layout<T, N> for Wire where [u8; N + 1]: Sized {
    fn width(&self) -> usize { N + size_of::<T>() }
    fn relay(&self) -> usize { self.width() }
}
fn main() { let _ = <Wire as Layout<u16, 3>>::relay(&Wire); }
