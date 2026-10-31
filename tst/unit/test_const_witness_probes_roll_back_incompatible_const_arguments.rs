#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
use std::mem::size_of;
const fn bytes<T, const N: usize>() -> usize { size_of::<T>() + N }
trait Layout<X> { fn width(&self) -> usize; }
struct Wire;
impl<X> Layout<X> for Wire where [u8; bytes::<X, 4>()]: Sized { fn width(&self) -> usize { size_of::<X>() } }
fn relay<T>() -> usize where [u8; bytes::<T, 8>()]: Sized, [u8; bytes::<T, 4>()]: Sized { Wire.width() }
fn main() { assert_eq!(relay::<u16>(), 2); assert_eq!(relay::<[u8; 5]>(), 5); }
