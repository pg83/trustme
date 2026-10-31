#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
use std::mem::size_of;
fn relay<T>(value: [u8; size_of::<T>()]) -> [u8; size_of::<T>()] where [u8; size_of::<T>()]: Sized { value }
fn forward<A, T>(value: [u8; size_of::<T>()]) -> [u8; size_of::<T>()] where [u8; size_of::<T>()]: Sized { relay::<T>(value) }
fn main() { assert_eq!(forward::<u64, u16>([2, 3]), [2, 3]); assert_eq!(forward::<u8, [u8; 3]>([5, 7, 11]), [5, 7, 11]); }
