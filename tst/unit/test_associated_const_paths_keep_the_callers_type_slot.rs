#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
trait Width { const N: usize; }
impl Width for u16 { const N: usize = 2; }
impl Width for [u8; 3] { const N: usize = 3; }
fn relay<T: Width>(value: [u8; T::N]) -> [u8; T::N] where [u8; T::N]: Sized { value }
fn forward<A, T: Width>(value: [u8; T::N]) -> [u8; T::N] where [u8; T::N]: Sized { relay::<T>(value) }
fn main() { assert_eq!(forward::<u64, u16>([2, 3]), [2, 3]); assert_eq!(forward::<u8, [u8; 3]>([5, 7, 11]), [5, 7, 11]); }
