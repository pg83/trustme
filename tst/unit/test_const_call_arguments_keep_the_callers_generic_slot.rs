#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
const fn count<const N: usize>() -> usize { N + 1 }
fn relay<const N: usize>(value: [u8; count::<N>()]) -> [u8; count::<N>()] where [u8; count::<N>()]: Sized { value }
fn forward<const UNUSED: usize, const N: usize>(value: [u8; count::<N>()]) -> [u8; count::<N>()] where [u8; count::<N>()]: Sized { relay::<N>(value) }
fn main() { assert_eq!(forward::<91, 3>([2, 3, 5, 7]), [2, 3, 5, 7]); }
