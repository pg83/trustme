//@ compile-fail: Type mismatch between [u16] and [u16; Known(value = 3)]
#![feature(unsize)]
use std::marker::Unsize;
struct Tagged<const N: usize, T: ?Sized> { header: [u8; N], tail: T }
fn require<T: ?Sized + Unsize<U>, U: ?Sized>() {}
fn main() { require::<Tagged<2, [u16; 3]>, Tagged<3, [u16]>>(); }
